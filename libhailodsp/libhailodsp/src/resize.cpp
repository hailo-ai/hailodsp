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
        LOGGER__ERROR("Error: NULL argument (image={}, privacy_mask_params->bitmask={})", fmt::ptr(image),
                      fmt::ptr(privacy_mask_params->bitmask));
        return DSP_INVALID_ARGUMENT;
    }

    if (privacy_mask_params->rois_count == 0) {
        LOGGER__ERROR("Error: Must have at least 1 ROI");
        return DSP_INVALID_ARGUMENT;
    }

    if (privacy_mask_params->rois_count > MAX_PRIVACY_MASK_ROIS) {
        LOGGER__ERROR("Error: Too many ROIs. The operation supports up to {} ROIs", MAX_PRIVACY_MASK_ROIS);
        return DSP_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < privacy_mask_params->rois_count; ++i) {
        dsp_roi_t *roi_params = &privacy_mask_params->rois[i];
        bool invalid_roi = false;

        if (roi_params->start_x >= roi_params->end_x) {
            LOGGER__ERROR("Error: ROI start_x ({}) must be smaller then end_x ({})", roi_params->start_x,
                          roi_params->end_x);
            invalid_roi = true;
        }

        if (roi_params->start_y >= roi_params->end_y) {
            LOGGER__ERROR("Error: ROI start_y ({}) must be smaller then end_y ({})", roi_params->start_y,
                          roi_params->end_y);
            invalid_roi = true;
        }

        const size_t bitmask_width = ceil(image->width / (float)PRIVACY_MASK_QUANTIZATION);
        if (roi_params->end_x > bitmask_width) {
            LOGGER__ERROR("Error: ROI end_x ({}) must be smaller or equal to quantized bitmask width ({})",
                          roi_params->end_x, bitmask_width);
            invalid_roi = true;
        }

        const size_t bitmask_height = ceil(image->height / (float)PRIVACY_MASK_QUANTIZATION);
        if (roi_params->end_y > bitmask_height) {
            LOGGER__ERROR("Error: ROI end_y ({}) must be smaller or equal to quantized bitmask height ({})",
                          roi_params->end_y, bitmask_height);
            invalid_roi = true;
        }

        if (invalid_roi) {
            LOGGER__ERROR("Error: ROI properties check failed for \"roi[{}]\"", i);
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
            "times smaller than src width({})",
            dst_width, max_scale, src_width);
        return DSP_INVALID_ARGUMENT;
    }

    if (dst_height * max_scale < src_height) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst height({}) can be at most {} "
            "times smaller than src height({})",
            dst_height, max_scale, src_height);
        return DSP_INVALID_ARGUMENT;
    }

    if (dst_width > src_width * max_scale) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst width({}) can be at most {} "
            "times larger than src width({})",
            dst_width, max_scale, src_width);
        return DSP_INVALID_ARGUMENT;
    }

    if (dst_height > src_height * max_scale) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst height({}) can be at most {} "
            "times larger than src height({})",
            dst_height, max_scale, src_height);
        return DSP_INVALID_ARGUMENT;
    }

    return DSP_SUCCESS;
}

