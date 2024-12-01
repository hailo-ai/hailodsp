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
#include "blend_perf.h"
#include "dsp_tracer.hpp"
#include "hailo/hailodsp.h"
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "send_command.hpp"
#include "thread_local_buffer.hpp"
#include "user_dsp_interface.h"

#include <utils.h>

static dsp_status verify_overlay_params(const dsp_image_properties_t *image, const dsp_overlay_properties_t *overlay)
{
    if (overlay->x_offset >= (int)image->width) {
        LOGGER__ERROR("Error: Overlay x_offset ({}) is outside the image width ({})", overlay->x_offset, image->width);
        return DSP_INVALID_ARGUMENT;
    }

    if (overlay->y_offset >= (int)image->height) {
        LOGGER__ERROR("Error: Overlay y_offset ({}) is outside the image height ({})", overlay->y_offset,
                      image->height);
        return DSP_INVALID_ARGUMENT;
    }

    if (overlay->x_offset + (int)overlay->overlay.width <= 0) {
        LOGGER__ERROR("Error: Overlay is outside the image (x_offset={}, overlay_width={})", overlay->x_offset,
                      overlay->overlay.width);
        return DSP_INVALID_ARGUMENT;
    }

    if (overlay->y_offset + (int)overlay->overlay.height <= 0) {
        LOGGER__ERROR("Error: Overlay is outside the image (y_offset={}, overlay_height={})", overlay->y_offset,
                      overlay->overlay.height);
        return DSP_INVALID_ARGUMENT;
    }

    return DSP_SUCCESS;
}

dsp_status dsp_blend_perf(dsp_device device,
                          const dsp_image_properties_t *image,
                          const dsp_overlay_properties_t overlays[],
                          size_t overlays_count,
                          perf_info_t *perf_info)
{
    if ((!device) || (!image) || (!overlays)) {
        LOGGER__ERROR("Error: One of the parameters provided is NULL (device={}, image={}, overlays={})",
                      fmt::ptr(device), fmt::ptr(image), fmt::ptr(overlays));
        return DSP_INVALID_ARGUMENT;
    }

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(IMAGING_OP_BLEND);

    if (overlays_count > MAX_BLEND_OVERLAYS) {
        LOGGER__ERROR("Error: Too many overlays. The operation supports up to {} overlays", MAX_BLEND_OVERLAYS);
        return DSP_INVALID_ARGUMENT;
    }

    auto status = verify_image_properties(image);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"image\"");
        return status;
    }

    if (image->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Image format ({}) is not supported", format_arg_to_string(image->format));
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }

    in_data->operation = IMAGING_OP_BLEND;
    in_data->blend_args.overlays_count = overlays_count;

    std::vector<command_image_t> images;
    images.reserve(overlays_count + 1);

    images.emplace_back(command_image_t{
        .user_api_image = image,
        .dsp_api_image = &in_data->blend_args.background,
        .access_type = BufferAccessType::ReadWrite,
    });

    for (size_t i = 0; i < overlays_count; ++i) {
        status = verify_image_properties(&overlays[i].overlay);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for \"overlays[{}]\"", i);
            return status;
        }

        status = verify_overlay_params(image, &overlays[i]);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Overlay parameters check failed for \"overlays[{}]\"", i);
            return status;
        }

        if (overlays[i].overlay.format != DSP_IMAGE_FORMAT_A420 &&
            overlays[i].overlay.format != DSP_IMAGE_FORMAT_ARGB) {
            LOGGER__ERROR("Error: Overlay[{}] format ({}) is not supported", i,
                          format_arg_to_string(overlays[i].overlay.format));
            return DSP_INVALID_ARGUMENT;
        }

        images.emplace_back(command_image_t{
            .user_api_image = &overlays[i].overlay,
            .dsp_api_image = &in_data->blend_args.overlays[i].overlay,
            .access_type = BufferAccessType::Read,
        });

        in_data->blend_args.overlays[i].x_offset = overlays[i].x_offset;
        in_data->blend_args.overlays[i].y_offset = overlays[i].y_offset;
    }

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status =
        send_command(device, images, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size, timeframe);
    tracer.update_event_timestamp_now(timeframe, DSP_TASK_FINISHED_EVENT);

    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing blend operation. Error code: {}", status);
    }

    return status;
}

dsp_status dsp_blend(dsp_device device,
                     const dsp_image_properties_t *image,
                     const dsp_overlay_properties_t overlays[],
                     size_t overlays_count)
{
    return dsp_blend_perf(device, image, overlays, overlays_count, NULL);
}
