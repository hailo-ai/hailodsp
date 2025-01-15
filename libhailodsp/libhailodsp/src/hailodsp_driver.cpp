/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "aligned_uptr.hpp"
#include "dsp_tracer.hpp"
#include "fcntl.h"
#include "hailodsp_driver.hpp"
#include "logger_macros.hpp"
#include "xrp_types.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include "xrp_kernel_defs.h"

dsp_status driver_open_device(int &fd)
{
    const char *device_path = "/dev/dsp0";
    fd = open(device_path, O_RDWR | O_CLOEXEC);
    if (fd == -1) {
        LOGGER__ERROR("Error: Failed to open device \"{}\"", device_path);
        return DSP_OPEN_DEVICE_FAILED;
    }
    return DSP_SUCCESS;
}

dsp_status driver_close_device(int fd)
{
    if (fd != -1) {
        close(fd);
    }
    return DSP_SUCCESS;
}

dsp_status driver_allocate_buffer(int fd, size_t size, void **buffer)
{
    struct xrp_ioctl_alloc ioctl_alloc = {
        .size = static_cast<__u32>(size),
    };

    int ret = ioctl(fd, XRP_IOCTL_ALLOC, &ioctl_alloc);
    if (ret < 0) {
        LOGGER__ERROR("Error: Failed to allocate buffer of size {}, err={}", size, ret);
        return DSP_CREATE_BUFFER_FAILED;
    }

    *buffer = (void *)ioctl_alloc.addr;

    return DSP_SUCCESS;
}

dsp_status driver_release_buffer(int fd, void *buffer, size_t size)
{
    struct xrp_ioctl_alloc ioctl_alloc = {
        .size = static_cast<__u32>(size),
        .addr = reinterpret_cast<uintptr_t>(buffer),
    };
    int ret = ioctl(fd, XRP_IOCTL_FREE, &ioctl_alloc);
    if (ret < 0) {
        LOGGER__ERROR("Error: Failed to free buffer {}, err={}", buffer, ret);
        return DSP_UNMAP_BUFFER_FAILED;
    }
    return DSP_SUCCESS;
}

static dsp_status driver_sync_buffer(int fd,
                                     void *buffer,
                                     size_t size,
                                     dsp_sync_direction_t direction,
                                     enum ioctl_sync_access_time access_time)
{
    if (!buffer) {
        return DSP_INVALID_ARGUMENT;
    }

    struct xrp_ioctl_sync_buffer ioctl_sync = {
        .direction = direction,
        .access_time = access_time,
        .size = static_cast<__u32>(size),
        .addr = reinterpret_cast<uintptr_t>(buffer),
    };

    int ret = ioctl(fd, XRP_IOCTL_DMA_SYNC, &ioctl_sync);
    if (ret < 0) {
        LOGGER__ERROR("Error: Failed to sync buffer {}, err={}", buffer, ret);
        return DSP_SYNC_BUFFER_FAILED;
    }
    return DSP_SUCCESS;
}

dsp_status driver_sync_buffer_start(int fd, void *buffer, size_t size, dsp_sync_direction_t direction)
{
    return driver_sync_buffer(fd, buffer, size, direction, XRP_FLAG_BUFFER_SYNC_START);
}

dsp_status driver_sync_buffer_end(int fd, void *buffer, size_t size, dsp_sync_direction_t direction)
{
    return driver_sync_buffer(fd, buffer, size, direction, XRP_FLAG_BUFFER_SYNC_END);
}

