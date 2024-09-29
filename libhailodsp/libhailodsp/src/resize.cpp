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
#include "dsp_tracer.hpp"
#include "hailo/hailodsp.h"
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "resize_perf.h"
#include "send_command.hpp"
#include "thread_local_buffer.hpp"
#include "user_dsp_interface.h"

#include <utils.h>

#define RESOLUTION_WIDTH_HD (1280)
#define RESOLUTION_HEIGHT_HD (720)

#define CROP_RESIZE_MAX_SCALE (16)
#define MULTI_CROP_RESIZE_MAX_SCALE (32)

static_assert((int)DSP_NO_LETTERBOX == (int)INTERFACE_NO_LETTERBOX,
              "Interface and internal implementation must have the same number for no letterbox");
static_assert((int)DSP_LETTERBOX_MIDDLE == (int)INTERFACE_LETTERBOX_MIDDLE,
              "Interface and internal implementation must have the same number for letterbox middle");
static_assert((int)DSP_LETTERBOX_UP_LEFT == (int)INTERFACE_LETTERBOX_UP_LEFT,
              "Interface and internal implementation must have the same number for letterbox up_left");

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

static dsp_status verify_resize_scaling(size_t src_width,
                                        size_t src_height,
                                        size_t dst_width,
                                        size_t dst_height,
                                        size_t max_scale)
{
    if (dst_width * max_scale < src_width) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst width({}) can be at most {} "
            "times smaller than src width({})\n",
            dst_width, max_scale, src_width);
        return DSP_INVALID_ARGUMENT;
    }

    if (dst_height * max_scale < src_height) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst height({}) can be at most {} "
            "times smaller than src height({})\n",
            dst_height, max_scale, src_height);
        return DSP_INVALID_ARGUMENT;
    }

    if (dst_width > src_width * max_scale) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst width({}) can be at most {} "
            "times larger than src width({})\n",
            dst_width, max_scale, src_width);
        return DSP_INVALID_ARGUMENT;
    }

    if (dst_height > src_height * max_scale) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst height({}) can be at most {} "
            "times larger than src height({})\n",
            dst_height, max_scale, src_height);
        return DSP_INVALID_ARGUMENT;
    }

    return DSP_SUCCESS;
}

dsp_status dsp_crop_and_resize_perf(dsp_device device,
                                    const dsp_resize_params_t *resize_params,
                                    const dsp_roi_t *crop_params,
                                    const dsp_letterbox_properties_t *letterbox_params,
                                    perf_info_t *perf_info)
{
    if ((!device) || (!resize_params)) {
        LOGGER__ERROR("Error: NULL argument (device={}, resize_params={})\n", fmt::ptr(device),
                      fmt::ptr(resize_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(IMAGING_OP_CROP_AND_RESIZE);
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

    // Skip scaling check for FHD to 64x64 in Bilinear or Nearest, as it is a special case that is allowed
    bool skip_scaling_check = ((cropped_src.width == 1920) && (cropped_src.height == 1080) &&
                               (resize_params->dst->width == 64) && (resize_params->dst->height == 64) &&
                               ((resize_params->interpolation == INTERPOLATION_TYPE_NEAREST_NEIGHBOR) ||
                                (resize_params->interpolation == INTERPOLATION_TYPE_BILINEAR)));
    if (!skip_scaling_check) {
        status = verify_resize_scaling(cropped_src.width, cropped_src.height, resize_params->dst->width,
                                       resize_params->dst->height, CROP_RESIZE_MAX_SCALE);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image resize scale check failed\n");
            return status;
        }
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

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }

    in_data->operation = IMAGING_OP_CROP_AND_RESIZE;
    in_data->crop_and_resize_args.interpolation = resize_params->interpolation;
    in_data->crop_and_resize_args.crop.start_x = crop_params->start_x;
    in_data->crop_and_resize_args.crop.start_y = crop_params->start_y;
    in_data->crop_and_resize_args.crop.end_x = crop_params->end_x;
    in_data->crop_and_resize_args.crop.end_y = crop_params->end_y;
    in_data->crop_and_resize_args.letterbox.alignment = INTERFACE_NO_LETTERBOX;
    if ((letterbox_params) && (letterbox_params->alignment != DSP_NO_LETTERBOX)) {
        in_data->crop_and_resize_args.letterbox.alignment =
            (interface_letterbox_alignment_t)letterbox_params->alignment;
        memcpy(&in_data->crop_and_resize_args.letterbox.color, &letterbox_params->color, sizeof(color_t));
    }

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
    status =
        send_command(device, images, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size, timeframe);
    tracer.update_event_timestamp_now(timeframe, DSP_TASK_FINISHED_EVENT);

    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}\n", status);
    }

    return status;
}

