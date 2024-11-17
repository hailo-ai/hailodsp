#include <CLI/CLI.hpp>
#include <chrono>
#include <cleanup.h>
#include <fmt/core.h>
#include <hailo/hailodsp.h>
#include <iostream>
#include <logger_macros.hpp>
#include <thread>
#include <utilization.hpp>

int main(int argc, char **argv)
{
    uint32_t utilization = 0;
    bool batch_mode = false;
    size_t iteration_count = 0;
    std::chrono::duration<double> delay_time(1);

    CLI::App app("DSP Utilization tool");

    app.add_flag("-b,--batch", batch_mode,
                 "Batch mode - Starts tool in Batch mode, which could be useful for sending output to other programs "
                 "or to a file. In this mode, the tool will run until the iterations limit you've set with the `-i' "
                 "command-line option or until killed.");
    app.add_option("-i,--iterations", iteration_count,
                   "Number of iterations - Specifies the maximum number of iterations the tool should produce before "
                   "ending. The default value is 0, which means iterate forever.")
        ->check(CLI::NonNegativeNumber);
    app.add_option(
           "-d,--delay", delay_time,
           "Delay time - Specifies the delay between screen updates in seconds. Fractional seconds are honored.")
        ->check(CLI::PositiveNumber);

    CLI11_PARSE(app, argc, argv);

    if (batch_mode && iteration_count == 0) {
        iteration_count = 1; // Default value for batch mode
    }

    dsp_device device = nullptr;
    dsp_status status = dsp_create_device(&device);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Open device failed {}\n", status);
        DSP_RELEASE_DEVICE(device);
        return 1;
    }

    if (!batch_mode) {
        std::cout << "Gathering DSP utilization..." << std::flush;
    }

    // Call dsp_get_utilization to reset the counters
    status = dsp_get_utilization(device, utilization);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Get utilization failed {}\n", status);
        DSP_RELEASE_DEVICE(device);
        return 1;
    }

    // Loop until we reach the iteration count or forever if iteration count is 0
    for (size_t i = 0; (i < iteration_count) || (iteration_count == 0); i++) {
        std::this_thread::sleep_for(delay_time);

        status = dsp_get_utilization(device, utilization);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Get utilization failed {}\n", status);
            DSP_RELEASE_DEVICE(device);
            return 1;
        }

        if (batch_mode) {
            fmt::print("{}%\n", utilization);
        } else {
            // cover previous output with spaces to avoid artifacts when printing shorter strings
            std::cout << "\r                                    ";
            fmt::print("\rDSP Utilization: {:3}%", utilization);
            std::cout << std::flush;
        }
    }

    if (!batch_mode) {
        std::cout << std::endl;
    }

    DSP_RELEASE_DEVICE(device);
    return 0;
}