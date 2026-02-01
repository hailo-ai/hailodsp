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

#pragma once

#include "buffer_list.hpp"
#include "dsp_tracer.hpp"
#include "hailo/hailodsp.h"
#include "user_dsp_interface.h"

#include <functional>

typedef struct {
    const dsp_image_properties_t *user_api_image;
    image_properties_t *dsp_api_image;
    BufferAccessType access_type;
} command_image_t;

dsp_status send_command(dsp_device device,
                        BufferList &buffer_list,
                        const void *in_data,
                        size_t in_data_size,
                        void *out_data,
                        size_t out_data_size,
                        const char *nsid = IMAGING_NSID);

dsp_status send_command(dsp_device device,
                        std::vector<command_image_t> images,
                        const void *in_data,
                        size_t in_data_size,
                        void *out_data,
                        size_t out_data_size);

dsp_status send_command(dsp_device device,
                        BufferList &buffers,
                        const void *in_data,
                        size_t in_data_size,
                        void *out_data,
                        size_t out_data_size,
                        task_timeframe_t *timeframe);

dsp_status send_command(dsp_device device,
                        std::vector<command_image_t> images,
                        const void *in_data,
                        size_t in_data_size,
                        void *out_data,
                        size_t out_data_size,
                        task_timeframe_t *timeframe);

dsp_status add_images_to_buffer_list(BufferList &buffer_list, std::vector<command_image_t> images);