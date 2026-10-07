/*
 * Copyright (c) 2026 Hailo Technologies Ltd. All rights reserved.
 *
 * Driver backend that talks to DSP firmware running in the Xtensa instruction
 * set simulator instead of to the XRP kernel driver.
 *
 * It implements the same driver_* seam as hailodsp_driver.cpp, so everything
 * above it - device handling, the priority queue, image conversion, every
 * public API - is the code that ships. A device is a simulator process; a
 * command is a record on a pipe carrying exactly what the kernel would have
 * placed in shared memory.
 *
 * Configuration comes from the environment, with compile-time defaults:
 *   HAILODSP_ISS_FW         path to dsp-fw-sim.elf
 *   HAILODSP_ISS_XTRUN      path to xt-run
 *   HAILODSP_ISS_TIMEOUT_S  seconds to wait for one command (default 3600)
 *   HAILODSP_ISS_KEEP       leave session directories behind for inspection
 */

#include "buffer_list.hpp"
#include "dsp_tracer.hpp"
#include "hailodsp_driver.hpp"
#include "logger_macros.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dsp_sim_protocol.h>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <poll.h>
#include <string>
#include <user_dsp_interface.h>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef DSP_ISS_DEFAULT_FIRMWARE
#define DSP_ISS_DEFAULT_FIRMWARE ""
#endif
#ifndef DSP_ISS_DEFAULT_XTRUN
#define DSP_ISS_DEFAULT_XTRUN ""
#endif

namespace {

constexpr const char *REQUEST_PIPE = "to_dsp.fifo";
constexpr const char *RESPONSE_PIPE = "to_host.fifo";
constexpr const char *FIRMWARE_LOG = "dsp.log";

/* The board allocates buffers from the kernel's contiguous pool, which hands
   out page-aligned memory. Imaging kernels choose their SIMD path from the
   alignment they are given, so the host has to match it or the simulator would
   exercise a different path than the hardware. */
constexpr size_t BUFFER_ALIGNMENT = 4096;

constexpr long DEFAULT_TIMEOUT_SECONDS = 3600;

/* How long to wait for a simulator to announce itself. Booting one takes
   a couple of hundred milliseconds, so this is generous; it exists so that a
   simulator that dies on startup - a missing licence seat, a corrupt image -
   fails the device open with its log rather than hanging until the much longer
   command timeout expires. */
constexpr std::chrono::seconds STARTUP_TIMEOUT{120};

/* How much of the firmware log to quote when a command fails. The tail is where
   a fault reports itself, and it is what a developer would have read out of
   /dev/dsp_log0 on a board. */
constexpr size_t LOG_TAIL_BYTES = 2048;

/* How often to look up from a pipe that is not moving and check the simulator
   is still alive. */
constexpr std::chrono::milliseconds LIVENESS_POLL_INTERVAL{500};

std::string environment_or(const char *name, const std::string &fallback)
{
    const char *value = ::getenv(name);
    return (value != nullptr && value[0] != '\0') ? std::string(value) : fallback;
}

/* Read once, on the first device open: the environment does not change under a
   running process, and re-reading it per pipe write showed up in profiles. */
struct Configuration {
    std::string firmware;
    std::string xtrun;
    std::chrono::milliseconds timeout;
    bool keep_sessions;

    static const Configuration &get()
    {
        static const Configuration configuration = []() {
            const long seconds = std::strtol(environment_or("HAILODSP_ISS_TIMEOUT_S", "").c_str(), nullptr, 10);
            return Configuration{
                environment_or("HAILODSP_ISS_FW", DSP_ISS_DEFAULT_FIRMWARE),
                environment_or("HAILODSP_ISS_XTRUN", DSP_ISS_DEFAULT_XTRUN),
                std::chrono::seconds(seconds > 0 ? seconds : DEFAULT_TIMEOUT_SECONDS),
                !environment_or("HAILODSP_ISS_KEEP", "").empty(),
            };
        }();
        return configuration;
    }
};

/* A plane the firmware must see. USERPTR planes are already addressable;
   DMABUF planes are mapped for as long as the command runs. */
class MappedBuffer {
   public:
    explicit MappedBuffer(const struct xrp_ioctl_buffer &buffer) : m_size(buffer.size)
    {
        if (buffer.memory_type == XRP_MEMORY_TYPE_USERPTR) {
            m_address = reinterpret_cast<void *>(static_cast<uintptr_t>(buffer.addr));
            return;
        }

        void *mapped = mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
        if (mapped == MAP_FAILED) {
            LOGGER__ERROR("Error: Failed to map buffer fd {} of size {}: {}", buffer.fd, m_size, strerror(errno));
            return;
        }
        m_address = mapped;
        m_owned = true;
    }