static dsp_status verify_image_enhancement_params(const dsp_image_enhancement_params_t *image_enhancement_params)
{
    if (!image_enhancement_params) {
        // image_enhancement params are optional
        return DSP_SUCCESS;
    }

    if (image_enhancement_params->sharpness < 1 || image_enhancement_params->sharpness > 5) {
        LOGGER__ERROR("Error: sharpness ({}) must be in the range [1, 5]", image_enhancement_params->sharpness);
        return DSP_INVALID_ARGUMENT;
    }

    if (image_enhancement_params->brightness < -128 || image_enhancement_params->brightness > 128) {
        LOGGER__ERROR("Error: brightness ({}) must be in the range [-128, 128]", image_enhancement_params->brightness);
        return DSP_INVALID_ARGUMENT;
    }

    if (image_enhancement_params->saturation_u_b < -128 || image_enhancement_params->saturation_u_b > 128) {
        LOGGER__ERROR("Error: saturation_u_b ({}) must be in the range [-128, 128]",
                      image_enhancement_params->saturation_u_b);
        return DSP_INVALID_ARGUMENT;
    }

    if (image_enhancement_params->saturation_v_b < -128 || image_enhancement_params->saturation_v_b > 128) {
        LOGGER__ERROR("Error: saturation_v_b ({}) must be in the range [-128, 128]",
                      image_enhancement_params->saturation_v_b);
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
        LOGGER__ERROR("Error: NULL argument (device={}, resize_params={})", fmt::ptr(device), fmt::ptr(resize_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(IMAGING_OP_CROP_AND_RESIZE);
    auto status = verify_crop_params(resize_params->src, crop_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Crop parameters check failed");
        return status;
    }

    dsp_image_properties_t cropped_src = *resize_params->src;
    cropped_src.width = crop_params->end_x - crop_params->start_x;
    cropped_src.height = crop_params->end_y - crop_params->start_y;

    status = verify_image_properties(&cropped_src);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"src\" (after crop)");
        return status;
    }

    status = verify_image_properties(resize_params->dst);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"dst\"");
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
            LOGGER__ERROR("Error: Image resize scale check failed");
            return status;
        }
    }

    if (resize_params->interpolation >= INTERPOLATION_TYPE_COUNT) {
        LOGGER__ERROR("Error: Unknown interpolation type {}", resize_params->interpolation);
        return DSP_INVALID_ARGUMENT;
    }

    // Only same format for src and dst is currently supported and only GRAY8, RGB, BGR or NV12
    if (resize_params->src->format != resize_params->dst->format) {
        LOGGER__ERROR("Error: The src and dst formats are not the same");
        return DSP_INVALID_ARGUMENT;
    }

    switch (resize_params->src->format) {
        case DSP_IMAGE_FORMAT_GRAY8:
        case DSP_IMAGE_FORMAT_RGB:
        case DSP_IMAGE_FORMAT_BGR:
        case DSP_IMAGE_FORMAT_NV12:
            break;

        default:
            LOGGER__ERROR("Error: The src/dst format ({}) is not supported",
                          format_arg_to_string(resize_params->src->format));
            return DSP_INVALID_ARGUMENT;
    }

    if ((resize_params->interpolation == INTERPOLATION_TYPE_AREA) &&
        ((cropped_src.width < resize_params->dst->width) || (cropped_src.height < resize_params->dst->height))) {
        LOGGER__ERROR("Error: Area interpolation does not support upscaling");
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
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}", status);
    }

    return status;
}

