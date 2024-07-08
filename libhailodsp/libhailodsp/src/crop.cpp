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

#include "crop_perf.h"
#include "hailo/hailodsp.h"
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "send_command.hpp"
#include "thread_local_buffer.hpp"
#include "user_dsp_interface.h"

#include <utils.h>

dsp_status dsp_multi_crop_perf(dsp_device device,
                               const dsp_crop_params_t *multi_crop_params,
                               size_t crops_count,
                               perf_info_t *perf_info)
{
    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(IMAGING_OP_MULTI_CROP);

    if ((!device) || (!multi_crop_params)) {
        LOGGER__ERROR("Error: NULL argument (device={}, multi_crop_params={})\n", fmt::ptr(device),
                      fmt::ptr(multi_crop_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }

    in_data->operation = IMAGING_OP_MULTI_CROP;
    in_data->multi_crop_args.crops_count = crops_count;

    std::vector<command_image_t> images;
    images.reserve(2 * crops_count);

    for (size_t i = 0; i < crops_count; ++i) {
        auto single_crop_params = &multi_crop_params[i];

        if (!single_crop_params->src || !single_crop_params->dst) {
            LOGGER__ERROR("Error: NULL argument at index {} (src={}, dst={})\n", i, fmt::ptr(single_crop_params->src),
                          fmt::ptr(single_crop_params->dst));
            return DSP_INVALID_ARGUMENT;
        }

        if (single_crop_params->src->format != DSP_IMAGE_FORMAT_NV12 &&
            single_crop_params->src->format != DSP_IMAGE_FORMAT_GRAY8 &&
            single_crop_params->src->format != DSP_IMAGE_FORMAT_RGB &&
            single_crop_params->src->format != DSP_IMAGE_FORMAT_A420 &&
            single_crop_params->src->format != DSP_IMAGE_FORMAT_ARGB) {
            LOGGER__ERROR("Error: Src [{}] format ({}) is not supported\n", i,
                          format_arg_to_string(single_crop_params->src->format));
            return DSP_INVALID_ARGUMENT;
        }

        auto status = verify_crop_params(single_crop_params->src, &single_crop_params->src_roi);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Src crop parameters check failed for crop[{}]\n", i);
            return status;
        }

        status = verify_crop_params(single_crop_params->dst, &single_crop_params->dst_roi);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Dst crop parameters check failed for crop[{}]\n", i);
            return status;
        }

        dsp_image_properties_t cropped_src = *multi_crop_params->src;
        cropped_src.width = single_crop_params->src_roi.end_x - single_crop_params->src_roi.start_x;
        cropped_src.height = single_crop_params->src_roi.end_y - single_crop_params->src_roi.start_y;

        status = verify_image_properties(&cropped_src);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for src[{}] (after crop)\n", i);
            return status;
        }

        dsp_image_properties_t cropped_dst = *multi_crop_params->dst;
        cropped_dst.width = single_crop_params->dst_roi.end_x - single_crop_params->dst_roi.start_x;
        cropped_dst.height = single_crop_params->dst_roi.end_y - single_crop_params->dst_roi.start_y;

        status = verify_image_properties(&cropped_dst);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for dst[{}] (after crop)\n", i);
            return status;
        }

        if ((cropped_src.width != cropped_dst.width) || (cropped_src.height != cropped_dst.height)) {
            LOGGER__ERROR("Error: Src and Dst ROI sizes are different for crop[{}]\n", i);
            return DSP_INVALID_ARGUMENT;
        }

        if (single_crop_params->dst->format != multi_crop_params->src->format) {
            LOGGER__ERROR("Error: Dst format ({}) is not the same as src format ({}) on crop {}\n",
                          format_arg_to_string(single_crop_params->dst->format),
                          format_arg_to_string(multi_crop_params->src->format), i);
            return DSP_INVALID_ARGUMENT;
        }

        images.emplace_back(command_image_t{
            .user_api_image = single_crop_params->src,
            .dsp_api_image = &in_data->multi_crop_args.crops[i].src,
            .access_type = BufferAccessType::Read,
        });

        images.emplace_back(command_image_t{
            .user_api_image = single_crop_params->dst,
            .dsp_api_image = &in_data->multi_crop_args.crops[i].dst,
            .access_type = BufferAccessType::Write,
        });

        auto single_crop_args = &in_data->multi_crop_args.crops[i];
        single_crop_args->src_roi.start_x = single_crop_params->src_roi.start_x;
        single_crop_args->src_roi.start_y = single_crop_params->src_roi.start_y;
        single_crop_args->src_roi.end_x = single_crop_params->src_roi.end_x;
        single_crop_args->src_roi.end_y = single_crop_params->src_roi.end_y;
        single_crop_args->dst_roi.start_x = single_crop_params->dst_roi.start_x;
        single_crop_args->dst_roi.start_y = single_crop_params->dst_roi.start_y;
        single_crop_args->dst_roi.end_x = single_crop_params->dst_roi.end_x;
        single_crop_args->dst_roi.end_y = single_crop_params->dst_roi.end_y;
    }

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    auto status =
        send_command(device, images, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size, timeframe);
    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}\n", status);
        return status;
    }

    return DSP_SUCCESS;
}

dsp_status dsp_multi_crop(dsp_device device, const dsp_crop_params_t *multi_crop_params, size_t crops_count)
{
    return dsp_multi_crop_perf(device, multi_crop_params, crops_count, nullptr);
}

dsp_status dsp_crop(dsp_device device, const dsp_crop_params_t *crop_params)
{
    return dsp_multi_crop_perf(device, crop_params, 1, nullptr);
}
