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
#include "image_utils.hpp"
#include "send_command.hpp"

dsp_status add_buffer_to_buffer_group(dsp_device device,
                                      void *buffer,
                                      size_t buffer_size,
                                      enum xrp_access_flags access_flags,
                                      struct xrp_buffer_group *buffer_group,
                                      uint32_t *buffer_index)
{
    enum xrp_status xrp_status;
    dsp_status status = DSP_UNINITIALIZED;

    auto xrp_buffer = xrp_create_buffer(device->xrp_device, buffer_size, buffer, &xrp_status);
    if (xrp_status != XRP_STATUS_SUCCESS) {
        status = DSP_CREATE_BUFFER_FAILED;
        goto l_err;
    }

    *buffer_index = xrp_add_buffer_to_group(buffer_group, xrp_buffer, access_flags, &xrp_status);
    if (xrp_status != XRP_STATUS_SUCCESS) {
        status = DSP_ADD_BUFFER_GROUP_FAILED;
        goto l_err;
    }

    status = DSP_SUCCESS;

l_err:
    (void)xrp_release_buffer(xrp_buffer);

    return status;
}

dsp_status add_image_to_buffer_group(dsp_device device,
                                     const command_image_t *image,
                                     struct xrp_buffer_group *buffer_group)
{
    auto status = convert_image(image->user_api_image, image->dsp_api_image);
    if (status != DSP_SUCCESS) {
        return status;
    }

    for (size_t i = 0; i < image->user_api_image->planes_count; i++) {
        dsp_data_plane_t *plane = &image->user_api_image->planes[i];

        status = add_buffer_to_buffer_group(device, plane->userptr, plane->bytesused, image->access_flags, buffer_group,
                                            &image->dsp_api_image->planes[i].xrp_buffer_index);
        if (status != DSP_SUCCESS) {
            return status;
        }
    }

    return DSP_SUCCESS;
}

dsp_status add_images_to_buffer_group(dsp_device device,
                                      const command_image_t images[],
                                      size_t images_count,
                                      struct xrp_buffer_group *buffer_group)
{
    for (size_t i = 0; i < images_count; ++i) {
        auto status = add_image_to_buffer_group(device, &images[i], buffer_group);
        if (status != DSP_SUCCESS) {
            return status;
        }
    }
    return DSP_SUCCESS;
}

dsp_status send_command(dsp_device device,
                        fill_buffer_group_t fill_buffer_group,
                        const void *in_data,
                        size_t in_data_size,
                        void *out_data,
                        size_t out_data_size)
{
    dsp_status status = DSP_UNINITIALIZED;
    enum xrp_status xrp_status;
    struct xrp_buffer_group *buffer_group = NULL;

    buffer_group = xrp_create_buffer_group(&xrp_status);
    if (xrp_status != XRP_STATUS_SUCCESS) {
        status = DSP_CREATE_BUFFER_GROUP_FAILED;
        goto l_err;
    }

    status = fill_buffer_group(device, buffer_group);
    if (status != DSP_SUCCESS) {
        goto l_err;
    }

    xrp_run_command_ioctl(device->xrp_queue, in_data, in_data_size, out_data, out_data_size, buffer_group, &xrp_status);
    if (xrp_status != XRP_STATUS_SUCCESS) {
        status = DSP_RUN_COMMAND_FAILED;
        goto l_err;
    }

    status = DSP_SUCCESS;

l_err:
    if (buffer_group != NULL) {
        xrp_release_buffer_group(buffer_group);
    }

    return status;
}

dsp_status send_command(dsp_device device,
                        const command_image_t images[],
                        size_t images_count,
                        const void *in_data,
                        size_t in_data_size,
                        void *out_data,
                        size_t out_data_size)
{
    fill_buffer_group_t fill_buffer_group = [&images, images_count](dsp_device device,
                                                                    struct xrp_buffer_group *buffer_group) {
        return add_images_to_buffer_group(device, images, images_count, buffer_group);
    };

    return send_command(device, fill_buffer_group, in_data, in_data_size, out_data, out_data_size);
}