dsp_status dsp_resize_perf(dsp_device device,
                           const dsp_resize_params_t *resize_params,
                           const dsp_letterbox_properties_t *letterbox_params,
                           perf_info_t *perf_info)
{
    if (!resize_params) {
        LOGGER__ERROR("Error: resize_params is NULL");
        return DSP_INVALID_ARGUMENT;
    }

    if (!resize_params->src) {
        LOGGER__ERROR("Error: resize_params->src is NULL");
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
        LOGGER__ERROR("Error: NULL argument (device={}, mutli_crop_resize_params={})", fmt::ptr(device),
                      fmt::ptr(mutli_crop_resize_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(IMAGING_OP_MULTI_CROP_AND_RESIZE);

    auto status = verify_privacy_mask_params(mutli_crop_resize_params->src, privacy_mask_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Privacy mask parameters check failed");
        return status;
    }

    if (mutli_crop_resize_params->src->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Src format ({}) is not supported",
                      format_arg_to_string(mutli_crop_resize_params->src->format));
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }

    in_data->multi_crop_and_resize_args.privacy_mask.enabled = false;
    in_data->multi_crop_and_resize_args.multi_resizes_count = mutli_crop_resize_params->crop_resize_params_count;
    in_data->operation = IMAGING_OP_MULTI_CROP_AND_RESIZE;

    if ((mutli_crop_resize_params->interpolation != INTERPOLATION_TYPE_BILINEAR) &&
        (mutli_crop_resize_params->interpolation != INTERPOLATION_TYPE_BICUBIC)) {
        LOGGER__ERROR("Error: interpolation type ({}) not supported", mutli_crop_resize_params->interpolation);
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
            LOGGER__ERROR("Error: Crop parameters check failed for crop&resize[{}]", j);
            return status;
        }

        dsp_image_properties_t cropped_src = *mutli_crop_resize_params->src;
        cropped_src.width = crop_and_resize_params->crop->end_x - crop_and_resize_params->crop->start_x;
        cropped_src.height = crop_and_resize_params->crop->end_y - crop_and_resize_params->crop->start_y;

        status = verify_image_properties(&cropped_src);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for src (after crop)");
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
                LOGGER__ERROR("Error: Image properties check failed for dst[{}, {}]", j, i);
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
                LOGGER__ERROR("Error: Image resize scale check failed for dst[{}, {}]", j, i);
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
        LOGGER__ERROR("Error: Failed adding images to buffer list. Error code: {}", status);
        return status;
    }

    if (privacy_mask_params) {
        in_data->multi_crop_and_resize_args.privacy_mask.enabled = true;
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
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}", status);
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

/* Telescopic is only valid if each resolution is less than or equal to the previous one in both width and height */
dsp_status verify_telescopic_resolutions(dsp_crop_resize_params_t *params)
{
    const dsp_image_properties_t *prev_resolution = nullptr;
    int i = 0;

    /* Find the first resolution in the list */
    for (i = 0; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; i++) {
        if (params->dst[i] != nullptr) {
            prev_resolution = params->dst[i];
            break;
        }
    }

    if (prev_resolution == nullptr) {
        LOGGER__ERROR("Error: At least one image destination must be provided");
        return DSP_INVALID_ARGUMENT;
    }

    /* Check that each resolution is less than or equal to the previous one */
    for (; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; i++) {
        if (params->dst[i] == nullptr) {
            continue;
        }

        if (params->dst[i]->width > prev_resolution->width) {
            LOGGER__ERROR("Error: Resolution {} width ({}) must be lower or equal to previous resolution width ({})", i,
                          params->dst[i]->width, prev_resolution->width);
            return DSP_INVALID_ARGUMENT;
        }

        if (params->dst[i]->height > prev_resolution->height) {
            LOGGER__ERROR("Error: Resolution {} height ({}) must be lower or equal to previous resolution height ({})",
                          i, params->dst[i]->height, prev_resolution->height);
            return DSP_INVALID_ARGUMENT;
        }

        prev_resolution = params->dst[i];
    }

    return DSP_SUCCESS;
}

dsp_status dsp_telescopic_multi_crop_and_resize_perf(dsp_device device,
                                                     const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
                                                     const dsp_privacy_mask_t *privacy_mask_params,
                                                     const dsp_image_enhancement_params_t *image_enhancement_params,
                                                     perf_info_t *perf_info)
{
    if ((!device) || (!mutli_crop_resize_params)) {
        LOGGER__ERROR("Error: NULL argument (device={}, mutli_crop_resize_params={})", fmt::ptr(device),
                      fmt::ptr(mutli_crop_resize_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto imaging_op = IMAGING_OP_TELESCOPIC_MULTI_CROP_AND_RESIZE;

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(imaging_op);

    auto status = verify_privacy_mask_params(mutli_crop_resize_params->src, privacy_mask_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Privacy mask parameters check failed");
        return status;
    }

    status = verify_image_enhancement_params(image_enhancement_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: image_enhancement parameters check failed");
        return status;
    }

    if (mutli_crop_resize_params->src->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Src format ({}) is not supported",
                      format_arg_to_string(mutli_crop_resize_params->src->format));
        return DSP_INVALID_ARGUMENT;
    }

    if (mutli_crop_resize_params->interpolation != INTERPOLATION_TYPE_BILINEAR) {
        LOGGER__ERROR("Error: interpolation type ({}) not supported", mutli_crop_resize_params->interpolation);
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }

    in_data->multi_crop_and_resize_args.privacy_mask.enabled = false;
    in_data->multi_crop_and_resize_args.image_enhancement.enabled = false;
    in_data->multi_crop_and_resize_args.multi_resizes_count = mutli_crop_resize_params->crop_resize_params_count;
    in_data->operation = imaging_op;
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
            LOGGER__ERROR("Error: Crop parameters check failed for crop&resize[{}]", j);
            return status;
        }

        dsp_image_properties_t cropped_src = *mutli_crop_resize_params->src;
        cropped_src.width = crop_and_resize_params->crop->end_x - crop_and_resize_params->crop->start_x;
        cropped_src.height = crop_and_resize_params->crop->end_y - crop_and_resize_params->crop->start_y;

        status = verify_image_properties(&cropped_src);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for src[{}] (after crop)", j);
            return status;
        }

        status = verify_telescopic_resolutions(crop_and_resize_params);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Telescopic resolutions check failed for crop&resize[{}]", j);
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
                LOGGER__ERROR("Error: Image properties check failed for dst[{}, {}]", j, i);
                return status;
            }

            if (dst_image->format != DSP_IMAGE_FORMAT_NV12) {
                LOGGER__ERROR("Error: brightness ({}) must be in the range [-128, 128]",
                              image_enhancement_params->brightness);
                return DSP_INVALID_ARGUMENT;
            }

            status = verify_resize_scaling(cropped_src.width, cropped_src.height, dst_image->width, dst_image->height,
                                           MULTI_CROP_RESIZE_MAX_SCALE);
            if (status != DSP_SUCCESS) {
                LOGGER__ERROR("Error: Image resize scale check failed for dst[{}, {}]", j, i);
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
        LOGGER__ERROR("Error: Failed adding images to buffer list. Error code: {}", status);
        return status;
    }

    if (privacy_mask_params) {
        in_data->multi_crop_and_resize_args.privacy_mask.enabled = true;
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

    if (image_enhancement_params) {
        in_data->multi_crop_and_resize_args.image_enhancement.enabled = true;
        in_data->multi_crop_and_resize_args.image_enhancement.sharpness = image_enhancement_params->sharpness;
        in_data->multi_crop_and_resize_args.image_enhancement.contrast = image_enhancement_params->contrast;
        in_data->multi_crop_and_resize_args.image_enhancement.brightness = image_enhancement_params->brightness;
        in_data->multi_crop_and_resize_args.image_enhancement.saturation_u_a = image_enhancement_params->saturation_u_a;
        in_data->multi_crop_and_resize_args.image_enhancement.saturation_u_b = image_enhancement_params->saturation_u_b;
        in_data->multi_crop_and_resize_args.image_enhancement.saturation_v_a = image_enhancement_params->saturation_v_a;
        in_data->multi_crop_and_resize_args.image_enhancement.saturation_v_b = image_enhancement_params->saturation_v_b;
    }

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status = send_command(device, buffer_list, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size,
                          timeframe);
    tracer.update_event_timestamp_now(timeframe, DSP_TASK_FINISHED_EVENT);

    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}", status);
    }

    return status;
}

dsp_status dsp_telescopic_multi_crop_and_resize(dsp_device device,
                                                const dsp_multi_crop_resize_params_t *mutli_crop_resize_params)
{
    return dsp_telescopic_multi_crop_and_resize_perf(device, mutli_crop_resize_params, NULL, NULL, NULL);
}

dsp_status dsp_telescopic_multi_crop_and_resize_privacy_mask(
    dsp_device device,
    const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
    const dsp_privacy_mask_t *privacy_mask_params)
{
    return dsp_telescopic_multi_crop_and_resize_perf(device, mutli_crop_resize_params, privacy_mask_params, NULL, NULL);
}

dsp_status dsp_telescopic_multi_resize_privacy_mask_image_enhancement(
    dsp_device device,
    const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
    const dsp_privacy_mask_t *privacy_mask_params,
    const dsp_image_enhancement_params_t *image_enhancement_params)
{
    return dsp_telescopic_multi_crop_and_resize_perf(device, mutli_crop_resize_params, privacy_mask_params,
                                                     image_enhancement_params, NULL);
}