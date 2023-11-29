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

#include "aligned_uptr.hpp"
#include "blur_perf.h"
#include "hailo/hailodsp.h"
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "send_command.hpp"
#include "user_dsp_interface.h"

#include <cstdint>
#include <cstdio>
#include <utils.h>

#define KERNEL_MAX_SIZE (33)

// This function assumes that "image" params is already checked for correctness
static dsp_status verify_roi_params(const dsp_image_properties_t *image, const dsp_blur_roi_t *roi_params)
{
    if (!image || !roi_params) {
        LOGGER__ERROR("Error: NULL argument (image={}, roi_params={})\n", fmt::ptr(image), fmt::ptr(roi_params));
        return DSP_INVALID_ARGUMENT;
    }

    if (roi_params->start_x >= roi_params->end_x) {
        LOGGER__ERROR("Error: ROI start_x ({}) must be smaller then end_x ({})\n", roi_params->start_x,
                      roi_params->end_x);
        return DSP_INVALID_ARGUMENT;
    }

    if (roi_params->start_y >= roi_params->end_y) {
        LOGGER__ERROR("Error: ROI start_y ({}) must be smaller then end_y ({})\n", roi_params->start_y,
                      roi_params->end_y);
        return DSP_INVALID_ARGUMENT;
    }

    if (roi_params->end_x > image->width) {
        LOGGER__ERROR("Error: ROI end_x ({}) must be smaller or equal to image width ({})\n", roi_params->end_x,
                      image->width);
        return DSP_INVALID_ARGUMENT;
    }

    if (roi_params->end_y > image->height) {
        LOGGER__ERROR("Error: ROI end_y ({}) must be smaller or equal to image height ({})\n", roi_params->end_y,
                      image->height);
        return DSP_INVALID_ARGUMENT;
    }

    return DSP_SUCCESS;
}

dsp_status dsp_blur_perf(dsp_device device,
                         dsp_image_properties_t *image,
                         const dsp_blur_roi_t rois[],
                         size_t rois_count,
                         uint32_t kernel_size,
                         perf_info_t *perf_info)
{
    if ((!device) || (!image) || (!rois)) {
        LOGGER__ERROR("Error: One of the parameters provided is NULL (device={}, image={}, rois={})\n",
                      fmt::ptr(device), fmt::ptr(image), fmt::ptr(rois));
        return DSP_INVALID_ARGUMENT;
    }

    if (kernel_size % 2 == 0) {
        LOGGER__ERROR("Error: Kernel size should be odd\n");
        return DSP_INVALID_ARGUMENT;
    }

    if (kernel_size > KERNEL_MAX_SIZE) {
        LOGGER__ERROR("Error: Kernel size cannot exceed {}\n", KERNEL_MAX_SIZE);
        return DSP_INVALID_ARGUMENT;
    }

    if (rois_count > MAX_BLUR_ROIS) {
        LOGGER__ERROR("Error: Too many ROIs. The operation supports up to {} ROIs\n", MAX_BLUR_ROIS);
        return DSP_INVALID_ARGUMENT;
    }

    auto status = verify_image_properties(image);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"image\"\n");
        return status;
    }

    switch (image->format) {
        case DSP_IMAGE_FORMAT_GRAY8:
        case DSP_IMAGE_FORMAT_NV12:
            break;

        default:
            LOGGER__ERROR("Error: Image format ({}) is not supported\n", format_arg_to_string(image->format));
            return DSP_INVALID_ARGUMENT;
    }

    auto in_data = make_aligned_uptr<imaging_request_t>();
    in_data->operation = IMAGING_OP_BLUR;
    in_data->blur_args.rois_count = rois_count;
    in_data->blur_args.kernel_size = kernel_size;

    for (size_t i = 0; i < rois_count; ++i) {
        status = verify_roi_params(image, &rois[i]);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: ROI properties check failed for \"roi[{}]\"\n", i);
            return status;
        }

        in_data->blur_args.rois[i].start_x = rois[i].start_x;
        in_data->blur_args.rois[i].start_y = rois[i].start_y;
        in_data->blur_args.rois[i].end_x = rois[i].end_x;
        in_data->blur_args.rois[i].end_y = rois[i].end_y;
    }

    command_image_t command_image = {
        .user_api_image = image,
        .dsp_api_image = &in_data->blur_args.image,
        .access_flags = XRP_READ_WRITE,
    };

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status =
        send_command(device, &command_image, 1, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing blur operation. Error code: {}\n", status);
    }

    return status;
}

dsp_status dsp_blur(dsp_device device,
                    dsp_image_properties_t *image,
                    const dsp_blur_roi_t rois[],
                    size_t rois_count,
                    uint32_t kernel_size)
{
    return dsp_blur_perf(device, image, rois, rois_count, kernel_size, NULL);
}
