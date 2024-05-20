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

#define IDMA_TEST_BUFFER_SIZE (0x100)

#define MAX_PLANES (4)
#define MAX_BLEND_OVERLAYS (50)
#define MAX_BLUR_ROIS (80)
#define MAX_PRIVACY_MASK_ROIS (8)
#define MAX_MULTI_RESIZE_CROPS (260)
#define MAX_MULTI_RESIZE_OUTPUTS (7)
#define MAX_AFFINE_TRANSFORMATION_CROPS (80)
#define MAX_MULTI_CROP_ROIS (80)

#define IDMA_TEST_NSID "idmaidmaidmaidma"
#define IDMA_PERF_TEST_NSID "perfidmaperfidma"
#define MPU_TEST_NSID "mpumpumpumputest"
#define IDMA_LOOKUP_TEST_NSID "lookupidmalookup"
#define FIREWALL_TEST_NSID "firewallfirewall"
#define IMAGING_NSID "imagingnamespace"
#define UTILIZATION_NSID "utilizationcheck"
#define DDR_TRAFFIC_NSID "ddrddrddrtraffic"

typedef enum {
    IMAGING_OP_CROP_AND_RESIZE,
    IMAGING_OP_BLEND,
    IMAGING_OP_BLUR,
    IMAGING_OP_CONVERT_FORMAT,
    IMAGING_OP_DEWARP,
    IMAGING_OP_MULTI_CROP_AND_RESIZE,
    IMAGING_OP_MULTI_CROP_AND_RESIZE_PRIVACY_MASK,
    IMAGING_OP_ROT_DIS_DEWARP,
    IMAGING_OP_MULTI_AFFINE_TRANSFORM,
    IMAGING_OP_MULTI_CROP,
    IMAGING_OPS_COUNT,
} imaging_operation_t;

enum dsp_interface_image_format {
    INTERFACE_IMAGE_FORMAT_GRAY8,
    INTERFACE_IMAGE_FORMAT_RGB,
    INTERFACE_IMAGE_FORMAT_NV12,
    INTERFACE_IMAGE_FORMAT_A420,
    INTERFACE_IMAGE_FORMAT_ARGB,
};

/** Letterbox aligment types */
typedef enum {
    INTERFACE_NO_LETTERBOX = 0,  /**< No letterbox resizing is performed */
    INTERFACE_LETTERBOX_MIDDLE,  /**< Align the image to the middle */
    INTERFACE_LETTERBOX_UP_LEFT, /**< Align the image up or left */
} interface_letterbox_alignment_t;

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
    uint32_t start_x;
    uint32_t start_y;
    uint32_t end_x;
    uint32_t end_y;
} roi_in_data_t;

typedef struct {
    data_plane_t bitmask;
    uint8_t y_color;
    uint8_t u_color;
    uint8_t v_color;
    roi_in_data_t rois[MAX_PRIVACY_MASK_ROIS];
    uint32_t rois_count;
} privacy_mask_in_data_t;

/** Color parameters */
typedef union {
    struct {
        uint8_t gray;
    };
    struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };
    struct {
        uint8_t y;
        uint8_t u;
        uint8_t v;
    };
} color_t;

/** Letterbox properties */
typedef struct {
    /** Image alignment */
    interface_letterbox_alignment_t alignment;
    /** The color used for letterboxes */
    color_t color;
} letterbox_properties_t;

typedef struct {
    image_properties_t src;
    image_properties_t dst;
    roi_in_data_t crop;
    uint8_t interpolation;
    letterbox_properties_t letterbox;
} crop_resize_in_data_t;

typedef struct {
    roi_in_data_t crop;
    image_properties_t dst[MAX_MULTI_RESIZE_OUTPUTS];
    uint8_t dst_count;
} multi_resize_in_data_t;

typedef struct {
    image_properties_t src;
    image_properties_t helper;
    multi_resize_in_data_t multi_resizes[MAX_MULTI_RESIZE_CROPS];
    uint8_t multi_resizes_count;
    uint8_t interpolation;
    privacy_mask_in_data_t privacy_mask;
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
    uint8_t interpolation;
} dewarp_in_data_t;