    ~MappedBuffer()
    {
        if (m_owned && m_address != nullptr) {
            munmap(m_address, m_size);
        }
    }

    MappedBuffer(const MappedBuffer &) = delete;
    MappedBuffer &operator=(const MappedBuffer &) = delete;
    MappedBuffer(MappedBuffer &&other) noexcept
        : m_address(other.m_address), m_size(other.m_size), m_owned(other.m_owned)
    {
        other.m_address = nullptr;
        other.m_owned = false;
    }
    MappedBuffer &operator=(MappedBuffer &&) = delete;

    bool valid() const { return m_address != nullptr; }
    void *data() const { return m_address; }
    uint32_t size() const { return m_size; }

   private:
    void *m_address = nullptr;
    uint32_t m_size = 0;
    bool m_owned = false;
};

/* Two entries name the same buffer when they point at the same memory: the
   same user pointer, or the same dma-buf. */
bool same_buffer(const struct xrp_ioctl_buffer &left, const struct xrp_ioctl_buffer &right)
{
    if (left.memory_type != right.memory_type) {
        return false;
    }
    return (left.memory_type == XRP_MEMORY_TYPE_USERPTR) ? (left.addr == right.addr) : (left.fd == right.fd);
}

/*
 * Group the entries that name the same buffer.
 *
 * A command can name one buffer more than once: two crops into one destination,
 * an image that is both source and result. The kernel hands the DSP the same
 * address each time, so those entries have to share one allocation in the
 * simulator too, or each copy would overwrite what the previous one wrote. The
 * first entry of a group leads it and carries the group's bytes both ways.
 */
dsp_status describe_buffers(const std::vector<struct xrp_ioctl_buffer> &buffers,
                            std::vector<dsp_sim_buffer_t> &descriptors)
{
    descriptors.assign(buffers.size(), dsp_sim_buffer_t{});
    for (size_t i = 0; i < buffers.size(); i++) {
        descriptors[i].access_flags = buffers[i].flags;
        descriptors[i].size = buffers[i].size;
        descriptors[i].alias_of = DSP_SIM_NO_ALIAS;
        descriptors[i].group_flags = buffers[i].flags;
    }

    for (size_t i = 0; i < buffers.size(); i++) {
        for (size_t j = 0; j < i; j++) {
            if (descriptors[j].alias_of != DSP_SIM_NO_ALIAS || !same_buffer(buffers[i], buffers[j])) {
                continue;
            }
            if (buffers[i].size != buffers[j].size) {
                LOGGER__ERROR("Error: The same buffer appears with sizes {} and {} in one command", buffers[j].size,
                              buffers[i].size);
                return DSP_INVALID_ARGUMENT;
            }
            descriptors[i].alias_of = static_cast<uint32_t>(j);
            descriptors[j].group_flags |= buffers[i].flags;
            break;
        }
    }
    return DSP_SUCCESS;
}

bool is_group_leader(const dsp_sim_buffer_t &descriptor)
{
    return descriptor.alias_of == DSP_SIM_NO_ALIAS;
}

struct Statistics {
    std::chrono::microseconds total{0};
    std::chrono::microseconds maximum{0};
    uint32_t commands = 0;
};

/* One simulator process and the pipe pair that carries commands to it. */
class SimSession {
   public:
    ~SimSession() { shutdown(); }

    dsp_status start();
    dsp_status run_command(const std::string *nsid,
                           const std::vector<struct xrp_ioctl_buffer> &buffers,
                           const void *in_data,
                           size_t in_data_size,
                           void *out_data,
                           size_t out_data_size);
    void shutdown();

    std::mutex &lock() { return m_lock; }
    Statistics &statistics() { return m_statistics; }

   private:
    dsp_status create_session_directory();
    dsp_status spawn_simulator();
    dsp_status connect_pipes();
    dsp_status await_ready();
    void reap_simulator();

    dsp_status transfer(const std::string *nsid,
                        const std::vector<struct xrp_ioctl_buffer> &buffers,
                        const void *in_data,
                        size_t in_data_size,
                        void *out_data,
                        size_t out_data_size);
    dsp_status send_request(const std::string *nsid,
                            const std::vector<dsp_sim_buffer_t> &descriptors,
                            const std::vector<MappedBuffer> &mapped,
                            const void *in_data,
                            size_t in_data_size,
                            size_t out_data_size);
    dsp_status receive_response(const std::vector<dsp_sim_buffer_t> &descriptors,
                                const std::vector<MappedBuffer> &mapped,
                                void *out_data,
                                size_t out_data_size);

