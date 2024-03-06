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
#include "hailo/hailodsp.h"
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "resize_perf.h"
#include "send_command.hpp"
#include "user_dsp_interface.h"

#include <cstdio>
#include <memory>
#include <utils.h>

// This function assumes that "image" params is already checked for correctness
static dsp_status verify_crop_params(const dsp_image_properties_t *image, const dsp_roi_t *crop_params)
{
    if (!image || !crop_params) {
        LOGGER__ERROR("Error: NULL argument (image={}, crop_params={})\n", fmt::ptr(image), fmt::ptr(crop_params));
        return DSP_INVALID_ARGUMENT;
    }

    if (crop_params->start_x >= crop_params->end_x) {
        LOGGER__ERROR("Error: Crop start_x ({}) must be smaller then end_x ({})\n", crop_params->start_x,
                      crop_params->end_x);
        return DSP_INVALID_ARGUMENT;
    }

    if (crop_params->start_y >= crop_params->end_y) {
        LOGGER__ERROR("Error: Crop start_y ({}) must be smaller then end_y ({})\n", crop_params->start_y,
                      crop_params->end_y);
        return DSP_INVALID_ARGUMENT;
    }

    if (crop_params->end_x > image->width) {
        LOGGER__ERROR("Error: Crop end_x ({}) must be smaller or equal to image width ({})\n", crop_params->end_x,
                      image->width);
        return DSP_INVALID_ARGUMENT;
    }

    if (crop_params->end_y > image->height) {
        LOGGER__ERROR("Error: Crop end_y ({}) must be smaller or equal to image height ({})\n", crop_params->end_y,
                      image->height);
        return DSP_INVALID_ARGUMENT;
    }

    return DSP_SUCCESS;
}

// This function assumes that "image" params is already checked for correctness
static dsp_status verify_privacy_mask_params(const dsp_image_properties_t *image,
                                             const dsp_privacy_mask_t *privacy_mask_params)
{
    // privacy mask is optional
    if (!privacy_mask_params) {
        return DSP_SUCCESS;
    }

    if (!image || !privacy_mask_params->bitmask) {
        LOGGER__ERROR("Error: NULL argument (image={}, privacy_mask_params->bitmask={})\n", fmt::ptr(image),
                      fmt::ptr(privacy_mask_params->bitmask));
        return DSP_INVALID_ARGUMENT;
    }

    if (privacy_mask_params->rois_count == 0) {
        LOGGER__ERROR("Error: Must have at least 1 ROI\n");
        return DSP_INVALID_ARGUMENT;
    }

    if (privacy_mask_params->rois_count > MAX_PRIVACY_MASK_ROIS) {
        LOGGER__ERROR("Error: Too many ROIs. The operation supports up to {} ROIs\n", MAX_PRIVACY_MASK_ROIS);
        return DSP_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < privacy_mask_params->rois_count; ++i) {
        dsp_roi_t *roi_params = &privacy_mask_params->rois[i];
        bool invalid_roi = false;

        if (roi_params->start_x >= roi_params->end_x) {
            LOGGER__ERROR("Error: ROI start_x ({}) must be smaller then end_x ({})\n", roi_params->start_x,
                          roi_params->end_x);
            invalid_roi = true;
        }

        if (roi_params->start_y >= roi_params->end_y) {
            LOGGER__ERROR("Error: ROI start_y ({}) must be smaller then end_y ({})\n", roi_params->start_y,
                          roi_params->end_y);
            invalid_roi = true;
        }

        const size_t bitmask_width = ceil(image->width / (float)PRIVACY_MASK_QUANTIZATION);
        if (roi_params->end_x > bitmask_width) {
            LOGGER__ERROR("Error: ROI end_x ({}) must be smaller or equal to quantized bitmask width ({})\n",
                          roi_params->end_x, bitmask_width);
            invalid_roi = true;
        }

        const size_t bitmask_height = ceil(image->height / (float)PRIVACY_MASK_QUANTIZATION);
        if (roi_params->end_y > bitmask_height) {
            LOGGER__ERROR("Error: ROI end_y ({}) must be smaller or equal to quantized bitmask height ({})\n",
                          roi_params->end_y, bitmask_height);
            invalid_roi = true;
        }

        if (invalid_roi) {
            LOGGER__ERROR("Error: ROI properties check failed for \"roi[{}]\"\n", i);
            return DSP_INVALID_ARGUMENT;
        }
    }

    return DSP_SUCCESS;
}

