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

/************************************************************************************
 *  This file is used for defintions that are shared between
 *  user space application and dsp firmware.
 *  Each component has a copy of this header
 * WHEN UPDATING THIS FILE, MAKE SURE TO UPDATE ALL COPIES
 ************************************************************************************/

#ifndef _USER_DSP_INTERFACE_H
#define _USER_DSP_INTERFACE_H

#include <stdint.h>

#define MAX_PLANES (4)
#define MAX_BLEND_OVERLAYS (50)
#define MAX_BLUR_ROIS (80)
#define INTERFACE_MULTI_RESIZE_OUTPUTS_COUNT (5)

#define IDMA_TEST_NSID "idmaidmaidmaidma"
#define IDMA_PERF_TEST_NSID "perfidmaperfidma"
#define MPU_TEST_NSID "mpumpumpumputest"
#define IDMA_LOOKUP_TEST_NSID "lookupidmalookup"
#define IMAGING_NSID "imagingnamespace"
#define IDMA_TEST_BUFFER_SIZE (0x100)

typedef enum {
    IMAGING_OP_CROP_AND_RESIZE,
    IMAGING_OP_BLEND,
    IMAGING_OP_BLUR,
    IMAGING_OP_CONVERT_FORMAT,
    IMAGING_OP_DEWARP,
    IMAGING_OP_MULTI_CROP_AND_RESIZE,
} imaging_operation_t;

enum dsp_interface_image_format {
    INTERFACE_IMAGE_FORMAT_GRAY8,
    INTERFACE_IMAGE_FORMAT_RGB,
    INTERFACE_IMAGE_FORMAT_NV12,
    INTERFACE_IMAGE_FORMAT_A420,
};

typedef struct {
    uint32_t xrp_buffer_index;
    uint32_t line_stride; // in bytes
    uint32_t plane_size;  // in bytes
} data_plane_t;

typedef struct {
    uint32_t width;  // number of pixels for each row
    uint32_t height; // number of pixels for each column
    data_plane_t planes[MAX_PLANES];
    uint32_t planes_count;
    uint32_t format;
} image_properties_t;

typedef struct {
    image_properties_t src;
    image_properties_t dst;
    uint32_t crop_start_x;
    uint32_t crop_start_y;
    uint32_t crop_end_x;
    uint32_t crop_end_y;
    uint8_t interpolation;
} crop_resize_in_data_t;

typedef struct {
    image_properties_t src;
    image_properties_t dst[INTERFACE_MULTI_RESIZE_OUTPUTS_COUNT];
    uint32_t crop_start_x;
    uint32_t crop_start_y;
    uint32_t crop_end_x;
    uint32_t crop_end_y;
    uint8_t dst_count;
    uint8_t interpolation;
} multi_crop_resize_in_data_t;

typedef struct {
    image_properties_t overlay;
    uint32_t x_offset;
    uint32_t y_offset;
} overlay_in_data_t;

typedef struct {
    image_properties_t background;
    overlay_in_data_t overlays[MAX_BLEND_OVERLAYS];
    uint32_t overlays_count;
} blend_in_data_t;

typedef struct {
    uint32_t start_x;
    uint32_t start_y;
    uint32_t end_x;
    uint32_t end_y;
} roi_in_data_t;

typedef struct {
    image_properties_t image;
    roi_in_data_t rois[MAX_BLUR_ROIS];
    uint32_t rois_count;
    uint32_t kernel_size;
} blur_in_data_t;

typedef struct {
    image_properties_t src;
    image_properties_t dst;
} convert_format_in_data_t;

typedef struct {
    image_properties_t src;
    image_properties_t dst;
    data_plane_t mesh;
    uint32_t mesh_width;
    uint32_t mesh_height;
    uint32_t mesh_sq_size;
    uint8_t interpolation;
} dewarp_in_data_t;

typedef struct {
    int32_t operation;
    union {
        crop_resize_in_data_t crop_and_resize_args;
        blend_in_data_t blend_args;
        blur_in_data_t blur_args;
        convert_format_in_data_t convert_format_args;
        dewarp_in_data_t dewarp_args;
        multi_crop_resize_in_data_t multi_crop_and_resize_args;
    };
} imaging_request_t;

typedef struct {
    uint32_t xrp_handler;
    uint32_t get_arg_params_context;
    uint32_t process_tiles_total;
    uint32_t process_tiles_setup;
    uint32_t kernel;
    uint32_t dma_wait;
    uint32_t setup_updates_tiles;
    uint32_t pad_edges;
    uint32_t ref_tile_setup;
    uint32_t in_dma_config;
    uint32_t out_dma_config;
    uint32_t tiles_count;
} perf_info_t;

#endif //_USER_DSP_INTERFACE_H