    bool write_exact(const void *data, size_t length);
    bool read_exact(void *data, size_t length, std::chrono::milliseconds timeout);
    bool wait_for_pipe(int fd, short events, std::chrono::steady_clock::time_point deadline, const char *what);
    bool simulator_exited();
    void report_firmware_log(const char *why);

    std::filesystem::path m_directory;
    pid_t m_simulator = -1;
    int m_request_fd = -1;
    int m_response_fd = -1;
    bool m_broken = false;
    std::mutex m_lock;
    Statistics m_statistics;
};

/* Has the simulator process gone? Reaps it if so, and clears the pid: every
   path here must stop signalling once the child has been reaped, or a later
   kill could land on a recycled pid. */
bool SimSession::simulator_exited()
{
    if (m_simulator <= 0) {
        return true;
    }
    int status = 0;
    const pid_t reaped = waitpid(m_simulator, &status, WNOHANG);
    if (reaped == 0) {
        return false;
    }
    m_simulator = -1;
    return true;
}

bool SimSession::wait_for_pipe(int fd, short events, std::chrono::steady_clock::time_point deadline, const char *what)
{
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            LOGGER__ERROR("Error: Timed out {} the simulator", what);
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        /* Wait in slices rather than straight to the deadline, so a simulator
           that died is noticed in seconds instead of after a command timeout
           that is measured in hours. A death mid-request shows up on the
           response pipe as end of file, but nothing arrives there while the
           request is still being written. */
        const auto slice = std::min<long>(remaining, LIVENESS_POLL_INTERVAL.count());

        struct pollfd descriptor = {.fd = fd, .events = events, .revents = 0};
        const int ready = poll(&descriptor, 1, static_cast<int>(slice));
        if (ready > 0) {
            return true;
        }
        if (ready < 0 && errno != EINTR) {
            LOGGER__ERROR("Error: Cannot wait on the simulator pipe: {}", strerror(errno));
            return false;
        }
        if (simulator_exited()) {
            LOGGER__ERROR("Error: The simulator exited while a command was in flight");
            return false;
        }
    }
}

bool SimSession::write_exact(const void *data, size_t length)
{
    const auto deadline = std::chrono::steady_clock::now() + Configuration::get().timeout;
    const uint8_t *cursor = static_cast<const uint8_t *>(data);

    while (length > 0) {
        if (!wait_for_pipe(m_request_fd, POLLOUT, deadline, "writing to")) {
            return false;
        }
        const ssize_t written = write(m_request_fd, cursor, length);
        if (written > 0) {
            cursor += written;
            length -= static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        }
        LOGGER__ERROR("Error: Write to the simulator failed: {}", strerror(errno));
        return false;
    }
    return true;
}

bool SimSession::read_exact(void *data, size_t length, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    uint8_t *cursor = static_cast<uint8_t *>(data);

    while (length > 0) {
        if (!wait_for_pipe(m_response_fd, POLLIN, deadline, "waiting for")) {
            return false;
        }
        const ssize_t got = read(m_response_fd, cursor, length);
        if (got > 0) {
            cursor += got;
            length -= static_cast<size_t>(got);
            continue;
        }
        if (got == 0) {
            LOGGER__ERROR("Error: The simulator exited while a command was in flight");
            return false;
        }
        if (errno == EINTR || errno == EAGAIN) {
            continue;
        }
        LOGGER__ERROR("Error: Read from the simulator failed: {}", strerror(errno));
        return false;
    }
    return true;
}

void SimSession::report_firmware_log(const char *why)
{
    const std::filesystem::path path = m_directory / FIRMWARE_LOG;
    FILE *log = fopen(path.c_str(), "rb");
    if (log == nullptr) {
        LOGGER__ERROR("{} (no firmware log at {})", why, path.string());
        return;
    }

    std::string contents;
    char chunk[4096];
    size_t got = 0;
    while ((got = fread(chunk, 1, sizeof(chunk), log)) > 0) {
        contents.append(chunk, got);
    }
    fclose(log);

    if (contents.size() > LOG_TAIL_BYTES) {
        contents = "..." + contents.substr(contents.size() - LOG_TAIL_BYTES);
    }
    LOGGER__ERROR("{}. Firmware log:\n{}", why, contents);
}

