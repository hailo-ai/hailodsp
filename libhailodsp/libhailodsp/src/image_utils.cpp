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

#include "hailo/hailodsp.h"
#include "logger_macros.hpp"
#include "user_dsp_interface.h"

#include <cmath>
#include <cstdio>
#include <utils.h>

static dsp_status convert_image_format(dsp_image_format_t format, enum dsp_interface_image_format *interface_format)
{
    dsp_status status = DSP_UNINITIALIZED;

    enum dsp_interface_image_format format_convert[] = {
        [DSP_IMAGE_FORMAT_GRAY8] = INTERFACE_IMAGE_FORMAT_GRAY8,
        [DSP_IMAGE_FORMAT_RGB] = INTERFACE_IMAGE_FORMAT_RGB,
        [DSP_IMAGE_FORMAT_NV12] = INTERFACE_IMAGE_FORMAT_NV12,
        [DSP_IMAGE_FORMAT_A420] = INTERFACE_IMAGE_FORMAT_A420,
    };

    if ((size_t)format >= ARRAY_LENGTH(format_convert)) {
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    *interface_format = format_convert[(size_t)format];
    status = DSP_SUCCESS;

    return status;
}

dsp_status convert_image(const dsp_image_properties_t *img_src, image_properties_t *img_dst)
{
    dsp_status status = DSP_UNINITIALIZED;

    if (img_src->planes_count > MAX_PLANES) {
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    status = convert_image_format(img_src->format, (dsp_interface_image_format *)&img_dst->format);
    if (status != DSP_SUCCESS) {
        return status;
    }

    img_dst->width = img_src->width;
    img_dst->height = img_src->height;
    img_dst->planes_count = img_src->planes_count;

    for (size_t i = 0; i < img_src->planes_count; ++i) {
        img_dst->planes[i].line_stride = img_src->planes[i].bytesperline;
        img_dst->planes[i].plane_size = img_src->planes[i].bytesused;
    }

    status = DSP_SUCCESS;

    return status;
}

static dsp_status verify_plane(const dsp_image_properties_t *image,
                               const dsp_data_plane_t *plane,
                               size_t bytes_per_pixel,
                               size_t width_ratio,
                               size_t height_ratio,
                               size_t plane_index)
{
    dsp_status status = DSP_UNINITIALIZED;

    if (plane == NULL) {
        LOGGER__ERROR("Error: Plane[{}] is NULL\n", plane_index);
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    if (plane->userptr == NULL) {
        LOGGER__ERROR("Error: Plane[{}] data pointer is NULL\n", plane_index);
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    size_t line_stride = plane->bytesperline;
    if (line_stride < ceil((double)(image->width * bytes_per_pixel) / width_ratio)) {
        LOGGER__ERROR("Error: Plane[{}] line stride ({}) is too small for the image width and image format specified\n",
                      plane_index, line_stride);
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    if (plane->bytesused < ceil((double)(line_stride * image->height) / height_ratio)) {
        LOGGER__ERROR(
            "Error: Plane[{}] size ({}) is too small based on the plane line stride and image height specified\n",
            plane_index, plane->bytesused);
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    status = DSP_SUCCESS;

    return status;
}

static dsp_status verify_yuv420_image(const dsp_image_properties_t *image)
{
    dsp_status status = DSP_UNINITIALIZED;

    if ((image->width % 2 != 0) || (image->height % 2 != 0)) {
        LOGGER__ERROR(
            "Error: In YUV420 based formats (such as NV12, A420), image width and height must be even numbers\n");
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    status = DSP_SUCCESS;

    return status;
}

static dsp_status verify_gray8_image(const dsp_image_properties_t *image)
{
    dsp_status status = DSP_UNINITIALIZED;

    if (image->planes_count != 1) {
        LOGGER__ERROR("Error: Gray8 format should contain 1 plane\n");
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    status = verify_plane(image, &image->planes[0], 1, 1, 1, 0);
    if (status != DSP_SUCCESS) {
        return status;
    }

    return status;
}

static dsp_status verify_rgb_image(const dsp_image_properties_t *image)
{
    dsp_status status = DSP_UNINITIALIZED;

    if (image->planes_count != 1) {
        LOGGER__ERROR("Error: RGB format should contain 1 plane\n");
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    status = verify_plane(image, &image->planes[0], 3, 1, 1, 0);
    if (status != DSP_SUCCESS) {
        return status;
    }

    return status;
}

static dsp_status verify_nv12_image(const dsp_image_properties_t *image)
{
    dsp_status status = DSP_UNINITIALIZED;

    if (image->planes_count != 2) {
        LOGGER__ERROR("Error: NV12 format should contain 2 planes\n");
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    status = verify_yuv420_image(image);
    if (status != DSP_SUCCESS) {
        return status;
    }

    status = verify_plane(image, &image->planes[0], 1, 1, 1, 0);
    if (status != DSP_SUCCESS) {
        return status;
    }

    status = verify_plane(image, &image->planes[1], 2, 2, 2, 1);
    if (status != DSP_SUCCESS) {
        return status;
    }

    return status;
}

static dsp_status verify_a420_image(const dsp_image_properties_t *image)
{
    dsp_status status = DSP_UNINITIALIZED;

    if (image->planes_count != 4) {
        LOGGER__ERROR("Error: A420 format should contain 4 planes\n");
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    status = verify_yuv420_image(image);
    if (status != DSP_SUCCESS) {
        return status;
    }

    status = verify_plane(image, &image->planes[0], 1, 1, 1, 0);
    if (status != DSP_SUCCESS) {
        return status;
    }

    status = verify_plane(image, &image->planes[1], 1, 2, 2, 1);
    if (status != DSP_SUCCESS) {
        return status;
    }

    status = verify_plane(image, &image->planes[2], 1, 2, 2, 2);
    if (status != DSP_SUCCESS) {
        return status;
    }

    status = verify_plane(image, &image->planes[3], 1, 1, 1, 3);
    if (status != DSP_SUCCESS) {
        return status;
    }

    status = DSP_SUCCESS;

    return status;
}

dsp_status verify_image_properties(const dsp_image_properties_t *image)
{
    dsp_status status = DSP_UNINITIALIZED;

    if (image == NULL) {
        LOGGER__ERROR("Error: Pointer to dsp_image_properties_t struct is NULL\n");
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    if (image->width == 0) {
        LOGGER__ERROR("Error: image width is 0\n");
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    if (image->height == 0) {
        LOGGER__ERROR("Error: image height is 0\n");
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    if (image->planes == NULL) {
        LOGGER__ERROR("Error: image planes pointer is NULL\n");
        status = DSP_INVALID_ARGUMENT;
        return status;
    }

    switch (image->format) {
        case DSP_IMAGE_FORMAT_GRAY8:
            status = verify_gray8_image(image);
            break;
        case DSP_IMAGE_FORMAT_RGB:
            status = verify_rgb_image(image);
            break;
        case DSP_IMAGE_FORMAT_NV12:
            status = verify_nv12_image(image);
            break;
        case DSP_IMAGE_FORMAT_A420:
            status = verify_a420_image(image);
            break;
        default:
            LOGGER__ERROR("Error: Unknown image format {}\n", image->format);
            status = DSP_INVALID_ARGUMENT;
            break;
    }

    if (status != DSP_SUCCESS) {
        return status;
    }

    status = DSP_SUCCESS;

    return status;
}
