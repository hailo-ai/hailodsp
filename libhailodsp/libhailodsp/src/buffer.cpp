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

#include <utils.h>
#include <xrp_api.h>
#include <xrp_status.h>

typedef struct {
    struct xrp_buffer *metadata;
    // Aligning 'data' to 64 bytes is better for DDR performance
    char dummy[64 - sizeof(struct xrp_buffer *)];
    char data[];
} __attribute__((packed)) dsp_buffer_t;

dsp_status dsp_create_buffer(dsp_device device, size_t size, void **buffer)
{
    dsp_status status = DSP_UNINITIALIZED;
    enum xrp_status xrp_status;
    struct xrp_buffer *xrp_buffer = NULL;
    void *data_buffer = NULL;
    dsp_buffer_t *dsp_buffer;

    if (!device || !size || !buffer) {
        status = DSP_INVALID_ARGUMENT;
        goto l_exit;
    }

    xrp_buffer = xrp_create_buffer(device->xrp_device, sizeof(*dsp_buffer) + size, NULL, &xrp_status);
    if (XRP_FAILURE(xrp_status)) {
        status = DSP_CREATE_BUFFER_FAILED;
        goto l_exit;
    }

    data_buffer = xrp_map_buffer(xrp_buffer, 0, sizeof(*dsp_buffer) + size, XRP_READ_WRITE, &xrp_status);
    if (XRP_FAILURE(xrp_status)) {
        status = DSP_MAP_BUFFER_FAILED;
        goto l_exit;
    }

    dsp_buffer = (dsp_buffer_t *)data_buffer;
    dsp_buffer->metadata = xrp_buffer;
    *buffer = dsp_buffer->data;

    data_buffer = NULL;
    xrp_buffer = NULL;

    status = DSP_SUCCESS;

l_exit:
    if (data_buffer) {
        xrp_unmap_buffer(xrp_buffer, data_buffer, &xrp_status);

        // should only fail if data_buffer not within xrp_buffer->ptr range
        (void)xrp_status;
    }
    if (xrp_buffer) {
        xrp_release_buffer(xrp_buffer);
    }

    return status;
}

dsp_status dsp_release_buffer(dsp_device device, void *buffer)
{
    dsp_status status = DSP_UNINITIALIZED;
    enum xrp_status xrp_status;

    if (!device || !buffer) {
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    dsp_buffer_t *dsp_buffer = (dsp_buffer_t *)((uint8_t *)buffer - sizeof(*dsp_buffer));
    struct xrp_buffer *xrp_buffer = dsp_buffer->metadata;

    xrp_unmap_buffer(xrp_buffer, buffer, &xrp_status);
    if (XRP_FAILURE(xrp_status)) {
        status = DSP_UNMAP_BUFFER_FAILED;
        return status;
    }

    xrp_release_buffer(xrp_buffer);

    status = DSP_SUCCESS;

    return status;
}