dsp_status SimSession::create_session_directory()
{
    /* Honour TMPDIR: CI gives each job a scratch directory, and a session that
       lands outside it is not cleaned up when the job is torn down. */
    std::error_code error;
    const std::filesystem::path base = std::filesystem::temp_directory_path(error);
    if (error) {
        LOGGER__ERROR("Error: Cannot locate a temporary directory: {}", error.message());
        return DSP_OPEN_DEVICE_FAILED;
    }

    std::string pattern = (base / "hailodsp-iss-XXXXXX").string();
    if (mkdtemp(pattern.data()) == nullptr) {
        LOGGER__ERROR("Error: Cannot create a session directory: {}", strerror(errno));
        return DSP_OPEN_DEVICE_FAILED;
    }
    m_directory = pattern;

    if (mkfifo((m_directory / REQUEST_PIPE).c_str(), 0600) != 0 ||
        mkfifo((m_directory / RESPONSE_PIPE).c_str(), 0600) != 0) {
        LOGGER__ERROR("Error: Cannot create the session pipes: {}", strerror(errno));
        return DSP_OPEN_DEVICE_FAILED;
    }
    return DSP_SUCCESS;
}

dsp_status SimSession::spawn_simulator()
{
    const Configuration &configuration = Configuration::get();

    /* execl() needs paths that survive the chdir() into the session directory,
       and the child cannot report a lookup failure beyond its exit code, so
       both are resolved here where a real message can be produced. */
    std::error_code error;
    const std::filesystem::path firmware = std::filesystem::absolute(configuration.firmware, error);
    if (error) {
        LOGGER__ERROR("Error: Cannot resolve firmware path {}: {}", configuration.firmware, error.message());
        return DSP_OPEN_DEVICE_FAILED;
    }
    const std::filesystem::path xtrun = std::filesystem::absolute(configuration.xtrun, error);
    if (error) {
        LOGGER__ERROR("Error: Cannot resolve simulator path {}: {}", configuration.xtrun, error.message());
        return DSP_OPEN_DEVICE_FAILED;
    }

    m_simulator = fork();
    if (m_simulator < 0) {
        LOGGER__ERROR("Error: fork() failed: {}", strerror(errno));
        return DSP_OPEN_DEVICE_FAILED;
    }

    if (m_simulator == 0) {
        /* The firmware opens its pipes by relative name, so the session
           directory is simply the simulator's working directory. */
        if (chdir(m_directory.c_str()) != 0) {
            _exit(127);
        }
        const int log = open(FIRMWARE_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (log < 0) {
            _exit(127);
        }
        dup2(log, STDOUT_FILENO);
        dup2(log, STDERR_FILENO);
        close(log);
        execl(xtrun.c_str(), xtrun.c_str(), "--nosummary", "--exit_with_target_code", firmware.c_str(),
              static_cast<char *>(nullptr));
        _exit(127);
    }

    return DSP_SUCCESS;
}

dsp_status SimSession::connect_pipes()
{
    /* Read-write, so opening never blocks and a simulator that dies on startup
       surfaces on the response pipe rather than wedging this open. Both ends
       open the request pipe first and the response pipe second, so the opens
       rendezvous instead of deadlocking.
     *
     * Non-blocking too, because a blocking write() to a pipe does not return
     * until the whole count has been handed over: a firmware that died before
     * draining the payloads would leave this process asleep in the kernel with
     * its deadline never consulted again. Nothing else would rescue it, since
     * read-write makes us a reader of our own pipe, so the kernel never reports
     * EPIPE however long the far end has been gone. */
    m_request_fd = open((m_directory / REQUEST_PIPE).c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_request_fd < 0) {
        LOGGER__ERROR("Error: Cannot open {}: {}", REQUEST_PIPE, strerror(errno));
        return DSP_OPEN_DEVICE_FAILED;
    }

    /* Opening the read end of a fifo blocks until a writer arrives, which for a
       simulator that never got as far as opening its end means forever. Opening
       it non-blocking always succeeds, and leaves waiting for the firmware to
       the deadline-guarded read in await_ready(). */
    m_response_fd = open((m_directory / RESPONSE_PIPE).c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_response_fd < 0) {
        LOGGER__ERROR("Error: Cannot open {}: {}", RESPONSE_PIPE, strerror(errno));
        return DSP_OPEN_DEVICE_FAILED;
    }
    /* Back to blocking now the descriptor exists: read_exact() polls for
       readiness itself and expects a read that does not return EAGAIN. */
    const int flags = fcntl(m_response_fd, F_GETFL);
    if (flags < 0 || fcntl(m_response_fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        LOGGER__ERROR("Error: Cannot restore blocking mode on {}: {}", RESPONSE_PIPE, strerror(errno));
        return DSP_OPEN_DEVICE_FAILED;
    }
    return DSP_SUCCESS;
}

dsp_status SimSession::await_ready()
{
    dsp_sim_ready_t ready = {};
    if (!read_exact(&ready, sizeof(ready), STARTUP_TIMEOUT)) {
        report_firmware_log("The simulator did not come up");
        return DSP_OPEN_DEVICE_FAILED;
    }
    if (ready.magic != DSP_SIM_MAGIC_READY || ready.version != DSP_SIM_PROTOCOL_VERSION) {
        LOGGER__ERROR(
            "Error: The firmware speaks protocol version {} (magic {:#x}), this build speaks {}. "
            "Rebuild dsp-fw-sim.elf and the library from the same tree.",
            ready.version, ready.magic, DSP_SIM_PROTOCOL_VERSION);
        return DSP_OPEN_DEVICE_FAILED;
    }

    LOGGER__INFO("Simulator ready in {} ({} MB arena)", m_directory.string(), ready.arena_size / (1024 * 1024));
    return DSP_SUCCESS;
}

dsp_status SimSession::start()
{
    const Configuration &configuration = Configuration::get();

    if (configuration.firmware.empty() || configuration.xtrun.empty()) {
        LOGGER__ERROR(
            "Error: The simulator backend needs HAILODSP_ISS_FW and HAILODSP_ISS_XTRUN "
            "(no compile-time default was built in)");
        return DSP_OPEN_DEVICE_FAILED;
    }
    if (access(configuration.firmware.c_str(), R_OK) != 0) {
        LOGGER__ERROR("Error: Cannot read firmware {}: {}", configuration.firmware, strerror(errno));
        return DSP_OPEN_DEVICE_FAILED;
    }
    if (access(configuration.xtrun.c_str(), X_OK) != 0) {
        LOGGER__ERROR("Error: Cannot execute simulator {}: {}", configuration.xtrun, strerror(errno));
        return DSP_OPEN_DEVICE_FAILED;
    }

    dsp_status status = create_session_directory();
    if (status != DSP_SUCCESS) {
        return status;
    }
    status = spawn_simulator();
    if (status != DSP_SUCCESS) {
        return status;
    }
    status = connect_pipes();
    if (status != DSP_SUCCESS) {
        return status;
    }
    return await_ready();
}

void SimSession::reap_simulator()
{
    if (m_simulator <= 0) {
        return;
    }

    /* End of file on the request pipe asks the firmware to exit; insist only if
       it does not. Every path here must stop signalling once the child has been
       reaped, or a later kill could land on a recycled pid. */
    constexpr int POLL_INTERVAL_US = 10000;
    constexpr int ATTEMPTS_BEFORE_SIGTERM = 50;
    constexpr int ATTEMPTS_BEFORE_SIGKILL = 100;

    int status = 0;
    bool reaped = waitpid(m_simulator, &status, WNOHANG) != 0;
    for (int attempt = 0; !reaped && attempt < ATTEMPTS_BEFORE_SIGKILL; attempt++) {
        usleep(POLL_INTERVAL_US);
        reaped = waitpid(m_simulator, &status, WNOHANG) != 0;
        if (!reaped && attempt == ATTEMPTS_BEFORE_SIGTERM) {
            kill(m_simulator, SIGTERM);
        }
    }
    if (!reaped) {
        kill(m_simulator, SIGKILL);
        waitpid(m_simulator, &status, 0);
    }
    m_simulator = -1;
}

void SimSession::shutdown()
{
    if (m_request_fd >= 0) {
        close(m_request_fd); /* end of file asks the firmware to exit */
        m_request_fd = -1;
    }
    if (m_response_fd >= 0) {
        close(m_response_fd);
        m_response_fd = -1;
    }

    reap_simulator();

    if (m_directory.empty()) {
        return;
    }
    if (Configuration::get().keep_sessions) {
        LOGGER__INFO("Keeping session directory {}", m_directory.string());
    } else {
        std::error_code error;
        std::filesystem::remove_all(m_directory, error);
        if (error) {
            LOGGER__WARNING("Could not remove session directory {}: {}", m_directory.string(), error.message());
        }
    }
    m_directory.clear();
}

dsp_status SimSession::send_request(const std::string *nsid,
                                    const std::vector<dsp_sim_buffer_t> &descriptors,
                                    const std::vector<MappedBuffer> &mapped,
                                    const void *in_data,
                                    size_t in_data_size,
                                    size_t out_data_size)
{
    dsp_sim_request_t request = {};
    request.magic = DSP_SIM_MAGIC_REQUEST;
    request.has_nsid = (nsid != nullptr) ? 1 : 0;
    if (nsid != nullptr) {
        /* A namespace id is a fixed 16 bytes and is not terminated; the field
           is already zeroed, so a short name is padded. */
        const size_t length = std::min(nsid->size(), static_cast<size_t>(DSP_SIM_NSID_SIZE));
        memcpy(request.nsid, nsid->data(), length);
    }
    request.in_data_size = static_cast<uint32_t>(in_data_size);
    request.out_data_size = static_cast<uint32_t>(out_data_size);
    request.n_buffers = static_cast<uint32_t>(descriptors.size());

    if (!write_exact(&request, sizeof(request))) {
        return DSP_RUN_COMMAND_FAILED;
    }
    if (!descriptors.empty() && !write_exact(descriptors.data(), descriptors.size() * sizeof(descriptors[0]))) {
        return DSP_RUN_COMMAND_FAILED;
    }
    if (in_data_size > 0 && !write_exact(in_data, in_data_size)) {
        return DSP_RUN_COMMAND_FAILED;
    }

    /* Every group leader goes across, whatever its access flags say. The kernel
       hands the DSP the buffer itself, so an operation that writes only part of
       its destination leaves the rest as it found it. Sending only the readable
       buffers would hand the firmware uninitialised memory instead. */
    for (size_t i = 0; i < descriptors.size(); i++) {
        if (!is_group_leader(descriptors[i])) {
            continue;
        }
        if (!write_exact(mapped[i].data(), mapped[i].size())) {
            return DSP_RUN_COMMAND_FAILED;
        }
    }
    return DSP_SUCCESS;
}

dsp_status SimSession::receive_response(const std::vector<dsp_sim_buffer_t> &descriptors,
                                        const std::vector<MappedBuffer> &mapped,
                                        void *out_data,
                                        size_t out_data_size)
{
    dsp_sim_response_t response = {};
    if (!read_exact(&response, sizeof(response), Configuration::get().timeout)) {
        report_firmware_log("The simulator stopped answering");
        return DSP_RUN_COMMAND_FAILED;
    }
    if (response.magic != DSP_SIM_MAGIC_RESPONSE || response.n_buffers != descriptors.size()) {
        LOGGER__ERROR("Error: Malformed response from the simulator (magic {:#x}, {} buffers)", response.magic,
                      response.n_buffers);
        return DSP_RUN_COMMAND_FAILED;
    }

    /* The access the firmware actually performed. The kernel reports it too and
       nothing above this layer reads it, so it is drained and discarded. */
    std::vector<uint32_t> map_flags(descriptors.size());
    if (!map_flags.empty() &&
        !read_exact(map_flags.data(), map_flags.size() * sizeof(map_flags[0]), Configuration::get().timeout)) {
        return DSP_RUN_COMMAND_FAILED;
    }

    if (out_data_size > 0 && !read_exact(out_data, out_data_size, Configuration::get().timeout)) {
        return DSP_RUN_COMMAND_FAILED;
    }
    for (size_t i = 0; i < descriptors.size(); i++) {
        if (!is_group_leader(descriptors[i])) {
            continue; /* the leader carries this group's bytes back */
        }
        if ((descriptors[i].group_flags & DSP_SIM_BUFFER_WRITE) == 0) {
            continue;
        }
        if (!read_exact(mapped[i].data(), mapped[i].size(), Configuration::get().timeout)) {
            return DSP_RUN_COMMAND_FAILED;
        }
    }

    if (response.cmd_flags & DSP_SIM_CMD_FLAG_DELIVERY_FAIL) {
        report_firmware_log("The firmware rejected the command");
        return DSP_RUN_COMMAND_FAILED;
    }
    return DSP_SUCCESS;
}

dsp_status SimSession::transfer(const std::string *nsid,
                                const std::vector<struct xrp_ioctl_buffer> &buffers,
                                const void *in_data,
                                size_t in_data_size,
                                void *out_data,
                                size_t out_data_size)
{
    if (buffers.size() > DSP_SIM_MAX_BUFFERS) {
        LOGGER__ERROR("Error: {} buffers in one command, the simulator transport carries at most {}", buffers.size(),
                      DSP_SIM_MAX_BUFFERS);
        return DSP_INVALID_ARGUMENT;
    }

    std::vector<MappedBuffer> mapped;
    mapped.reserve(buffers.size());
    for (const auto &buffer : buffers) {
        mapped.emplace_back(buffer);
        if (!mapped.back().valid()) {
            return DSP_RUN_COMMAND_FAILED;
        }
    }

    std::vector<dsp_sim_buffer_t> descriptors;
    const dsp_status status = describe_buffers(buffers, descriptors);
    if (status != DSP_SUCCESS) {
        return status;
    }

    const dsp_status sent = send_request(nsid, descriptors, mapped, in_data, in_data_size, out_data_size);
    if (sent != DSP_SUCCESS) {
        return sent;
    }
    return receive_response(descriptors, mapped, out_data, out_data_size);
}

dsp_status SimSession::run_command(const std::string *nsid,
                                   const std::vector<struct xrp_ioctl_buffer> &buffers,
                                   const void *in_data,
                                   size_t in_data_size,
                                   void *out_data,
                                   size_t out_data_size)
{
    if (m_broken) {
        LOGGER__ERROR("Error: This simulator session is no longer usable");
        return DSP_RUN_COMMAND_FAILED;
    }

    const auto started = std::chrono::steady_clock::now();
    const dsp_status status = transfer(nsid, buffers, in_data, in_data_size, out_data, out_data_size);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started);

    if (status != DSP_SUCCESS) {
        /* The pipe is out of step once a transfer fails part-way, so later
           commands would read another command's bytes. Fail them instead. */
        m_broken = true;
        return status;
    }

    m_statistics.commands++;
    m_statistics.total += elapsed;
    m_statistics.maximum = std::max(m_statistics.maximum, elapsed);
    return DSP_SUCCESS;
}

/*
 * The seam hands devices around as an int, so sessions live in a registry keyed
 * by a handle standing in for the /dev/dsp0 file descriptor. Lookup returns a
 * shared_ptr: a device closed on one thread while another is mid-command would
 * otherwise destroy the session out from under it.
 */
std::mutex g_registry_lock;
std::map<int, std::shared_ptr<SimSession>> g_sessions;
std::atomic<int> g_next_handle{1};

std::shared_ptr<SimSession> find_session(int handle)
{
    std::lock_guard<std::mutex> guard(g_registry_lock);
    const auto found = g_sessions.find(handle);
    return (found == g_sessions.end()) ? nullptr : found->second;
}

} // namespace

