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
#include "dewarp_perf.h"
#include "hailo/hailodsp.h"
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "send_command.hpp"
#include "user_dsp_interface.h"
#include "utils.h"

#include <stdint.h>
#include <stdio.h>

dsp_status dsp_dewarp_perf(dsp_device device,
                           const dsp_image_properties_t *src,
                           const dsp_image_properties_t *dst,
                           const dsp_dewarp_mesh_t *mesh,
                           dsp_interpolation_type_t interpolation,
                           perf_info_t *perf_info)
{
    if ((!device) || (!src) || (!dst) || (!mesh)) {
        LOGGER__ERROR("Error: One of the parameters provided is NULL (device={}, src={}, dst={}, mesh={})\n",
                      fmt::ptr(device), fmt::ptr(src), fmt::ptr(dst), fmt::ptr(mesh));
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

    // TODO: add more parameters check
    size_t mesh_line_stride = mesh->mesh_width * 2 * 4;
    size_t mesh_size = mesh_line_stride * mesh->mesh_height;

    auto in_data = make_aligned_uptr<imaging_request_t>();
    in_data->operation = IMAGING_OP_DEWARP;
    in_data->dewarp_args.interpolation = interpolation;
    in_data->dewarp_args.mesh_width = mesh->mesh_width;
    in_data->dewarp_args.mesh_height = mesh->mesh_height;
    in_data->dewarp_args.mesh_sq_size = mesh->mesh_sq_size;
    in_data->dewarp_args.mesh.plane_size = mesh_size;
    in_data->dewarp_args.mesh.line_stride = mesh_line_stride;

    command_image_t command_images[] = {
        {
            .user_api_image = src,
            .dsp_api_image = &in_data->dewarp_args.src,
            .access_flags = XRP_READ,
        },
        {
            .user_api_image = dst,
            .dsp_api_image = &in_data->dewarp_args.dst,
            .access_flags = XRP_WRITE,
        },
    };

    auto fill_buffer_group = [&command_images, &in_data, &mesh, mesh_size](dsp_device device,
                                                                           struct xrp_buffer_group *buffer_group) {
        auto status = add_buffer_to_buffer_group(device, mesh->mesh_table, mesh_size, XRP_READ, buffer_group,
                                                 &in_data->dewarp_args.mesh.xrp_buffer_index);
        if (status != DSP_SUCCESS) {
            return status;
        }

        return add_images_to_buffer_group(device, command_images, ARRAY_LENGTH(command_images), buffer_group);
    };

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;

    status =
        send_command(device, fill_buffer_group, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing dewarp operation. Error code: {}\n", status);
    }

    return status;
}

dsp_status dsp_dewarp(dsp_device device,
                      const dsp_image_properties_t *src,
                      const dsp_image_properties_t *dst,
                      const dsp_dewarp_mesh_t *mesh,
                      dsp_interpolation_type_t interpolation)
{
    return dsp_dewarp_perf(device, src, dst, mesh, interpolation, NULL);
}