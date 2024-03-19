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

#include "buffer_list.hpp"
#include "device.hpp"
#include "hailo/hailodsp.h"
#include "hailodsp_driver.hpp"
#include "logger_macros.hpp"
#include "user_dsp_interface.h"
#include "utilization.hpp"

dsp_status dsp_get_utilization(dsp_device device, uint32_t &utilization)
{
    if (!device) {
        LOGGER__ERROR("Error: device is null\n");
        return DSP_INVALID_ARGUMENT;
    }

    utilization_response_t response = {};
    auto status = driver_send_command(device->fd, UTILIZATION_NSID, nullptr, 0, &response, sizeof(response));
    if (status != DSP_SUCCESS) {
        return status;
    }

    utilization = response.utilization;
    return DSP_SUCCESS;
}

dsp_status dsp_reset_kernel_statistics(dsp_device device)
{
    if (!device) {
        LOGGER__ERROR("Error: device is null\n");
        return DSP_INVALID_ARGUMENT;
    }

    return driver_reset_kernel_statistics(device->fd);
}

dsp_status dsp_get_kernel_statistics(dsp_device device, kernel_statistics &stats)
{
    if (!device) {
        LOGGER__ERROR("Error: device is null\n");
        return DSP_INVALID_ARGUMENT;
    }

    return driver_get_kernel_statistics(device->fd, stats);
}