dsp_status driver_open_device(int &fd)
{
    auto session = std::make_shared<SimSession>();
    const dsp_status status = session->start();
    if (status != DSP_SUCCESS) {
        return status;
    }

    const int handle = g_next_handle.fetch_add(1);
    {
        std::lock_guard<std::mutex> guard(g_registry_lock);
        g_sessions[handle] = std::move(session);
    }
    fd = handle;
    return DSP_SUCCESS;
}

dsp_status driver_close_device(int fd)
{
    std::shared_ptr<SimSession> session;
    {
        std::lock_guard<std::mutex> guard(g_registry_lock);
        const auto found = g_sessions.find(fd);
        if (found == g_sessions.end()) {
            return DSP_SUCCESS;
        }
        session = std::move(found->second);
        g_sessions.erase(found);
    }
    /* The simulator is shut down by the last reference, which is this one
       unless a command is still in flight on another thread. */
    session.reset();
    return DSP_SUCCESS;
}

dsp_status driver_allocate_buffer(int fd, size_t size, void **buffer)
{
    (void)fd;
    /* The board allocates from the kernel's contiguous pool; here the firmware
       never dereferences a host address, so ordinary memory is enough. */
    void *allocated = nullptr;
    if (posix_memalign(&allocated, BUFFER_ALIGNMENT, size) != 0) {
        LOGGER__ERROR("Error: Failed to allocate buffer of size {}", size);
        return DSP_CREATE_BUFFER_FAILED;
    }
    memset(allocated, 0, size);
    *buffer = allocated;
    return DSP_SUCCESS;
}

