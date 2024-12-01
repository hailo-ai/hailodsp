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
#include "gaussian_perf.h"
#include "hailo/hailodsp.h"
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "send_command.hpp"
#include "thread_local_buffer.hpp"
#include "user_dsp_interface.h"

#include <algorithm>
#include <utils.h>

dsp_status dsp_multi_gaussian_perf(dsp_device device,
                                   dsp_gaussian_params_t *params,
                                   size_t params_count,
                                   perf_info_t *perf_info)
{
    if ((!device) || (!params)) {
        LOGGER__ERROR("Error: One of the parameters provided is NULL (device={}, params={})", fmt::ptr(device),
                      fmt::ptr(params));
        return DSP_INVALID_ARGUMENT;
    }

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(IMAGING_OP_MULTI_GAUSSIAN);

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }
    in_data->operation = IMAGING_OP_MULTI_GAUSSIAN;
    in_data->multi_gaussian_args.filters_count = params_count;

    std::vector<command_image_t> images;
    images.reserve(2 * params_count);

    for (size_t i = 0; i < params_count; i++) {
        auto gaussian_params = &params[i];
        auto gaussian_args = &in_data->multi_gaussian_args.filters[i];

        auto status = verify_image_properties(gaussian_params->src);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for src[{}]", i);
            return status;
        }

        status = verify_image_properties(gaussian_params->dst);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for dst[{}]", i);
            return status;
        }

        if (gaussian_params->src->format != DSP_IMAGE_FORMAT_GRAY8) {
            LOGGER__ERROR("Error: Src [{}] format ({}) is not supported", i,
                          format_arg_to_string(gaussian_params->src->format));
            return DSP_INVALID_ARGUMENT;
        }

        if (gaussian_params->dst->format != gaussian_params->src->format) {
            LOGGER__ERROR("Error: Dst format ({}) is not the same as src format ({}) on gaussian {}",
                          format_arg_to_string(gaussian_params->dst->format),
                          format_arg_to_string(gaussian_params->src->format), i);
            return DSP_INVALID_ARGUMENT;
        }

        if (gaussian_params->kernel_size % 2 == 0) {
            LOGGER__ERROR("Error: Kernel size should be odd on gaussian {}", i);
            return DSP_INVALID_ARGUMENT;
        }

        if (gaussian_params->kernel_size > MAX_FILTER_KERNEL_SIZE) {
            LOGGER__ERROR("Error: Kernel size cannot exceed {} on gaussian {}", MAX_FILTER_KERNEL_SIZE, i);
            return DSP_INVALID_ARGUMENT;
        }

        gaussian_args->kernel_size = gaussian_params->kernel_size;

        images.emplace_back(command_image_t{
            .user_api_image = gaussian_params->src,
            .dsp_api_image = &gaussian_args->src,
            .access_type = BufferAccessType::Read,
        });

        images.emplace_back(command_image_t{
            .user_api_image = gaussian_params->dst,
            .dsp_api_image = &gaussian_args->dst,
            .access_type = BufferAccessType::Write,
        });
    }

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    auto status =
        send_command(device, images, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size, timeframe);
    tracer.update_event_timestamp_now(timeframe, DSP_TASK_FINISHED_EVENT);

    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing gaussian operation. Error code: {}", status);
    }

    return status;
}

dsp_status dsp_multi_gaussian(dsp_device device, dsp_gaussian_params_t *params, size_t params_count)
{
    return dsp_multi_gaussian_perf(device, params, params_count, NULL);
}

dsp_status dsp_gaussian_perf(dsp_device device, dsp_gaussian_params_t *params, perf_info_t *perf_info)
{
    return dsp_multi_gaussian_perf(device, params, 1, perf_info);
}

dsp_status dsp_gaussian(dsp_device device, dsp_gaussian_params_t *params)
{
    return dsp_gaussian_perf(device, params, NULL);
}
