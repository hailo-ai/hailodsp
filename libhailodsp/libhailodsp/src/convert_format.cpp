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
#include "convert_format_perf.h"
#include "hailo/hailodsp.h"
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "send_command.hpp"
#include "user_dsp_interface.h"

#include <cstdio>
#include <memory>
#include <utils.h>

dsp_status dsp_convert_format_perf(dsp_device device,
                                   const dsp_image_properties_t *src,
                                   dsp_image_properties_t *dst,
                                   perf_info_t *perf_info)
{
    if (!device) {
        LOGGER__ERROR("Error: One of the parameters provided is NULL (device={}, src={}, dst={})\n", fmt::ptr(device),
                      fmt::ptr(src), fmt::ptr(dst));
        return DSP_INVALID_ARGUMENT;
    }

    auto status = verify_image_properties(src);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"src\"\n");
        return status;
    }

    status = verify_image_properties(dst);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"dst\"\n");
        return status;
    }

    // Current supported format conversions:
    // RGB -> NV12
    // NV12 -> RGB
    if (!((src->format == DSP_IMAGE_FORMAT_RGB && dst->format == DSP_IMAGE_FORMAT_NV12) ||
          (src->format == DSP_IMAGE_FORMAT_NV12 && dst->format == DSP_IMAGE_FORMAT_RGB))) {
        LOGGER__ERROR("Error: Conversion from src {} to dst {} isn't supported\n", format_arg_to_string(src->format),
                      format_arg_to_string(dst->format));
        return DSP_INVALID_ARGUMENT;
    }

    if (src->width != dst->width || src->height != dst->height) {
        LOGGER__ERROR("Error: The src and dst sizes are not the same\n");
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = make_aligned_uptr<imaging_request_t>();
    in_data->operation = IMAGING_OP_CONVERT_FORMAT;

    command_image_t images[] = {
        {
            .user_api_image = src,
            .dsp_api_image = &in_data->convert_format_args.src,
            .access_flags = XRP_READ,
        },
        {
            .user_api_image = dst,
            .dsp_api_image = &in_data->convert_format_args.dst,
            .access_flags = XRP_WRITE,
        },
    };

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status = send_command(device, images, ARRAY_LENGTH(images), in_data.get(), sizeof(imaging_request_t), perf_info,
                          perf_info_size);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing format conversion operation. Error code: {}\n", status);
    }

    return status;
}

dsp_status dsp_convert_format(dsp_device device, const dsp_image_properties_t *src, dsp_image_properties_t *dst)
{
    return dsp_convert_format_perf(device, src, dst, NULL);
}
