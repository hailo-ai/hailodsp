/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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
#include "device.hpp"
#include "hailo/hailodsp.h"
#include "hailodsp_driver.hpp"
#include "logger_macros.hpp"
#include "user_dsp_interface.h"

#include <stdio.h>

dsp_status dsp_create_device(dsp_device *device)
{
    dsp_status status = DSP_UNINITIALIZED;

    dsp_device local_device = NULL;
    int fd = -1;

    if (device == NULL) {
        status = DSP_INVALID_ARGUMENT;
        goto l_exit;
    }

    local_device = new (std::nothrow) _dsp_device;
    if (!local_device) {
        LOGGER__ERROR("Failed to allocate memory for device");
        status = DSP_OUT_OF_HOST_MEMORY;
        goto l_exit;
    }

    status = driver_open_device(fd);
    if (status != DSP_SUCCESS) {
        goto l_exit;
    }

    local_device->fd = fd;
    *device = local_device;

    local_device = NULL;

    status = DSP_SUCCESS;

l_exit:
    if (local_device != NULL) {
        delete local_device;
    }
    return status;
}

dsp_status dsp_release_device(dsp_device device)
{
    dsp_status status = DSP_UNINITIALIZED;

    if (device == NULL) {
        status = DSP_INVALID_ARGUMENT;
        goto l_exit;
    }

    status = driver_close_device(device->fd);
    if (status != DSP_SUCCESS) {
        goto l_exit;
    }

    delete device;
    status = DSP_SUCCESS;

l_exit:
    return status;
}
