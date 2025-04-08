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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <hailo/hailodsp.h>

#include <assert.h>

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define ROUND_UP(N, S) ((((N) + (S) - 1) / (S)) * (S))

#define DIVIDE_AND_CEIL(a, b) (((a) + (b) - 1) / (b))

static const char *format_names[] = {
    [DSP_IMAGE_FORMAT_GRAY8] = "gray8", [DSP_IMAGE_FORMAT_RGB] = "rgb",   [DSP_IMAGE_FORMAT_NV12] = "nv12",
    [DSP_IMAGE_FORMAT_A420] = "a420",   [DSP_IMAGE_FORMAT_ARGB] = "argb", [DSP_IMAGE_FORMAT_GRAY16] = "gray16",
    [DSP_IMAGE_FORMAT_BGR] = "bgr",
};

static_assert(ARRAY_LENGTH(format_names) == DSP_IMAGE_FORMAT_COUNT,
              "format_names length must be identical to dsp_image_format_t count");

static const char *flip_direction_names[] = {
    [DSP_FLIP_DIRECTION_NONE] = "none",
    [DSP_FLIP_DIRECTION_HORIZONTAL] = "horizontal",
    [DSP_FLIP_DIRECTION_VERTICAL] = "vertical",
    [DSP_FLIP_DIRECTION_BOTH] = "both",
};

static_assert(ARRAY_LENGTH(flip_direction_names) == DSP_FLIP_DIRECTION_COUNT,
              "flip_direction_names length must be identical to dsp_flip_direction_t count");

static const char *rotation_angle_names[] = {
    [DSP_ROTATION_ANGLE_0] = "0",
    [DSP_ROTATION_ANGLE_90] = "90",
    [DSP_ROTATION_ANGLE_180] = "180",
    [DSP_ROTATION_ANGLE_270] = "270",
};

static_assert(ARRAY_LENGTH(rotation_angle_names) == DSP_ROTATION_ANGLE_COUNT,
              "rotation_angle_names length must be identical to dsp_rotation_angle_t count");

__attribute__((unused)) static inline const char *format_arg_to_string(dsp_image_format_t format)
{
    if (format < ARRAY_LENGTH(format_names)) {
        return format_names[format];
    } else {
        return "unknown";
    }
}

__attribute__((unused)) static inline const char *flip_direction_to_string(dsp_flip_direction_t direction)
{
    if (direction < ARRAY_LENGTH(flip_direction_names)) {
        return flip_direction_names[direction];
    } else {
        return "unknown";
    }
}

__attribute__((unused)) static inline const char *rotation_angle_to_string(dsp_rotation_angle_t angle)
{
    if (angle < ARRAY_LENGTH(rotation_angle_names)) {
        return rotation_angle_names[angle];
    } else {
        return "unknown";
    }
}

#ifdef __cplusplus
}
#endif