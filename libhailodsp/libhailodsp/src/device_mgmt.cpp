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
#include "user_dsp_interface.h"

#include <xrp_api.h>

dsp_status dsp_create_device(dsp_device *device)
{
    dsp_status status = DSP_UNINITIALIZED;
    enum xrp_status xrp_status;

    dsp_device local_device = NULL;
    struct xrp_device *xrp_device = NULL;
    struct xrp_queue *xrp_queue = NULL;

    if (device == NULL) {
        status = DSP_INVALID_ARGUMENT;
        goto l_exit;
    }

    local_device = (dsp_device)malloc(sizeof(*local_device));
    if (!local_device) {
        status = DSP_OUT_OF_HOST_MEMORY;
        goto l_exit;
    }

    xrp_device = xrp_open_device(0, &xrp_status);
    if (xrp_status != XRP_STATUS_SUCCESS) {
        status = DSP_OPEN_DEVICE_FAILED;
        goto l_exit;
    }

    xrp_queue = xrp_create_ns_queue(xrp_device, IMAGING_NSID, &xrp_status);
    if (xrp_status != XRP_STATUS_SUCCESS) {
        status = DSP_CREATE_QUEUE_FAILED;
        goto l_exit;
    }

    local_device->xrp_device = xrp_device;
    local_device->xrp_queue = xrp_queue;
    *device = local_device;

    xrp_device = NULL;
    xrp_queue = NULL;
    local_device = NULL;

    status = DSP_SUCCESS;
    goto l_exit;

l_exit:
    if (xrp_queue != NULL) {
        xrp_release_queue(xrp_queue);
    }
    if (xrp_device != NULL) {
        xrp_release_device(xrp_device);
    }
    if (local_device != NULL) {
        free(local_device);
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

    if (device->xrp_queue != NULL) {
        xrp_release_queue(device->xrp_queue);
    }

    if (device->xrp_device != NULL) {
        xrp_release_device(device->xrp_device);
    }

    free(device);
    status = DSP_SUCCESS;

l_exit:
    return status;
}
