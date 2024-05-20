/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
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
#include "affine_transform_perf.h"
#include "aligned_uptr.hpp"
#include "dsp_tracer.hpp"
#include "hailo/hailodsp.h"
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "send_command.hpp"
#include "user_dsp_interface.h"

static dsp_status safe_copy_affine_matrix(affine_transform_matrix *dst, dsp_affine_transform_matrix *src)
{
    if (!src || !dst) {
        LOGGER__ERROR("Error: Null affine matrix provided\n");
        return DSP_INVALID_ARGUMENT;
    }
    dst->a11 = src->a11;
    dst->a12 = src->a12;
    dst->a13 = src->a13;
    dst->a21 = src->a21;
    dst->a22 = src->a22;
    dst->a23 = src->a23;

    return DSP_SUCCESS;
}

static dsp_status check_cropped_image_args(const dsp_image_properties_t *src,
                                           const dsp_multi_warp_affine_cropped_part_t *crop)
{
    auto status = verify_image_properties(crop->dst);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"dst\"\n");
        return status;
    }

    if (crop->dst->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Only nv12 format is supported for warp affine\n");
        return DSP_INVALID_ARGUMENT;
    }

    status = verify_crop_params(src, crop->params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Crop parameters check failed\n");
        return status;
    }

    return status;
}

dsp_status dsp_multi_warp_affine_perf(dsp_device device,
                                      const dsp_multi_warp_affine_params_t *multi_warp_affine_params,
                                      perf_info_t *perf_info)
{
    if (!device) {
        LOGGER__ERROR("Error: NULL argument (device={})\n", fmt::ptr(device));
        return DSP_INVALID_ARGUMENT;
    }

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(IMAGING_OP_MULTI_AFFINE_TRANSFORM);

    if ((multi_warp_affine_params->crops_count == 0) ||
        (multi_warp_affine_params->crops_count > MAX_AFFINE_TRANSFORMATION_CROPS)) {
        LOGGER__ERROR("Error: bad number of images: {}, must be between 0 and {}\n",
                      multi_warp_affine_params->crops_count, MAX_AFFINE_TRANSFORMATION_CROPS);
        return DSP_INVALID_ARGUMENT;
    }

    auto status = verify_image_properties(multi_warp_affine_params->src);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"src\"\n");
        return status;
    }

    if (multi_warp_affine_params->interpolation != INTERPOLATION_TYPE_BILINEAR) {
        LOGGER__ERROR("Error: only bilinear interpolation type is supported for warp affine\n");
        return DSP_INVALID_ARGUMENT;
    }

    if (multi_warp_affine_params->src->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: only nv12 format is supported for warp affine\n");
        return DSP_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < multi_warp_affine_params->crops_count; i++) {
        status = check_cropped_image_args(multi_warp_affine_params->src, multi_warp_affine_params->crops[i]);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Bad image crop[{}] args\n", i);
            return status;
        }
    }

    auto in_data = make_aligned_uptr<imaging_request_t>();
    in_data->operation = IMAGING_OP_MULTI_AFFINE_TRANSFORM;
    in_data->multi_affine_transform_args.interpolation = multi_warp_affine_params->interpolation;
    in_data->multi_affine_transform_args.crops_count = multi_warp_affine_params->crops_count;

    std::vector<command_image_t> images;
    images.reserve(1 + multi_warp_affine_params->crops_count);

    images.emplace_back(command_image_t{
        .user_api_image = multi_warp_affine_params->src,
        .dsp_api_image = &in_data->multi_affine_transform_args.src,
        .access_type = BufferAccessType::Read,
    });

    for (size_t i = 0; i < multi_warp_affine_params->crops_count; i++) {
        status = safe_copy_affine_matrix(&in_data->multi_affine_transform_args.crops[i].affine_matrix,
                                         multi_warp_affine_params->crops[i]->affine_matrix);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Failed copying transformation matrix\n");
            return status;
        }

        in_data->multi_affine_transform_args.crops[i].params.start_x =
            multi_warp_affine_params->crops[i]->params->start_x;
        in_data->multi_affine_transform_args.crops[i].params.start_y =
            multi_warp_affine_params->crops[i]->params->start_y;
        in_data->multi_affine_transform_args.crops[i].params.end_x = multi_warp_affine_params->crops[i]->params->end_x;
        in_data->multi_affine_transform_args.crops[i].params.end_y = multi_warp_affine_params->crops[i]->params->end_y;

        images.emplace_back(command_image_t{
            .user_api_image = multi_warp_affine_params->crops[i]->dst,
            .dsp_api_image = &in_data->multi_affine_transform_args.crops[i].dst,
            .access_type = BufferAccessType::Write,
        });
    }

    BufferList buffer_list;
    status = add_images_to_buffer_list(buffer_list, images);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed adding images to buffer list. Error code: {}\n", status);
        return status;
    }

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status = send_command(device, buffer_list, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size,
                          timeframe);
    tracer.update_event_timestamp_now(timeframe, DSP_TASK_FINISH_EVENT);

    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing multi warp affine operation. Error code: {}\n", status);
    }

    return status;
}