dsp_status driver_release_buffer(int fd, void *buffer, size_t size)
{
    (void)fd;
    (void)size;
    free(buffer);
    return DSP_SUCCESS;
}

dsp_status driver_sync_buffer_start(int fd, void *buffer, size_t size, dsp_sync_direction_t direction)
{
    (void)fd;
    (void)size;
    (void)direction;
    /* Nothing to maintain: the payload is copied over the pipe, so there is no
       cache shared with a device. */
    return buffer ? DSP_SUCCESS : DSP_INVALID_ARGUMENT;
}

dsp_status driver_sync_buffer_end(int fd, void *buffer, size_t size, dsp_sync_direction_t direction)
{
    (void)fd;
    (void)size;
    (void)direction;
    return buffer ? DSP_SUCCESS : DSP_INVALID_ARGUMENT;
}

dsp_status driver_send_command(int fd,
                               std::optional<std::string> nsid,
                               BufferList &buffer_list,
                               const void *in_data,
                               size_t in_data_size,
                               void *out_data,
                               size_t out_data_size)
{
    const std::shared_ptr<SimSession> session = find_session(fd);
    if (session == nullptr) {
        LOGGER__ERROR("Error: No simulator session for device {}", fd);
        return DSP_RUN_COMMAND_FAILED;
    }

    std::lock_guard<std::mutex> guard(session->lock());
    return session->run_command(nsid.has_value() ? &nsid.value() : nullptr, buffer_list.get_buffers(), in_data,
                                in_data_size, out_data, out_data_size);
}