dsp_status driver_send_command(int fd,
                               std::optional<std::string> nsid,
                               BufferList &buffer_list,
                               const void *in_data,
                               size_t in_data_size,
                               void *out_data,
                               size_t out_data_size)
{
    struct xrp_ioctl_queue ioctl_queue = {
        .flags = static_cast<__u32>(nsid.has_value() ? XRP_QUEUE_FLAG_NSID : 0),
        .in_data_size = static_cast<__u32>(in_data_size),
        .out_data_size = static_cast<__u32>(out_data_size),
        .buffer_size = static_cast<__u32>(buffer_list.get_buffers().size() * sizeof(struct xrp_ioctl_buffer)),
        .in_data_addr = reinterpret_cast<__u64>(in_data),
        .out_data_addr = reinterpret_cast<__u64>(out_data),
        .buffer_addr = reinterpret_cast<__u64>(buffer_list.get_buffers().data()),
        .nsid_addr = reinterpret_cast<__u64>(nsid.has_value() ? nsid.value().c_str() : nullptr),
        .perf_stats_enabled = false,
    };

    int ret = ioctl(fd, XRP_IOCTL_QUEUE, &ioctl_queue);
    if (ret < 0) {
        LOGGER__ERROR(
            "Error: Failed to send command. For more information check Kernel log (dmesg) and DSP firmware log (cat "
            "/dev/dsp_log0)");
        return DSP_RUN_COMMAND_FAILED;
    }

    return DSP_SUCCESS;
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
    kernel_perf_stats_t kernel_perf_stats = {0};
    __u64 tracing_out_data;
    size_t tracing_out_data_size;
    unique_ptr_aligned<perf_info_t> perf_info(nullptr, &free);
    auto &tracer = DspTracer::get_instance();

    /* Always collect perf info when tracing is enabled */
    if (out_data_size == 0) {
        perf_info = make_aligned_uptr<perf_info_t>();
        tracing_out_data = reinterpret_cast<__u64>(perf_info.get());
        tracing_out_data_size = sizeof(perf_info_t);
    } else {
        tracing_out_data = reinterpret_cast<__u64>(out_data);
        tracing_out_data_size = out_data_size;
    }

    struct xrp_ioctl_queue ioctl_queue = {
        .flags = static_cast<__u32>(nsid.has_value() ? XRP_QUEUE_FLAG_NSID : 0),
        .in_data_size = static_cast<__u32>(in_data_size),
        .out_data_size = static_cast<__u32>(tracing_out_data_size),
        .buffer_size = static_cast<__u32>(buffer_list.get_buffers().size() * sizeof(struct xrp_ioctl_buffer)),
        .in_data_addr = reinterpret_cast<__u64>(in_data),
        .out_data_addr = tracing_out_data,
        .buffer_addr = reinterpret_cast<__u64>(buffer_list.get_buffers().data()),
        .nsid_addr = reinterpret_cast<__u64>(nsid.has_value() ? nsid.value().c_str() : nullptr),
        .perf_stats_enabled = true,
        .kernel_perf_stats_addr = reinterpret_cast<__u64>(&kernel_perf_stats),
    };

    tracer.update_event_timestamp_now(timeframe, DSP_USER_SENT_IOCTL_EVENT);

    int ret = ioctl(fd, XRP_IOCTL_QUEUE, &ioctl_queue);
    if (ret < 0) {
        LOGGER__ERROR(
            "Error: Failed to send command. For more information check Kernel log (dmesg) and DSP firmware log (cat "
            "/dev/xvp_log0)");
        return DSP_RUN_COMMAND_FAILED;
    }

    tracer.update_event_timestamp(timeframe, DSP_KERNEL_RECEIVED_IOCTL_EVENT, kernel_perf_stats.kernel_received_ioctl);
    tracer.update_event_timestamp(timeframe, DSP_WAITING_ON_MUTEX_EVENT, kernel_perf_stats.waiting_on_mutex);
    tracer.update_event_timestamp(timeframe, DSP_MUTEX_ACQUIRED_EVENT, kernel_perf_stats.mutex_acquired);
    tracer.update_event_timestamp(timeframe, DSP_IRQ_SENT_EVENT, kernel_perf_stats.irq_sent);
    tracer.update_event_timestamp(timeframe, DSP_FW_FINISHED_EVENT, kernel_perf_stats.fw_finished);
    tracer.update_event_timestamp(timeframe, DSP_MUTEX_RELEASED_EVENT, kernel_perf_stats.mutex_released);

    if (out_data_size == 0) {
        tracer.update_perf_info(timeframe, *((perf_info_t *)tracing_out_data));
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
    struct xrp_ioctl_stats ioctl_stats = {.reset = 1};

    int ret = ioctl(fd, XRP_IOCTL_STATS, &ioctl_stats);
    if (ret < 0) {
        LOGGER__ERROR("Error: Failed to gather kernel statistics, err = {}", ret);
        return DSP_IOCTL_FAILED;
    }

    return DSP_SUCCESS;
}

dsp_status driver_get_kernel_statistics(int fd, kernel_statistics &stats)
{
    struct xrp_ioctl_stats ioctl_stats = {.reset = 0};

    int ret = ioctl(fd, XRP_IOCTL_STATS, &ioctl_stats);
    if (ret < 0) {
        LOGGER__ERROR("Error: Failed to gather kernel statistics, err = {}", ret);
        return DSP_IOCTL_FAILED;
    }

    stats.total_dsp_time = std::chrono::microseconds(ioctl_stats.total_dsp_time_us);
    stats.max_dsp_command_time = std::chrono::microseconds(ioctl_stats.max_dsp_command_time_us);
    stats.total_dsp_commands = ioctl_stats.total_dsp_commands;
    stats.current_threads_using_dsp = ioctl_stats.current_threads_using_dsp;
    stats.max_threads_using_dsp = ioctl_stats.max_threads_using_dsp;
    return DSP_SUCCESS;
}