dsp_status dsp_crop_and_resize_perf(dsp_device device,
                                    const dsp_resize_params_t *resize_params,
                                    const dsp_roi_t *crop_params,
                                    perf_info_t *perf_info)
{
    if ((!device) || (!resize_params)) {
        LOGGER__ERROR("Error: NULL argument (device={}, resize_params={})\n", fmt::ptr(device),
                      fmt::ptr(resize_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto status = verify_crop_params(resize_params->src, crop_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Crop parameters check failed\n");
        return status;
    }

    dsp_image_properties_t cropped_src = *resize_params->src;
    cropped_src.width = crop_params->end_x - crop_params->start_x;
    cropped_src.height = crop_params->end_y - crop_params->start_y;

    status = verify_image_properties(&cropped_src);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"src\" (after crop)\n");
        return status;
    }

    status = verify_image_properties(resize_params->dst);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"dst\"\n");
        return status;
    }

    if (resize_params->interpolation >= INTERPOLATION_TYPE_COUNT) {
        LOGGER__ERROR("Error: Unknown interpolation type {}\n", resize_params->interpolation);
        return DSP_INVALID_ARGUMENT;
    }

    // Only same format for src and dst is currently supported and only GRAY8, RGB or NV12
    if (resize_params->src->format != resize_params->dst->format) {
        LOGGER__ERROR("Error: The src and dst formats are not the same\n");
        return DSP_INVALID_ARGUMENT;
    }

    switch (resize_params->src->format) {
        case DSP_IMAGE_FORMAT_GRAY8:
        case DSP_IMAGE_FORMAT_RGB:
        case DSP_IMAGE_FORMAT_NV12:
            break;

        default:
            LOGGER__ERROR("Error: The src/dst format ({}) is not supported\n",
                          format_arg_to_string(resize_params->src->format));
            return DSP_INVALID_ARGUMENT;
    }

    if ((resize_params->interpolation == INTERPOLATION_TYPE_AREA) &&
        ((cropped_src.width < resize_params->dst->width) || (cropped_src.height < resize_params->dst->height))) {
        LOGGER__ERROR("Error: Area interpolation does not support upscaling\n");
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = make_aligned_uptr<imaging_request_t>();
    in_data->operation = IMAGING_OP_CROP_AND_RESIZE;
    in_data->crop_and_resize_args.interpolation = resize_params->interpolation;
    in_data->crop_and_resize_args.crop_start_x = crop_params->start_x;
    in_data->crop_and_resize_args.crop_start_y = crop_params->start_y;
    in_data->crop_and_resize_args.crop_end_x = crop_params->end_x;
    in_data->crop_and_resize_args.crop_end_y = crop_params->end_y;

    std::vector<command_image_t> images = {
        {
            .user_api_image = resize_params->src,
            .dsp_api_image = &in_data->crop_and_resize_args.src,
            .access_type = BufferAccessType::Read,
        },
        {
            .user_api_image = resize_params->dst,
            .dsp_api_image = &in_data->crop_and_resize_args.dst,
            .access_type = BufferAccessType::Write,
        },
    };

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status = send_command(device, images, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}\n", status);
    }

    return status;
}

dsp_status dsp_resize_perf(dsp_device device, const dsp_resize_params_t *resize_params, perf_info_t *perf_info)
{
    if (!resize_params) {
        LOGGER__ERROR("Error: resize_params is NULL\n");
        return DSP_INVALID_ARGUMENT;
    }

    if (!resize_params->src) {
        LOGGER__ERROR("Error: resize_params->src is NULL\n");
        return DSP_INVALID_ARGUMENT;
    }

    dsp_roi_t crop_params = {
        .start_x = 0,
        .start_y = 0,
        .end_x = resize_params->src->width,
        .end_y = resize_params->src->height,
    };

    return dsp_crop_and_resize_perf(device, resize_params, &crop_params, perf_info);
}

dsp_status dsp_crop_and_resize(dsp_device device,
                               const dsp_resize_params_t *resize_params,
                               const dsp_roi_t *crop_params)
{
    return dsp_crop_and_resize_perf(device, resize_params, crop_params, NULL);
}

dsp_status dsp_resize(dsp_device device, const dsp_resize_params_t *resize_params)
{
    return dsp_resize_perf(device, resize_params, NULL);
}