dsp_status driver_send_command(int fd,
                               std::optional<std::string> nsid,
                               BufferList &buffer_list,
                               task_timeframe_t *timeframe,
                               const void *in_data,
                               size_t in_data_size,
                               void *out_data,
                               size_t out_data_size)
{
    auto &tracer = DspTracer::get_instance();

    /* Mirror the board backend: when the caller wants no output, collect the
       firmware's performance counters anyway so tracing has something to show. */
    perf_info_t perf_info = {};
    void *effective_out = out_data;
    size_t effective_out_size = out_data_size;
    const bool collecting_perf = (out_data_size == 0);
    if (collecting_perf) {
        effective_out = &perf_info;
        effective_out_size = sizeof(perf_info);
    }

    tracer.update_event_timestamp_now(timeframe, DSP_USER_SENT_IOCTL_EVENT);

    const dsp_status status =
        driver_send_command(fd, nsid, buffer_list, in_data, in_data_size, effective_out, effective_out_size);
    if (status != DSP_SUCCESS) {
        return status;
    }

    /* There is no kernel between us and the firmware, so the events the kernel
       would have timestamped collapse onto the command boundary. */
    tracer.update_event_timestamp_now(timeframe, DSP_FW_FINISHED_EVENT);

    if (collecting_perf) {
        tracer.update_perf_info(timeframe, perf_info);
    }

    return DSP_SUCCESS;
}

