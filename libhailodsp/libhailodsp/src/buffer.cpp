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

#include <utils.h>

typedef struct {
    dsp_device device;
    size_t allocated_size;
} buffer_metadata_t;

typedef struct {
    buffer_metadata_t metadata;
    // Aligning 'data' to 64 bytes is better for DDR performance
    char dummy[64 - sizeof(buffer_metadata_t)];
    char data[];
} __attribute__((packed)) dsp_buffer_t;

static dsp_buffer_t *dsp_buffer_from_ptr(void *buffer)
{
    return (dsp_buffer_t *)((uint8_t *)buffer - sizeof(dsp_buffer_t));
}

dsp_status dsp_create_buffer(dsp_device device, size_t size, void **buffer)
{
    dsp_buffer_t *dsp_buffer;

    if (!device || !size || !buffer) {
        return DSP_INVALID_ARGUMENT;
    }

    size_t allocated_size = sizeof(*dsp_buffer) + size;

    auto status = driver_allocate_buffer(device->fd, allocated_size, (void **)&dsp_buffer);
    if (status != DSP_SUCCESS) {
        return status;
    }

    dsp_buffer->metadata.device = device;
    dsp_buffer->metadata.allocated_size = allocated_size;
    *buffer = dsp_buffer->data;

    return DSP_SUCCESS;
}

dsp_status dsp_release_buffer(dsp_device device, void *buffer)
{
    if (!device || !buffer) {
        return DSP_INVALID_ARGUMENT;
    }

    dsp_buffer_t *dsp_buffer = dsp_buffer_from_ptr(buffer);
    return driver_release_buffer(device->fd, dsp_buffer, dsp_buffer->metadata.allocated_size);
}

dsp_status dsp_buffer_sync_start(void *buffer, dsp_sync_direction_t direction)
{
    if (!buffer) {
        return DSP_INVALID_ARGUMENT;
    }

    auto dsp_buffer = dsp_buffer_from_ptr(buffer);
    return driver_sync_buffer_start(dsp_buffer->metadata.device->fd, buffer, dsp_buffer->metadata.allocated_size,
                                    direction);
}

dsp_status dsp_buffer_sync_end(void *buffer, dsp_sync_direction_t direction)
{
    if (!buffer) {
        return DSP_INVALID_ARGUMENT;
    }

    auto dsp_buffer = dsp_buffer_from_ptr(buffer);
    return driver_sync_buffer_end(dsp_buffer->metadata.device->fd, buffer, dsp_buffer->metadata.allocated_size,
                                  direction);
}
