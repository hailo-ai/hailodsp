/*
 * Copyright (c) 2017-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "privacy_mask_perf.h"
#include "send_command.hpp"
#include "thread_local_buffer.hpp"
#include "user_dsp_interface.h"

#include <utility>
#include <utils.h>

static_assert((int)INTERFACE_PRIVACY_MASK_COLOR == (int)DSP_PRIVACY_MASK_COLOR,
              "privacy_mask_type_in_data_t must be identical to dsp_privacy_mask_type_t");
static_assert((int)INTERFACE_PRIVACY_MASK_PIXELIZATION == (int)DSP_PRIVACY_MASK_PIXELIZATION,
              "privacy_mask_type_in_data_t must be identical to dsp_privacy_mask_type_t");
static_assert((int)DSP_NO_LETTERBOX == (int)INTERFACE_NO_LETTERBOX,
              "Interface and internal implementation must have the same number for no letterbox");
static_assert((int)DSP_LETTERBOX_MIDDLE == (int)INTERFACE_LETTERBOX_MIDDLE,
              "Interface and internal implementation must have the same number for letterbox middle");
static_assert((int)DSP_LETTERBOX_UP_LEFT == (int)INTERFACE_LETTERBOX_UP_LEFT,
              "Interface and internal implementation must have the same number for letterbox up_left");

// This function assumes that "image" params is already checked for correctness
static dsp_status verify_static_privacy_mask_params(const dsp_image_properties_t *image,
                                                    const dsp_static_privacy_mask_t *static_params)
{
    // static privacy mask is optional
    if (!static_params) {
        return DSP_SUCCESS;
    }

    if (!static_params->bitmask) {
        LOGGER__ERROR("Error: NULL argument (static_params->bitmask={})", fmt::ptr(static_params->bitmask));
        return DSP_INVALID_ARGUMENT;
    }

    if (static_params->rois_count == 0) {
        LOGGER__ERROR("Error: Must have at least 1 static ROI");
        return DSP_INVALID_ARGUMENT;
    }

    if (static_params->rois_count > MAX_STATIC_PRIVACY_MASKS) {
        LOGGER__ERROR("Error: Too many static ROIs. The operation supports up to {} static ROIs",
                      MAX_STATIC_PRIVACY_MASKS);
        return DSP_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < static_params->rois_count; ++i) {
        const auto &roi_params = static_params->rois[i];
        bool invalid_roi = false;

        if (roi_params.start_x >= roi_params.end_x) {
            LOGGER__ERROR("Error: ROI start_x ({}) must be smaller then end_x ({})", roi_params.start_x,
                          roi_params.end_x);
            invalid_roi = true;
        }

        if (roi_params.start_y >= roi_params.end_y) {
            LOGGER__ERROR("Error: ROI start_y ({}) must be smaller then end_y ({})", roi_params.start_y,
                          roi_params.end_y);
            invalid_roi = true;
        }

        const size_t bitmask_width = ceil(image->width / (float)PRIVACY_MASK_QUANTIZATION);
        if (roi_params.end_x > bitmask_width) {
            LOGGER__ERROR("Error: ROI end_x ({}) must be smaller or equal to quantized bitmask width ({})",
                          roi_params.end_x, bitmask_width);
            invalid_roi = true;
        }

        const size_t bitmask_height = ceil(image->height / (float)PRIVACY_MASK_QUANTIZATION);
        if (roi_params.end_y > bitmask_height) {
            LOGGER__ERROR("Error: ROI end_y ({}) must be smaller or equal to quantized bitmask height ({})",
                          roi_params.end_y, bitmask_height);
            invalid_roi = true;
        }

        if (invalid_roi) {
            LOGGER__ERROR("Error: ROI properties check failed for \"roi[{}]\"", i);
            return DSP_INVALID_ARGUMENT;
        }
    }

    return DSP_SUCCESS;
}

static dsp_status verify_dynamic_privacy_mask_params(const dsp_dynamic_privacy_mask_t *dynamic_params)
{
    // dynamic privacy mask is optional
    if (!dynamic_params) {
        return DSP_SUCCESS;
    }

    if (dynamic_params->masks_count == 0) {
        LOGGER__ERROR("Error: Must have at least 1 dynamic mask");
        return DSP_INVALID_ARGUMENT;
    }

    if (dynamic_params->masks_count > MAX_DYNAMIC_PRIVACY_MASKS) {
        LOGGER__ERROR("Error: Too many dynamic masks. The operation supports up to {} dynamic masks",
                      MAX_DYNAMIC_PRIVACY_MASKS);
        return DSP_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < dynamic_params->masks_count; ++i) {
        const auto &mask_params = dynamic_params->masks[i];
        bool invalid_roi = false;

        if (mask_params.bytemask == nullptr) {
            LOGGER__ERROR("Error: NULL argument (dynamic_params->masks[{}].bytemask={})", i,
                          fmt::ptr(mask_params.bytemask));
            return DSP_INVALID_ARGUMENT;
        }

        if (mask_params.roi.start_x >= mask_params.roi.end_x) {
            LOGGER__ERROR("Error: ROI start_x ({}) must be smaller then end_x ({}) for dynamic mask[{}]",
                          mask_params.roi.start_x, mask_params.roi.end_x, i);
            invalid_roi = true;
        }

        if (mask_params.roi.start_y >= mask_params.roi.end_y) {
            LOGGER__ERROR("Error: ROI start_y ({}) must be smaller then end_y ({}) for dynamic mask[{}]",
                          mask_params.roi.start_y, mask_params.roi.end_y, i);
            invalid_roi = true;
        }

        if (mask_params.roi.end_x > mask_params.input_frame_net_width) {
            LOGGER__ERROR(
                "Error: ROI end_x ({}) must be smaller or equal to net width ({}) for "
                "dynamic mask[{}]",
                mask_params.roi.end_x, mask_params.input_frame_net_width, i);
            invalid_roi = true;
        }

        if (mask_params.roi.end_y > mask_params.input_frame_net_height) {
            LOGGER__ERROR("Error: ROI end_y ({}) must be smaller or equal to net height ({}) for dynamic mask[{}]",
                          mask_params.roi.end_y, mask_params.input_frame_net_height, i);
            invalid_roi = true;
        }

        if (mask_params.dilation_size > 15) {
            LOGGER__ERROR("Error: dilation_size ({}) must be in the range [0, 15] for dynamic mask[{}]",
                          mask_params.dilation_size, i);
            invalid_roi = true;
        }

        if (invalid_roi) {
            LOGGER__ERROR("Error: ROI properties check failed for dynamic mask[{}]", i);
            return DSP_INVALID_ARGUMENT;
        }
    }

    return DSP_SUCCESS;
}

static dsp_status verify_privacy_mask_params(const dsp_image_properties_t *image,
                                             const unified_dsp_privacy_mask_t *privacy_mask_params)
{
    if (!privacy_mask_params) {
        LOGGER__ERROR("Error: NULL argument (privacy_mask_params={})", fmt::ptr(privacy_mask_params));
        return DSP_INVALID_ARGUMENT;
    }

    if (!privacy_mask_params->static_privacy_mask_params && !privacy_mask_params->dynamic_privacy_mask_params) {
        LOGGER__ERROR("Error: At least one of static or dynamic privacy mask parameters must be provided");
        return DSP_INVALID_ARGUMENT;
    }

    if (privacy_mask_params->type >= DSP_PRIVACY_MASK_COUNT) {
        LOGGER__ERROR("Error: Unknown privacy mask type {}", privacy_mask_params->type);
        return DSP_INVALID_ARGUMENT;
    }

    if (privacy_mask_params->type == DSP_PRIVACY_MASK_PIXELIZATION) {
        if (privacy_mask_params->pixelization_size < 1 ||
            privacy_mask_params->pixelization_size > DSP_PIXELIZATION_PRIVACY_MASK_MAX_SIZE) {
            LOGGER__ERROR("Error: pixelization_size ({}) must be in the range [1, {}]",
                          privacy_mask_params->pixelization_size, DSP_PIXELIZATION_PRIVACY_MASK_MAX_SIZE);
            return DSP_INVALID_ARGUMENT;
        }

        if (privacy_mask_params->pixelization_size % 2 != 0) {
            LOGGER__ERROR("Error: pixelization_size ({}) must be an even number",
                          privacy_mask_params->pixelization_size);
            return DSP_INVALID_ARGUMENT;
        }
    }

    auto status = verify_static_privacy_mask_params(image, privacy_mask_params->static_privacy_mask_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Static privacy mask parameters check failed");
        return status;
    }

    status = verify_dynamic_privacy_mask_params(privacy_mask_params->dynamic_privacy_mask_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Dynamic privacy mask parameters check failed");
        return status;
    }

    return DSP_SUCCESS;
}

dsp_status dsp_privacy_mask_perf(dsp_device device,
                                 dsp_image_properties_t *image,
                                 const unified_dsp_privacy_mask_t *privacy_mask_params,
                                 perf_info_t *perf_info)
{
    if ((!device) || (!privacy_mask_params)) {
        LOGGER__ERROR("Error: NULL argument (device={}, privacy_mask_params={})", fmt::ptr(device),
                      fmt::ptr(privacy_mask_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(IMAGING_OP_PRIVACY_MASK);

    if (image->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Image format ({}) is not supported", format_arg_to_string(image->format));
        return DSP_INVALID_ARGUMENT;
    }

    auto status = verify_image_properties(image);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed");
        return status;
    }

    status = verify_privacy_mask_params(image, privacy_mask_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Static privacy mask parameters check failed");
        return status;
    }

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }
    in_data->operation = IMAGING_OP_PRIVACY_MASK;

    std::vector<command_image_t> images = {{image, &in_data->privacy_mask_args.image, BufferAccessType::ReadWrite}};

    BufferList buffer_list;
    status = add_images_to_buffer_list(buffer_list, images);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed adding images to buffer list. Error code: {}", status);
        return status;
    }

    in_data->privacy_mask_args.type = (privacy_mask_type_in_data_t)privacy_mask_params->type;
    if (privacy_mask_params->type == DSP_PRIVACY_MASK_COLOR) {
        in_data->privacy_mask_args.color.y = privacy_mask_params->color.y;
        in_data->privacy_mask_args.color.u = privacy_mask_params->color.u;
        in_data->privacy_mask_args.color.v = privacy_mask_params->color.v;
    } else {
        in_data->privacy_mask_args.pixelization_size = privacy_mask_params->pixelization_size;
    }

    if (privacy_mask_params->static_privacy_mask_params) {
        in_data->privacy_mask_args.static_args.enabled = true;

        const size_t bitmask_width = ceil(image->width / (double)(PRIVACY_MASK_QUANTIZATION * 8));
        const size_t bitmask_stride = ROUND_UP(bitmask_width, 8);
        const size_t bitmask_height = ceil(image->height / (double)PRIVACY_MASK_QUANTIZATION);
        in_data->privacy_mask_args.static_args.bitmask.line_stride = bitmask_stride;
        in_data->privacy_mask_args.static_args.bitmask.plane_size = bitmask_stride * bitmask_height;
        in_data->privacy_mask_args.static_args.bitmask.xrp_buffer_index =
            buffer_list.add_buffer(privacy_mask_params->static_privacy_mask_params->bitmask,
                                   bitmask_stride * bitmask_height, BufferAccessType::Read);

        in_data->privacy_mask_args.static_args.rois_count = privacy_mask_params->static_privacy_mask_params->rois_count;
        for (size_t i = 0; i < privacy_mask_params->static_privacy_mask_params->rois_count; ++i) {
            in_data->privacy_mask_args.static_args.rois[i].start_x =
                privacy_mask_params->static_privacy_mask_params->rois[i].start_x;
            in_data->privacy_mask_args.static_args.rois[i].start_y =
                privacy_mask_params->static_privacy_mask_params->rois[i].start_y;
            in_data->privacy_mask_args.static_args.rois[i].end_x =
                privacy_mask_params->static_privacy_mask_params->rois[i].end_x;
            in_data->privacy_mask_args.static_args.rois[i].end_y =
                privacy_mask_params->static_privacy_mask_params->rois[i].end_y;
        }
    } else {
        in_data->privacy_mask_args.static_args.enabled = false;
    }

    if (privacy_mask_params->dynamic_privacy_mask_params != nullptr) {
        in_data->privacy_mask_args.dynamic_args.enabled = true;

        in_data->privacy_mask_args.dynamic_args.masks_count =
            privacy_mask_params->dynamic_privacy_mask_params->masks_count;
        for (size_t i = 0; i < privacy_mask_params->dynamic_privacy_mask_params->masks_count; ++i) {
            const auto &src_mask = privacy_mask_params->dynamic_privacy_mask_params->masks[i];
            auto &dst_mask = in_data->privacy_mask_args.dynamic_args.masks[i];

            dst_mask.input_frame_net_width = src_mask.input_frame_net_width;
            dst_mask.input_frame_net_height = src_mask.input_frame_net_height;
            dst_mask.letterbox = (interface_letterbox_alignment_t)src_mask.letterbox;
            dst_mask.pre_letterbox_aspect_ratio = src_mask.pre_letterbox_aspect_ratio;
            dst_mask.is_aspect_ratio_preserved = src_mask.is_aspect_ratio_preserved;
            dst_mask.roi.start_x = src_mask.roi.start_x;
            dst_mask.roi.start_y = src_mask.roi.start_y;
            dst_mask.roi.end_x = src_mask.roi.end_x;
            dst_mask.roi.end_y = src_mask.roi.end_y;
            dst_mask.dilation_size = src_mask.dilation_size;

            const size_t roi_w = src_mask.roi.end_x - src_mask.roi.start_x;
            const size_t roi_h = src_mask.roi.end_y - src_mask.roi.start_y;
            const size_t size = roi_w * roi_h;

            dst_mask.bytemask.line_stride = roi_w;
            dst_mask.bytemask.plane_size = size;
            dst_mask.bytemask.xrp_buffer_index =
                buffer_list.add_buffer(src_mask.bytemask, size, BufferAccessType::Read);
        }
    } else {
        in_data->privacy_mask_args.dynamic_args.enabled = false;
    }

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status = send_command(device, buffer_list, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size,
                          timeframe);
    tracer.update_event_timestamp_now(timeframe, DSP_TASK_FINISHED_EVENT);

    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing privacy mask operation. Error code: {}", status);
    }

    return status;
}

dsp_status dsp_privacy_mask(dsp_device device,
                            dsp_image_properties_t *image,
                            const unified_dsp_privacy_mask_t *privacy_mask_params)
{
    return dsp_privacy_mask_perf(device, image, privacy_mask_params, nullptr);
}