dsp_status driver_send_command(int fd,
                               std::optional<std::string> nsid,
                               task_timeframe_t *timeframe,
                               const void *in_data,
                               size_t in_data_size,
                               void *out_data,
                               size_t out_data_size)
{
    BufferList buffer_list;
    return driver_send_command(fd, nsid, buffer_list, timeframe, in_data, in_data_size, out_data, out_data_size);
}

dsp_status driver_send_command(int fd,
                               std::optional<std::string> nsid,
                               const void *in_data,
                               size_t in_data_size,
                               void *out_data,
                               size_t out_data_size)
{
    BufferList buffer_list;
    return driver_send_command(fd, nsid, buffer_list, in_data, in_data_size, out_data, out_data_size);
}

dsp_status driver_reset_kernel_statistics(int fd)
{
    const std::shared_ptr<SimSession> session = find_session(fd);
    if (session == nullptr) {
        return DSP_IOCTL_FAILED;
    }
    std::lock_guard<std::mutex> guard(session->lock());
    session->statistics() = Statistics{};
    return DSP_SUCCESS;
}

dsp_status driver_get_kernel_statistics(int fd, kernel_statistics &stats)
{
    const std::shared_ptr<SimSession> session = find_session(fd);
    if (session == nullptr) {
        return DSP_IOCTL_FAILED;
    }

    std::lock_guard<std::mutex> guard(session->lock());
    const Statistics &counted = session->statistics();
    stats.total_dsp_time = counted.total;
    stats.max_dsp_command_time = counted.maximum;
    stats.total_dsp_commands = counted.commands;
    /* One simulator serves one command at a time, so these are constant. */
    stats.current_threads_using_dsp = 0;
    stats.max_threads_using_dsp = 1;
    return DSP_SUCCESS;
}