dsp_status dsp_resize_perf(dsp_device device,
                           const dsp_resize_params_t *resize_params,
                           const dsp_letterbox_properties_t *letterbox_params,
                           perf_info_t *perf_info)
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

    return dsp_crop_and_resize_perf(device, resize_params, &crop_params, letterbox_params, perf_info);
}

dsp_status dsp_crop_and_resize(dsp_device device,
                               const dsp_resize_params_t *resize_params,
                               const dsp_roi_t *crop_params)
{
    return dsp_crop_and_resize_perf(device, resize_params, crop_params, NULL, NULL);
}

dsp_status dsp_crop_and_resize_letterbox(dsp_device device,
                                         const dsp_resize_params_t *resize_params,
                                         const dsp_roi_t *crop_params,
                                         const dsp_letterbox_properties_t *letterbox_params)
{
    if (!crop_params) {
        return dsp_resize_perf(device, resize_params, letterbox_params, NULL);
    } else {
        return dsp_crop_and_resize_perf(device, resize_params, crop_params, letterbox_params, NULL);
    }
}

dsp_status dsp_resize(dsp_device device, const dsp_resize_params_t *resize_params)
{
    return dsp_resize_perf(device, resize_params, NULL, NULL);
}

dsp_status dsp_multi_crop_and_resize_perf(dsp_device device,
                                          const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
                                          const dsp_privacy_mask_t *privacy_mask_params,
                                          perf_info_t *perf_info)
{
    if ((!device) || (!mutli_crop_resize_params)) {
        LOGGER__ERROR("Error: NULL argument (device={}, mutli_crop_resize_params={})\n", fmt::ptr(device),
                      fmt::ptr(mutli_crop_resize_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(
        privacy_mask_params ? IMAGING_OP_MULTI_CROP_AND_RESIZE_PRIVACY_MASK : IMAGING_OP_MULTI_CROP_AND_RESIZE);

    auto status = verify_privacy_mask_params(mutli_crop_resize_params->src, privacy_mask_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Privacy mask parameters check failed\n");
        return status;
    }

    if (mutli_crop_resize_params->src->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Src format ({}) is not supported\n",
                      format_arg_to_string(mutli_crop_resize_params->src->format));
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }

    in_data->multi_crop_and_resize_args.multi_resizes_count = mutli_crop_resize_params->crop_resize_params_count;
    in_data->operation =
        privacy_mask_params ? IMAGING_OP_MULTI_CROP_AND_RESIZE_PRIVACY_MASK : IMAGING_OP_MULTI_CROP_AND_RESIZE;

    if ((mutli_crop_resize_params->interpolation != INTERPOLATION_TYPE_BILINEAR) &&
        (mutli_crop_resize_params->interpolation != INTERPOLATION_TYPE_BICUBIC)) {
        LOGGER__ERROR("Error: interpolation type ({}) not supported\n", mutli_crop_resize_params->interpolation);
        return DSP_INVALID_ARGUMENT;
    }

    in_data->multi_crop_and_resize_args.interpolation = mutli_crop_resize_params->interpolation;

    std::vector<command_image_t> images;
    images.reserve(1 + MAX_MULTI_RESIZE_OUTPUTS * mutli_crop_resize_params->crop_resize_params_count);
    images.emplace_back(command_image_t{
        .user_api_image = mutli_crop_resize_params->src,
        .dsp_api_image = &in_data->multi_crop_and_resize_args.src,
        .access_type = BufferAccessType::Read,
    });

    for (size_t j = 0; j < mutli_crop_resize_params->crop_resize_params_count; j++) {
        auto crop_and_resize_params = &mutli_crop_resize_params->crop_resize_params[j];
        auto crop_and_resize_args = &in_data->multi_crop_and_resize_args.multi_resizes[j];

        auto status = verify_crop_params(mutli_crop_resize_params->src, crop_and_resize_params->crop);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Crop parameters check failed for crop&resize[{}]\n", j);
            return status;
        }

        dsp_image_properties_t cropped_src = *mutli_crop_resize_params->src;
        cropped_src.width = crop_and_resize_params->crop->end_x - crop_and_resize_params->crop->start_x;
        cropped_src.height = crop_and_resize_params->crop->end_y - crop_and_resize_params->crop->start_y;

        status = verify_image_properties(&cropped_src);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for src (after crop)\n");
            return status;
        }

        size_t dst_count = 0;
        for (int i = 0; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; ++i) {
            auto dst_image = crop_and_resize_params->dst[i];
            if (dst_image == NULL) {
                continue;
            }

            status = verify_image_properties(dst_image);
            if (status != DSP_SUCCESS) {
                LOGGER__ERROR("Error: Image properties check failed for dst[{}, {}]\n", j, i);
                return status;
            }

            if (dst_image->format != DSP_IMAGE_FORMAT_NV12) {
                LOGGER__ERROR("Error: Dst[{} {}] format ({}) is not supported\n", j, i,
                              format_arg_to_string(dst_image->format));
                return DSP_INVALID_ARGUMENT;
            }

            status = verify_resize_scaling(cropped_src.width, cropped_src.height, dst_image->width, dst_image->height,
                                           MULTI_CROP_RESIZE_MAX_SCALE);
            if (status != DSP_SUCCESS) {
                LOGGER__ERROR("Error: Image resize scale check failed for dst[{}, {}]\n", j, i);
                return status;
            }

            images.emplace_back(command_image_t{
                .user_api_image = dst_image,
                .dsp_api_image = &crop_and_resize_args->dst[dst_count],
                .access_type = BufferAccessType::Write,
            });

            dst_count++;
        }

        crop_and_resize_args->dst_count = dst_count;
        crop_and_resize_args->crop.start_x = crop_and_resize_params->crop->start_x;
        crop_and_resize_args->crop.start_y = crop_and_resize_params->crop->start_y;
        crop_and_resize_args->crop.end_x = crop_and_resize_params->crop->end_x;
        crop_and_resize_args->crop.end_y = crop_and_resize_params->crop->end_y;
    }

    BufferList buffer_list;
    status = add_images_to_buffer_list(buffer_list, images);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed adding images to buffer list. Error code: {}\n", status);
        return status;
    }

    if (privacy_mask_params) {
        const size_t bitmask_width =
            ceil(mutli_crop_resize_params->src->width / (double)(PRIVACY_MASK_QUANTIZATION * 8));
        const size_t bitmask_stride = ROUND_UP(bitmask_width, 8);
        const size_t bitmask_height = ceil(mutli_crop_resize_params->src->height / (double)PRIVACY_MASK_QUANTIZATION);
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
    status = send_command(device, buffer_list, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size,
                          timeframe);
    tracer.update_event_timestamp_now(timeframe, DSP_TASK_FINISHED_EVENT);

    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}\n", status);
    }

    return status;
}

dsp_status dsp_multi_crop_and_resize(dsp_device device, const dsp_multi_crop_resize_params_t *mutli_crop_resize_params)
{
    return dsp_multi_crop_and_resize_perf(device, mutli_crop_resize_params, NULL, NULL);
}

dsp_status dsp_multi_crop_and_resize_privacy_mask(dsp_device device,
                                                  const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
                                                  const dsp_privacy_mask_t *privacy_mask_params)
{
    return dsp_multi_crop_and_resize_perf(device, mutli_crop_resize_params, privacy_mask_params, NULL);
}