typedef struct {
    float a11;
    float a12;
    float a13;
    float a21;
    float a22;
    float a23;
} affine_transform_matrix;

typedef struct {
    roi_in_data_t params;
    image_properties_t dst;
    affine_transform_matrix affine_matrix;
} multi_warp_affine_cropped_part_t;

typedef struct {
    image_properties_t src;
    multi_warp_affine_cropped_part_t crops[MAX_AFFINE_TRANSFORMATION_CROPS];
    uint32_t crops_count;
    uint8_t interpolation;
} multi_warp_affine_params_t;

typedef struct {
    dewarp_in_data_t dewarp_args;
    uint32_t dsp_vsm_hoffset;
    uint32_t dsp_vsm_voffset;
    uint32_t dsp_vsm_width;
    uint32_t dsp_vsm_height;
    uint32_t dsp_vsm_max_displacement;
    data_plane_t prev_rows_sum;
    data_plane_t prev_columns_sum;
    data_plane_t cur_rows_sum;
    data_plane_t cur_columns_sum;
    uint32_t isp_center_x;
    uint32_t isp_center_y;
    int32_t isp_dx;
    int32_t isp_dy;
    float maximum_theta;
    float alpha;
    float prev_angles_sum;
    float prev_traj;
    uint8_t do_mesh_correction;
} rot_dis_dewarp_in_data_t;

typedef struct {
    image_properties_t src;
    image_properties_t dst;
    roi_in_data_t src_roi;
    roi_in_data_t dst_roi;
} crop_in_data_t;

typedef struct {
    crop_in_data_t crops[MAX_MULTI_CROP_ROIS];
    uint32_t crops_count;
} multi_crop_in_data_t;

typedef struct {
    int32_t operation;
    union {
        crop_resize_in_data_t crop_and_resize_args;
        blend_in_data_t blend_args;
        blur_in_data_t blur_args;
        convert_format_in_data_t convert_format_args;
        dewarp_in_data_t dewarp_args;
        multi_crop_resize_in_data_t multi_crop_and_resize_args;
        rot_dis_dewarp_in_data_t rot_dis_dewarp_args;
        multi_warp_affine_params_t multi_affine_transform_args;
        multi_crop_in_data_t multi_crop_args;
    };
} imaging_request_t;

typedef struct {
    uint32_t utilization;
} utilization_response_t;

typedef struct {
    uint32_t get_arg_params_context;
    uint32_t process_tiles_total;
    uint32_t process_tiles_setup;
    uint32_t kernel;
    uint32_t in_dma_wait;
    uint32_t out_dma_wait;
    uint32_t setup_updates_tiles;
    uint32_t pad_edges;
    uint32_t ref_tile_setup;
    uint32_t in_dma_config;
    uint32_t out_dma_config;
    uint32_t tiles_count;
} fik_perf_into_t;

typedef struct {
    uint32_t y_plane_process_and_dma;
    uint32_t uv_plane_process_and_dma;
    uint32_t mesh_dma;
    uint32_t dma_y_in_wait;
    uint32_t dma_y_out_wait;
    uint32_t y_padding;
    uint32_t y_coord_and_coeff_calc;
    uint32_t y_process;
} dewarp_perf_info_t;

typedef struct {
    uint32_t vsm_calculation_window;
    uint32_t vsm_calculation_rows;
    uint32_t vsm_calculation_cols;
    uint32_t mesh_correction;
    uint32_t dewarp;
} rot_dis_dewarp_perf_info_t;

typedef struct {
    uint32_t xrp_handler;
    fik_perf_into_t fik;
    dewarp_perf_info_t dewarp;
    rot_dis_dewarp_perf_info_t rot_dis_dewarp;
} perf_info_t;

typedef struct {
    perf_info_t perf_info;
    float cur_angles_sum;
    float cur_traj;
    float stabilized_theta;
} rot_dis_dewarp_response_t;

typedef enum {
    DDR_TRAFFIC_READ,
    DDR_TRAFFIC_WRITE,
} ddr_traffic_operation_t;

typedef struct {
    int32_t operation;
    uint64_t base_address;
    uint8_t use_random_pattern;
    uint16_t pattern;
} ddr_traffic_request_t;

#endif //_USER_DSP_INTERFACE_H