dsp_status dsp_warp_affine(dsp_device device, const dsp_warp_affine_params_t *warp_affine_params)
{
    dsp_roi_t crop_params = {
        .start_x = 0,
        .start_y = 0,
        .end_x = warp_affine_params->src->width,
        .end_y = warp_affine_params->src->height,
    };

    dsp_multi_warp_affine_cropped_part_t crops[] = {{
        .params = &crop_params,
        .dst = warp_affine_params->dst,
        .affine_matrix = warp_affine_params->affine_matrix,
    }};

    dsp_multi_warp_affine_params_t multi_warp_affine_params = {
        .src = warp_affine_params->src,
        .crops = crops,
        .interpolation = warp_affine_params->interpolation,
        .crops_count = 1,
    };

    return dsp_multi_warp_affine_perf(device, &multi_warp_affine_params, NULL);
}

dsp_status dsp_multi_warp_affine(dsp_device device, const dsp_multi_warp_affine_params_t *multi_warp_affine_params)
{
    return dsp_multi_warp_affine_perf(device, multi_warp_affine_params, NULL);
}

dsp_status dsp_theta_to_affine_matrix(float theta,
                                      uint32_t image_width,
                                      uint32_t image_height,
                                      dsp_affine_transform_matrix *affine_matrix)
{
    if (!affine_matrix) {
        LOGGER__ERROR("Error: Null affine matrix provided\n");
        return DSP_INVALID_ARGUMENT;
    }

    double theta_radians = theta * M_PI / 180.0;
    float center_x = (float)image_width / 2;
    float center_y = (float)image_height / 2;
    double alpha = cos(theta_radians);
    double beta = sin(theta_radians);

    affine_matrix->a11 = alpha;
    affine_matrix->a12 = beta;
    affine_matrix->a13 = ((1 - alpha) * center_x) - (beta * center_y);
    affine_matrix->a21 = -beta;
    affine_matrix->a22 = alpha;
    affine_matrix->a23 = (beta * center_x) + ((1 - alpha) * center_y);

    return DSP_SUCCESS;
}

dsp_status dsp_rotate(dsp_device device, const dsp_affine_rotation_params_t *rotate_params)
{
    dsp_affine_transform_matrix affine_matrix;
    dsp_theta_to_affine_matrix(rotate_params->theta, rotate_params->dst->width, rotate_params->dst->height,
                               &affine_matrix);

    dsp_roi_t crop_params = {
        .start_x = 0,
        .start_y = 0,
        .end_x = rotate_params->src->width,
        .end_y = rotate_params->src->height,
    };

    dsp_multi_warp_affine_cropped_part_t crops[] = {{
        .params = &crop_params,
        .dst = rotate_params->dst,
        .affine_matrix = &affine_matrix,
    }};

    dsp_multi_warp_affine_params_t multi_warp_affine_params = {
        .src = rotate_params->src,
        .crops = crops,
        .interpolation = rotate_params->interpolation,
        .crops_count = 1,
    };

    return dsp_multi_warp_affine_perf(device, &multi_warp_affine_params, NULL);
}
