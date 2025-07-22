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
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <hailo/hailodsp.h>

struct image_arguments {
    const char *path;
    size_t width;
    size_t height;
    size_t stride; // 0 means auto
    dsp_image_format_t format;
    dsp_memory_type_t memory_type;
};

dsp_image_properties_t *read_image(dsp_device device, struct image_arguments *args);
dsp_image_properties_t *alloc_image(dsp_device device, struct image_arguments *args);
int write_image_to_file(const char *path, dsp_image_properties_t *image);
void release_image(dsp_device device, dsp_image_properties_t *image);

#define RELEASE_IMAGE(dev, img)      \
    do {                             \
        if (img) {                   \
            release_image(dev, img); \
        }                            \
    } while (0)

#ifdef __cplusplus
}
#endif