dsp_status dsp_multi_crop_and_resize_perf(dsp_device device,
                                          const dsp_multi_resize_params_t *resize_params,
                                          const dsp_roi_t *crop_params,
                                          const dsp_privacy_mask_t *privacy_mask_params,
                                          perf_info_t *perf_info)
{
    if ((!device) || (!resize_params)) {
        LOGGER__ERROR("Error: NULL argument (device={}, resize_params={})\n", fmt::ptr(device),
                      fmt::ptr(resize_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto status = verify_crop_params(resize_params->src, crop_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Crop parameters check failed\n");
        return status;
    }

    status = verify_privacy_mask_params(resize_params->src, privacy_mask_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Privacy mask parameters check failed\n");
        return status;
    }

    dsp_image_properties_t cropped_src = *resize_params->src;
    cropped_src.width = crop_params->end_x - crop_params->start_x;
    cropped_src.height = crop_params->end_y - crop_params->start_y;

    status = verify_image_properties(&cropped_src);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"src\" (after crop)\n");
        return status;
    }

    if (resize_params->src->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Src format ({}) is not supported\n", format_arg_to_string(resize_params->src->format));
        return DSP_INVALID_ARGUMENT;
    }

    for (int i = 0; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; ++i) {
        auto dst_image = resize_params->dst[i];
        if (dst_image == NULL)
            continue;

        status = verify_image_properties(dst_image);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for \"dst[{}]\"\n", i);
            return status;
        }

        if (dst_image->format != DSP_IMAGE_FORMAT_NV12) {
            LOGGER__ERROR("Error: Dst[{}] format ({}) is not supported\n", i, format_arg_to_string(dst_image->format));
            return DSP_INVALID_ARGUMENT;
        }
    }

    if ((resize_params->interpolation != INTERPOLATION_TYPE_BILINEAR) &&
        (resize_params->interpolation != INTERPOLATION_TYPE_BICUBIC)) {
        LOGGER__ERROR("Error: Interpolation type ({}) not supported\n", resize_params->interpolation);
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = make_aligned_uptr<imaging_request_t>();
    in_data->operation =
        privacy_mask_params ? IMAGING_OP_MULTI_CROP_AND_RESIZE_PRIVACY_MASK : IMAGING_OP_MULTI_CROP_AND_RESIZE;
    in_data->multi_crop_and_resize_args.interpolation = resize_params->interpolation;
    in_data->multi_crop_and_resize_args.crop_start_x = crop_params->start_x;
    in_data->multi_crop_and_resize_args.crop_start_y = crop_params->start_y;
    in_data->multi_crop_and_resize_args.crop_end_x = crop_params->end_x;
    in_data->multi_crop_and_resize_args.crop_end_y = crop_params->end_y;

    std::vector<command_image_t> images;
    images.reserve(1 + DSP_MULTI_RESIZE_OUTPUTS_COUNT);

    images.emplace_back(command_image_t{
        .user_api_image = resize_params->src,
        .dsp_api_image = &in_data->multi_crop_and_resize_args.src,
        .access_type = BufferAccessType::Read,
    });

    for (auto dst_image : resize_params->dst) {
        if (dst_image == NULL)
            continue;
        images.emplace_back(command_image_t{
            .user_api_image = dst_image,
            .dsp_api_image = &in_data->multi_crop_and_resize_args.dst[images.size() - 1],
            .access_type = BufferAccessType::Write,
        });
    }

    in_data->multi_crop_and_resize_args.dst_count = images.size() - 1;

    BufferList buffer_list;
    status = add_images_to_buffer_list(buffer_list, images);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed adding images to buffer list. Error code: {}\n", status);
        return status;
    }

    if (privacy_mask_params) {
        const size_t bitmask_width = ceil(resize_params->src->width / (double)(PRIVACY_MASK_QUANTIZATION * 8));
        const size_t bitmask_stride = ROUND_UP(bitmask_width, 8);
        const size_t bitmask_height = ceil(resize_params->src->height / (double)PRIVACY_MASK_QUANTIZATION);
        in_data->multi_crop_and_resize_args.privacy_mask.bitmask.line_stride = bitmask_stride;
        in_data->multi_crop_and_resize_args.privacy_mask.bitmask.plane_size = bitmask_stride * bitmask_height;
        in_data->multi_crop_and_resize_args.privacy_mask.bitmask.xrp_buffer_index = buffer_list.add_buffer(
            privacy_mask_params->bitmask, bitmask_stride * bitmask_height, BufferAccessType::Read);

        in_data->multi_crop_and_resize_args.privacy_mask.y_color = privacy_mask_params->y_color;
        in_data->multi_crop_and_resize_args.privacy_mask.u_color = privacy_mask_params->u_color;
        in_data->multi_crop_and_resize_args.privacy_mask.v_color = privacy_mask_params->v_color;

        in_data->multi_crop_and_resize_args.privacy_mask.rois_count = privacy_mask_params->rois_count;
        for (size_t i = 0; i < privacy_mask_params->rois_count; ++i) {
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].start_x = privacy_mask_params->rois[i].start_x;
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].start_y = privacy_mask_params->rois[i].start_y;
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].end_x = privacy_mask_params->rois[i].end_x;
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].end_y = privacy_mask_params->rois[i].end_y;
        }
    }

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status = send_command(device, buffer_list, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}\n", status);
    }

    return status;
}

dsp_status dsp_multi_crop_and_resize(dsp_device device,
                                     const dsp_multi_resize_params_t *resize_params,
                                     const dsp_roi_t *crop_params)
{
    return dsp_multi_crop_and_resize_perf(device, resize_params, crop_params, NULL, NULL);
}

dsp_status dsp_multi_crop_and_resize_privacy_mask(dsp_device device,
                                                  const dsp_multi_resize_params_t *resize_params,
                                                  const dsp_roi_t *crop_params,
                                                  const dsp_privacy_mask_t *privacy_mask_params)
{
    return dsp_multi_crop_and_resize_perf(device, resize_params, crop_params, privacy_mask_params, NULL);
}