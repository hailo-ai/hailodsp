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
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "send_command.hpp"

dsp_status add_images_to_buffer_list(BufferList &buffer_list, std::vector<command_image_t> images)
{
    for (auto &image : images) {
        auto status = convert_image(image.user_api_image, image.dsp_api_image);
        if (status != DSP_SUCCESS) {
            return status;
        }

        for (size_t j = 0; j < image.dsp_api_image->planes_count; j++) {
            image.dsp_api_image->planes[j].xrp_buffer_index =
                buffer_list.add_plane(image.user_api_image->planes[j], image.access_type, image.user_api_image->memory);
        }
    }

    return DSP_SUCCESS;
}

dsp_status send_command(dsp_device device,
                        BufferList &buffer_list,
                        const void *in_data,
                        size_t in_data_size,
                        void *out_data,
                        size_t out_data_size)
{
    return driver_send_command(device->fd, IMAGING_NSID, buffer_list, in_data, in_data_size, out_data, out_data_size);
}

dsp_status send_command(dsp_device device,
                        std::vector<command_image_t> images,
                        const void *in_data,
                        size_t in_data_size,
                        void *out_data,
                        size_t out_data_size)
{
    BufferList buffer_list;
    auto status = add_images_to_buffer_list(buffer_list, images);
    if (status != DSP_SUCCESS) {
        return status;
    }

    return send_command(device, buffer_list, in_data, in_data_size, out_data, out_data_size);
}
