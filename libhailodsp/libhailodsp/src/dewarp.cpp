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

#define MESH_SQ_SIZE (64)

static dsp_status verify_mesh_properties(const dsp_dewarp_mesh_t *mesh, const dsp_image_properties_t *dst)
{
    if (mesh->mesh_width == 0) {
        LOGGER__ERROR("Error: mesh width is 0\n");
        return DSP_INVALID_ARGUMENT;
    }

    if (mesh->mesh_height == 0) {
        LOGGER__ERROR("Error: mesh height is 0\n");
        return DSP_INVALID_ARGUMENT;
    }

    if (mesh->mesh_table == NULL) {
        LOGGER__ERROR("Error: mesh table pointer is NULL\n");
        return DSP_INVALID_ARGUMENT;
    }

    auto minimum_mesh_width = (dst->width / MESH_SQ_SIZE) + (dst->width % MESH_SQ_SIZE == 0 ? 0 : 1);
    if (mesh->mesh_width < minimum_mesh_width) {
        LOGGER__ERROR("Error: mesh width is too small. Minimum mesh width: {}\n", minimum_mesh_width);
        return DSP_INVALID_ARGUMENT;
    }

    auto minimum_mesh_height = (dst->height / MESH_SQ_SIZE) + (dst->height % MESH_SQ_SIZE == 0 ? 0 : 1);
    if (mesh->mesh_height < minimum_mesh_height) {
        LOGGER__ERROR("Error: mesh height is too small. Minimum mesh height: {}\n", minimum_mesh_height);
        return DSP_INVALID_ARGUMENT;
    }

    return DSP_SUCCESS;
}

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

    status = verify_mesh_properties(mesh, dst);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Mesh properties check failed\n");
        return status;
    }

    if (src->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Src format ({}) is not supported\n", src->format);
        return DSP_INVALID_ARGUMENT;
    }

    if (dst->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Dst format ({}) is not supported\n", dst->format);
        return DSP_INVALID_ARGUMENT;
    }

    if ((interpolation != INTERPOLATION_TYPE_BILINEAR) && (interpolation != INTERPOLATION_TYPE_BICUBIC)) {
        LOGGER__ERROR("Error: Interpolation type ({}) not supported\n", interpolation);
        return DSP_INVALID_ARGUMENT;
    }

    size_t mesh_line_stride = mesh->mesh_width * 2 * 4;
    size_t mesh_size = mesh_line_stride * mesh->mesh_height;

    auto in_data = make_aligned_uptr<imaging_request_t>();
    in_data->operation = IMAGING_OP_DEWARP;
    in_data->dewarp_args.interpolation = interpolation;
    in_data->dewarp_args.mesh_width = mesh->mesh_width;
    in_data->dewarp_args.mesh_height = mesh->mesh_height;
    in_data->dewarp_args.mesh.plane_size = mesh_size;
    in_data->dewarp_args.mesh.line_stride = mesh_line_stride;

    std::vector<command_image_t> images = {
        {
            .user_api_image = src,
            .dsp_api_image = &in_data->dewarp_args.src,
            .access_type = BufferAccessType::Read,
        },
        {
            .user_api_image = dst,
            .dsp_api_image = &in_data->dewarp_args.dst,
            .access_type = BufferAccessType::Write,
        },
    };

    BufferList buffer_list;
    in_data->dewarp_args.mesh.xrp_buffer_index =
        buffer_list.add_buffer(mesh->mesh_table, mesh_size, BufferAccessType::Read);
    status = add_images_to_buffer_list(buffer_list, images);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed adding images to buffer list. Error code: {}\n", status);
        return status;
    }

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;

    status = send_command(device, buffer_list, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size);
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