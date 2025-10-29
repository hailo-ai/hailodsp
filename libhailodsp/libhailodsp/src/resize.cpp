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
#include "hailo/hailodsp.h"
#include "image_utils.hpp"
#include "logger_macros.hpp"
#include "resize_perf.h"
#include "send_command.hpp"
#include "thread_local_buffer.hpp"
#include "user_dsp_interface.h"

#include <utility>
#include <utils.h>

#define CROP_RESIZE_MAX_SCALE (16)
#define MULTI_CROP_RESIZE_MAX_SCALE (32)

static_assert(static_cast<int>(DSP_NO_LETTERBOX) == static_cast<int>(INTERFACE_NO_LETTERBOX),
              "Interface and internal implementation must have the same number for no letterbox");
static_assert(static_cast<int>(DSP_LETTERBOX_MIDDLE) == static_cast<int>(INTERFACE_LETTERBOX_MIDDLE),
              "Interface and internal implementation must have the same number for letterbox middle");
static_assert(static_cast<int>(DSP_LETTERBOX_UP_LEFT) == static_cast<int>(INTERFACE_LETTERBOX_UP_LEFT),
              "Interface and internal implementation must have the same number for letterbox up_left");

static_assert(static_cast<int>(DSP_SCALING_MODE_STRETCH) == static_cast<int>(INTERFACE_SCALING_MODE_STRECH),
              "dsp_scaling_mode_t must be identical to interface_scaling_mode_t");
static_assert(static_cast<int>(DSP_SCALING_MODE_LETTERBOX_MIDDLE) ==
                  static_cast<int>(INTERFACE_SCALING_MODE_LETTERBOX_MIDDLE),
              "dsp_scaling_mode_t must be identical to interface_scaling_mode_t");
static_assert(static_cast<int>(DSP_SCALING_MODE_LETTERBOX_UP_LEFT) ==
                  static_cast<int>(INTERFACE_SCALING_MODE_LETTERBOX_UP_LEFT),
              "dsp_scaling_mode_t must be identical to interface_scaling_mode_t");
static_assert(static_cast<int>(DSP_SCALING_MODE_SCALE_AND_CROP) ==
                  static_cast<int>(INTERFACE_SCALING_MODE_SCALE_AND_CROP),
              "dsp_scaling_mode_t must be identical to interface_scaling_mode_t");

// This function assumes that "image" params is already checked for correctness
static dsp_status verify_privacy_mask_params(const dsp_image_properties_t *image,
                                             const dsp_privacy_mask_t *privacy_mask_params,
                                             bool is_multi_crop_resize)
{
    // privacy mask is optional
    if (!privacy_mask_params) {
        return DSP_SUCCESS;
    }

    if (!image || !privacy_mask_params->bitmask) {
        LOGGER__ERROR("Error: NULL argument (image={}, privacy_mask_params->bitmask={})", fmt::ptr(image),
                      fmt::ptr(privacy_mask_params->bitmask));
        return DSP_INVALID_ARGUMENT;
    }

    if (privacy_mask_params->rois_count == 0) {
        LOGGER__ERROR("Error: Must have at least 1 ROI");
        return DSP_INVALID_ARGUMENT;
    }

    if (privacy_mask_params->rois_count > MAX_STATIC_PRIVACY_MASKS) {
        LOGGER__ERROR("Error: Too many ROIs. The operation supports up to {} ROIs", MAX_STATIC_PRIVACY_MASKS);
        return DSP_INVALID_ARGUMENT;
    }

    if (privacy_mask_params->type >= DSP_PRIVACY_MASK_COUNT) {
        LOGGER__ERROR("Error: Unknown privacy mask type {}", privacy_mask_params->type);
        return DSP_INVALID_ARGUMENT;
    }

    if (privacy_mask_params->type == DSP_PRIVACY_MASK_BLUR) {
        if (is_multi_crop_resize) {
            LOGGER__ERROR("Error: Blur privacy mask is not supported for multi crop resize");
            return DSP_INVALID_ARGUMENT;
        }

        if (privacy_mask_params->blur_radius < 1 ||
            privacy_mask_params->blur_radius > DSP_BLUR_PRIVACY_MASK_MAX_RADIUS) {
            LOGGER__ERROR("Error: blur_radius ({}) must be in the range [1, 64]", privacy_mask_params->blur_radius);
            return DSP_INVALID_ARGUMENT;
        }

        if (privacy_mask_params->blur_radius % 2 != 0) {
            LOGGER__ERROR("Error: blur_radius ({}) must be an even number", privacy_mask_params->blur_radius);
            return DSP_INVALID_ARGUMENT;
        }
    }

    for (size_t i = 0; i < privacy_mask_params->rois_count; ++i) {
        dsp_roi_t *roi_params = &privacy_mask_params->rois[i];
        bool invalid_roi = false;

        if (roi_params->start_x >= roi_params->end_x) {
            LOGGER__ERROR("Error: ROI start_x ({}) must be smaller then end_x ({})", roi_params->start_x,
                          roi_params->end_x);
            invalid_roi = true;
        }

        if (roi_params->start_y >= roi_params->end_y) {
            LOGGER__ERROR("Error: ROI start_y ({}) must be smaller then end_y ({})", roi_params->start_y,
                          roi_params->end_y);
            invalid_roi = true;
        }

        const size_t bitmask_width = ceil(image->width / static_cast<float>(PRIVACY_MASK_QUANTIZATION));
        if (roi_params->end_x > bitmask_width) {
            LOGGER__ERROR("Error: ROI end_x ({}) must be smaller or equal to quantized bitmask width ({})",
                          roi_params->end_x, bitmask_width);
            invalid_roi = true;
        }

        const size_t bitmask_height = ceil(image->height / static_cast<float>(PRIVACY_MASK_QUANTIZATION));
        if (roi_params->end_y > bitmask_height) {
            LOGGER__ERROR("Error: ROI end_y ({}) must be smaller or equal to quantized bitmask height ({})",
                          roi_params->end_y, bitmask_height);
            invalid_roi = true;
        }

        if (invalid_roi) {
            LOGGER__ERROR("Error: ROI properties check failed for \"roi[{}]\"", i);
            return DSP_INVALID_ARGUMENT;
        }
    }

    return DSP_SUCCESS;
}

static std::pair<size_t, size_t> expand_to_aspect_ratio(size_t src_width,
                                                        size_t src_height,
                                                        size_t dst_width,
                                                        size_t dst_height)
{
    float src_aspect_ratio = static_cast<float>(src_width) / src_height;
    float dst_aspect_ratio = static_cast<float>(dst_width) / dst_height;

    size_t new_width, new_height;
    if (dst_aspect_ratio > src_aspect_ratio) {
        // adjust height to maintain aspect ratio
        new_width = dst_width;
        new_height = std::ceil(dst_width / src_aspect_ratio);
    } else {
        // adjust width to maintain aspect ratio
        new_width = std::ceil(dst_height * src_aspect_ratio);
        new_height = dst_height;
    }

    new_width = ROUND_UP(new_width, 2);
    new_height = ROUND_UP(new_height, 2);

    return {new_width, new_height};
}

static std::pair<size_t, size_t> shrink_to_aspect_ratio(size_t src_width,
                                                        size_t src_height,
                                                        size_t dst_width,
                                                        size_t dst_height)
{
    float src_aspect_ratio = static_cast<float>(src_width) / src_height;
    float dst_aspect_ratio = static_cast<float>(dst_width) / dst_height;

    size_t new_width, new_height;
    if (dst_aspect_ratio < src_aspect_ratio) {
        // truncate height to maintain aspect ratio
        new_width = dst_width;
        new_height = std::ceil(dst_width / src_aspect_ratio);
    } else {
        // truncate width to maintain aspect ratio
        new_width = std::ceil(dst_height * src_aspect_ratio);
        new_height = dst_height;
    }

    new_width = ROUND_UP(new_width, 2);
    new_height = ROUND_UP(new_height, 2);

    return {new_width, new_height};
}

static std::pair<size_t, size_t> adjust_to_aspect_ratio(size_t src_width,
                                                        size_t src_height,
                                                        size_t dst_width,
                                                        size_t dst_height,
                                                        dsp_scaling_mode_t scaling_mode)
{
    if (scaling_mode == DSP_SCALING_MODE_SCALE_AND_CROP) {
        return expand_to_aspect_ratio(src_width, src_height, dst_width, dst_height);
    } else if (scaling_mode == DSP_SCALING_MODE_LETTERBOX_MIDDLE ||
               scaling_mode == DSP_SCALING_MODE_LETTERBOX_UP_LEFT) {
        return shrink_to_aspect_ratio(src_width, src_height, dst_width, dst_height);
    } else {
        return {dst_width, dst_height};
    }
}

static dsp_status verify_resize_scaling(size_t src_width,
                                        size_t src_height,
                                        size_t dst_width,
                                        size_t dst_height,
                                        size_t max_scale,
                                        dsp_scaling_mode_t scaling_mode)
{
    if (scaling_mode >= DSP_SCALING_MODE_COUNT) {
        LOGGER__ERROR("Error: Unknown scaling mode {}", scaling_mode);
        return DSP_INVALID_ARGUMENT;
    }

    auto [width, height] = adjust_to_aspect_ratio(src_width, src_height, dst_width, dst_height, scaling_mode);

    if (width * max_scale < src_width) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst width({}) can be at most {} "
            "times smaller than src width({})",
            width, max_scale, src_width);
        return DSP_INVALID_ARGUMENT;
    }

    if (height * max_scale < src_height) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst height({}) can be at most {} "
            "times smaller than src height({})",
            height, max_scale, src_height);
        return DSP_INVALID_ARGUMENT;
    }

    if (width > src_width * max_scale) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst width({}) can be at most {} "
            "times larger than src width({})",
            width, max_scale, src_width);
        return DSP_INVALID_ARGUMENT;
    }

    if (height > src_height * max_scale) {
        LOGGER__ERROR(
            "Error: Resize scale is too large, dst height({}) can be at most {} "
            "times larger than src height({})",
            height, max_scale, src_height);
        return DSP_INVALID_ARGUMENT;
    }

    return DSP_SUCCESS;
}

static dsp_status verify_resize_scaling(size_t src_width,
                                        size_t src_height,
                                        size_t dst_width,
                                        size_t dst_height,
                                        size_t max_scale,
                                        const dsp_letterbox_properties_t *letterbox_params)
{
    if (letterbox_params) {
        // assert that the casting is correct
        static_assert(static_cast<int>(DSP_NO_LETTERBOX) == static_cast<int>(DSP_SCALING_MODE_STRETCH),
                      "DSP_NO_LETTERBOX must be identical to DSP_SCALING_MODE_STRETCH");
        static_assert(static_cast<int>(DSP_LETTERBOX_MIDDLE) == static_cast<int>(DSP_SCALING_MODE_LETTERBOX_MIDDLE),
                      "DSP_LETTERBOX_MIDDLE must be identical to DSP_SCALING_MODE_LETTERBOX_MIDDLE");
        static_assert(static_cast<int>(DSP_LETTERBOX_UP_LEFT) == static_cast<int>(DSP_SCALING_MODE_LETTERBOX_UP_LEFT),
                      "DSP_LETTERBOX_UP_LEFT must be identical to DSP_SCALING_MODE_LETTERBOX_UP_LEFT");

        return verify_resize_scaling(src_width, src_height, dst_width, dst_height, max_scale,
                                     static_cast<dsp_scaling_mode_t>(letterbox_params->alignment));
    } else {
        return verify_resize_scaling(src_width, src_height, dst_width, dst_height, max_scale, DSP_SCALING_MODE_STRETCH);
    }
}

static bool are_all_rois_same(const dsp_multi_crop_resize_params_t *params)
{
    const dsp_roi_t *first = params->crop_resize_params[0].crop;
    if (!first) {
        LOGGER__ERROR("Error: First crop is NULL");
        return false;
    }

    for (size_t i = 1; i < params->crop_resize_params_count; ++i) {
        if (!rois_equal(first, params->crop_resize_params[i].crop)) {
            return false;
        }
    }

    return true;
}

static dsp_status verify_image_enhancement_params(const dsp_image_enhancement_params_t *image_enhancement_params,
                                                  const dsp_multi_crop_resize_params_t *mutli_crop_resize_params)
{
    if (!image_enhancement_params) {
        // image enhancement params are optional
        return DSP_SUCCESS;
    }

    if (image_enhancement_params->blur.level > 4) {
        LOGGER__ERROR("Error: blur level ({}) must be in the range [0, 4]", image_enhancement_params->blur.level);
        return DSP_INVALID_ARGUMENT;
    }

    if (image_enhancement_params->sharpness.level > 3) {
        LOGGER__ERROR("Error: sharpness level  ({}) must be in the range [0, 3]",
                      image_enhancement_params->sharpness.level);
        return DSP_INVALID_ARGUMENT;
    }

    if (image_enhancement_params->color.brightness < -128 || image_enhancement_params->color.brightness > 128) {
        LOGGER__ERROR("Error: brightness ({}) must be in the range [-128, 128]",
                      image_enhancement_params->color.brightness);
        return DSP_INVALID_ARGUMENT;
    }

    if (image_enhancement_params->color.saturation_u_b < -128 || image_enhancement_params->color.saturation_u_b > 128) {
        LOGGER__ERROR("Error: saturation_u_b ({}) must be in the range [-128, 128]",
                      image_enhancement_params->color.saturation_u_b);
        return DSP_INVALID_ARGUMENT;
    }

    if (image_enhancement_params->color.saturation_v_b < -128 || image_enhancement_params->color.saturation_v_b > 128) {
        LOGGER__ERROR("Error: saturation_v_b ({}) must be in the range [-128, 128]",
                      image_enhancement_params->color.saturation_v_b);
        return DSP_INVALID_ARGUMENT;
    }

    if (image_enhancement_params->histogram_params && mutli_crop_resize_params->crop_resize_params_count > 1 &&
        !are_all_rois_same(mutli_crop_resize_params)) {
        LOGGER__ERROR(
            "Error: Histogram is supported only for a single crop or if the ROIs are the same across all crops");
        return DSP_INVALID_ARGUMENT;
    }

    return DSP_SUCCESS;
}

static dsp_status verify_image_enhancement_histogram_params(
    const dsp_image_enhancement_params_t *image_enhancement_params,
    const dsp_image_properties_t *image)
{
    if (!image_enhancement_params || !image_enhancement_params->histogram_params) {
        // image enhancement and histogram params are optional
        return DSP_SUCCESS;
    }

    if (image_enhancement_params->histogram_params->x_sample_step >= image->width) {
        LOGGER__ERROR("Error: X sample step ({}) must be smaller than image width ({})",
                      image_enhancement_params->histogram_params->x_sample_step, image->width);
        return DSP_INVALID_ARGUMENT;
    }

    if (image_enhancement_params->histogram_params->y_sample_step >= image->height) {
        LOGGER__ERROR("Error: Y sample step ({}) must be smaller than image height ({})",
                      image_enhancement_params->histogram_params->y_sample_step, image->height);
        return DSP_INVALID_ARGUMENT;
    }

    return DSP_SUCCESS;
}

/* Telescopic is only valid if each resolution is less than or equal to the previous one in both width and height */
dsp_status verify_telescopic_resolutions(dsp_crop_resize_params_t *params)
{
    int prev_idx = 0;
    int i = 0;

    /* Find the first resolution in the list */
    for (i = 0; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; i++) {
        if (params->dst[i] != nullptr) {
            prev_idx = i;
            break;
        }
    }

    if (i == DSP_MULTI_RESIZE_OUTPUTS_COUNT) {
        LOGGER__ERROR("Error: At least one image destination must be provided");
        return DSP_INVALID_ARGUMENT;
    }

    /* Check that each resolution is less than or equal to the previous one */
    for (i = prev_idx + 1; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; i++) {
        if (params->dst[i] == nullptr) {
            continue;
        }

        auto src_width = params->crop->end_x - params->crop->start_x;
        auto src_height = params->crop->end_y - params->crop->start_y;
        auto [prev_width, prev_height] =
            adjust_to_aspect_ratio(src_width, src_height, params->dst[prev_idx]->width, params->dst[prev_idx]->height,
                                   params->scaling_params[prev_idx].scaling_mode);
        auto [width, height] = adjust_to_aspect_ratio(src_width, src_height, params->dst[i]->width,
                                                      params->dst[i]->height, params->scaling_params[i].scaling_mode);
        if (width > prev_width) {
            LOGGER__ERROR(
                "Error: Resolution {} width {} (scaled from {} using {}) must be lower or equal to "
                "previous resolution {} width {} (scaled from {} using {})",
                i, width, params->dst[i]->width, scaling_mode_to_string(params->scaling_params[i].scaling_mode),
                prev_idx, prev_width, params->dst[prev_idx]->width,
                scaling_mode_to_string(params->scaling_params[prev_idx].scaling_mode));
            return DSP_INVALID_ARGUMENT;
        }
        if (height > prev_height) {
            LOGGER__ERROR(
                "Error: Resolution {} height {} (scaled from {} using {}) must be lower or equal to "
                "previous resolution {} height {} (scaled from {} using {})",
                i, height, params->dst[i]->height, scaling_mode_to_string(params->scaling_params[i].scaling_mode),
                prev_idx, prev_height, params->dst[prev_idx]->height,
                scaling_mode_to_string(params->scaling_params[prev_idx].scaling_mode));
            return DSP_INVALID_ARGUMENT;
        }

        prev_idx = i;
    }

    return DSP_SUCCESS;
}

static dsp_status verify_and_convert_flip_rotate_params(const dsp_flip_rotate_params_t *flip_rotate_params,
                                                        interface_flip_rotate_t &dsp_flip_rotate)
{
    if (!flip_rotate_params) {
        // flip rotate params are optional
        dsp_flip_rotate = INTERFACE_FLIP_ROTATE_NONE;
        return DSP_SUCCESS;
    }

    switch (flip_rotate_params->rot_ang) {
        case DSP_ROTATION_ANGLE_0:
            switch (flip_rotate_params->flip_dir) {
                case DSP_FLIP_DIRECTION_NONE:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_NONE;
                    break;
                case DSP_FLIP_DIRECTION_HORIZONTAL:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_HORIZONTAL;
                    break;
                case DSP_FLIP_DIRECTION_VERTICAL:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_VERTICAL;
                    break;
                case DSP_FLIP_DIRECTION_BOTH:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_180;
                    break;
                default:
                    LOGGER__ERROR("Flip direction {} is not supported", flip_rotate_params->flip_dir);
                    return DSP_INVALID_ARGUMENT;
            }
            break;
        case DSP_ROTATION_ANGLE_90:
            switch (flip_rotate_params->flip_dir) {
                case DSP_FLIP_DIRECTION_NONE:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_90;
                    break;
                case DSP_FLIP_DIRECTION_HORIZONTAL:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_VERTICAL_90;
                    break;
                case DSP_FLIP_DIRECTION_VERTICAL:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_VERTICAL_270;
                    break;
                case DSP_FLIP_DIRECTION_BOTH:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_270;
                    break;
                default:
                    LOGGER__ERROR("Flip direction {} is not supported", flip_rotate_params->flip_dir);
                    return DSP_INVALID_ARGUMENT;
            }
            break;
        case DSP_ROTATION_ANGLE_180:
            switch (flip_rotate_params->flip_dir) {
                case DSP_FLIP_DIRECTION_NONE:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_180;
                    break;
                case DSP_FLIP_DIRECTION_HORIZONTAL:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_VERTICAL;
                    break;
                case DSP_FLIP_DIRECTION_VERTICAL:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_HORIZONTAL;
                    break;
                case DSP_FLIP_DIRECTION_BOTH:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_NONE;
                    break;
                default:
                    LOGGER__ERROR("Flip direction {} is not supported", flip_rotate_params->flip_dir);
                    return DSP_INVALID_ARGUMENT;
            }
            break;

        case DSP_ROTATION_ANGLE_270:
            switch (flip_rotate_params->flip_dir) {
                case DSP_FLIP_DIRECTION_NONE:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_270;
                    break;
                case DSP_FLIP_DIRECTION_HORIZONTAL:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_VERTICAL_270;
                    break;
                case DSP_FLIP_DIRECTION_VERTICAL:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_VERTICAL_90;
                    break;
                case DSP_FLIP_DIRECTION_BOTH:
                    dsp_flip_rotate = INTERFACE_FLIP_ROTATE_90;
                    break;
                default:
                    LOGGER__ERROR("Flip direction {} is not supported", flip_rotate_params->flip_dir);
                    return DSP_INVALID_ARGUMENT;
            }
            break;
        default:
            LOGGER__ERROR("Rotation angle {} is not supported", flip_rotate_params->rot_ang);
            return DSP_INVALID_ARGUMENT;
    }
    return DSP_SUCCESS;
}

static void dump_telescopic_parameters(const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
                                       const dsp_privacy_mask_t *privacy_mask_params,
                                       const dsp_image_enhancement_params_t *image_enhancement_params,
                                       const dsp_flip_rotate_params_t *flip_rotate_params)
{
    LOGGER__ERROR("Parameters provided:");
    LOGGER__ERROR("  - src: width={}, height={}", mutli_crop_resize_params->src->width,
                  mutli_crop_resize_params->src->height);
    LOGGER__ERROR("  - interpolation: {}", mutli_crop_resize_params->interpolation);

    for (size_t i = 0; i < mutli_crop_resize_params->crop_resize_params_count; ++i) {
        const auto &crop_resize = mutli_crop_resize_params->crop_resize_params[i];
        LOGGER__ERROR("  - crop_resize[{}]: crop(start_x={}, start_y={}, end_x={}, end_y={})", i,
                      crop_resize.crop->start_x, crop_resize.crop->start_y, crop_resize.crop->end_x,
                      crop_resize.crop->end_y);
        for (size_t j = 0; j < DSP_MULTI_RESIZE_OUTPUTS_COUNT; ++j) {
            if (crop_resize.dst[j]) {
                LOGGER__ERROR("    - dst[{}]: width={}, height={}, scaling_mode={}", j, crop_resize.dst[j]->width,
                              crop_resize.dst[j]->height, crop_resize.scaling_params[j].scaling_mode);
            }
        }
    }

    if (privacy_mask_params) {
        LOGGER__ERROR("  - privacy_mask: type={}, rois_count={}", privacy_mask_params->type,
                      privacy_mask_params->rois_count);
        if (privacy_mask_params->type == DSP_PRIVACY_MASK_COLOR) {
            LOGGER__ERROR("    - color: y={}, u={}, v={}", privacy_mask_params->color.y, privacy_mask_params->color.u,
                          privacy_mask_params->color.v);
        } else if (privacy_mask_params->type == DSP_PRIVACY_MASK_BLUR) {
            LOGGER__ERROR("    - blur_radius: {}", privacy_mask_params->blur_radius);
        }
        for (size_t i = 0; i < privacy_mask_params->rois_count; ++i) {
            const auto &roi = privacy_mask_params->rois[i];
            LOGGER__ERROR("    - roi[{}]: start_x={}, start_y={}, end_x={}, end_y={}", i, roi.start_x, roi.start_y,
                          roi.end_x, roi.end_y);
        }
    }

    if (image_enhancement_params) {
        LOGGER__ERROR("  - image_enhancement:");
        LOGGER__ERROR("    - blur: level={}", image_enhancement_params->blur.level);
        LOGGER__ERROR("    - bilateral: enabled={}, sigma_color={}", image_enhancement_params->bilateral.enabled,
                      image_enhancement_params->bilateral.sigma_color);
        LOGGER__ERROR("    - sharpness: level={}, amount={}, threshold={}", image_enhancement_params->sharpness.level,
                      image_enhancement_params->sharpness.amount, image_enhancement_params->sharpness.threshold);
        LOGGER__ERROR(
            "    - color: contrast={}, brightness={}, saturation_u_a={}, saturation_u_b={},"
            " saturation_v_a={}, saturation_v_b={}",
            image_enhancement_params->color.contrast, image_enhancement_params->color.brightness,
            image_enhancement_params->color.saturation_u_a, image_enhancement_params->color.saturation_u_b,
            image_enhancement_params->color.saturation_v_a, image_enhancement_params->color.saturation_v_b);
        if (image_enhancement_params->histogram_params) {
            LOGGER__ERROR("    - histogram: x_sample_step={}, y_sample_step={}",
                          image_enhancement_params->histogram_params->x_sample_step,
                          image_enhancement_params->histogram_params->y_sample_step);
        }
        if (image_enhancement_params->histogram_equalization_params) {
            LOGGER__ERROR("    - histogram_equalization: enabled=true");
        }
    }

    if (flip_rotate_params) {
        LOGGER__ERROR("  - flip_rotate: flip_dir={}, rot_ang={}", flip_rotate_params->flip_dir,
                      flip_rotate_params->rot_ang);
    }
}

dsp_status dsp_crop_and_resize_perf(dsp_device device,
                                    const dsp_resize_params_t *resize_params,
                                    const dsp_roi_t *crop_params,
                                    const dsp_letterbox_properties_t *letterbox_params,
                                    perf_info_t *perf_info)
{
    if ((!device) || (!resize_params)) {
        LOGGER__ERROR("Error: NULL argument (device={}, resize_params={})", fmt::ptr(device), fmt::ptr(resize_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(IMAGING_OP_CROP_AND_RESIZE);
    auto status = verify_crop_params(resize_params->src, crop_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Crop parameters check failed");
        return status;
    }

    dsp_image_properties_t cropped_src = *resize_params->src;
    cropped_src.width = crop_params->end_x - crop_params->start_x;
    cropped_src.height = crop_params->end_y - crop_params->start_y;

    status = verify_image_properties(&cropped_src);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"src\" (after crop)");
        return status;
    }

    status = verify_image_properties(resize_params->dst);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image properties check failed for \"dst\"");
        return status;
    }

    // Skip scaling check for FHD to 64x64 in Bilinear or Nearest, as it is a special case that is allowed
    bool skip_scaling_check = ((cropped_src.width == 1920) && (cropped_src.height == 1080) &&
                               (resize_params->dst->width == 64) && (resize_params->dst->height == 64) &&
                               ((resize_params->interpolation == INTERPOLATION_TYPE_NEAREST_NEIGHBOR) ||
                                (resize_params->interpolation == INTERPOLATION_TYPE_BILINEAR)));
    if (!skip_scaling_check) {
        status = verify_resize_scaling(cropped_src.width, cropped_src.height, resize_params->dst->width,
                                       resize_params->dst->height, CROP_RESIZE_MAX_SCALE, letterbox_params);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image resize scale check failed");
            return status;
        }
    }

    if (resize_params->interpolation >= INTERPOLATION_TYPE_COUNT) {
        LOGGER__ERROR("Error: Unknown interpolation type {}", resize_params->interpolation);
        return DSP_INVALID_ARGUMENT;
    }

    // Only same format for src and dst is currently supported and only GRAY8, RGB, BGR or NV12
    if (resize_params->src->format != resize_params->dst->format) {
        LOGGER__ERROR("Error: The src and dst formats are not the same");
        return DSP_INVALID_ARGUMENT;
    }

    switch (resize_params->src->format) {
        case DSP_IMAGE_FORMAT_GRAY8:
        case DSP_IMAGE_FORMAT_RGB:
        case DSP_IMAGE_FORMAT_BGR:
        case DSP_IMAGE_FORMAT_NV12:
            break;

        default:
            LOGGER__ERROR("Error: The src/dst format ({}) is not supported",
                          format_arg_to_string(resize_params->src->format));
            return DSP_INVALID_ARGUMENT;
    }

    if ((resize_params->interpolation == INTERPOLATION_TYPE_AREA) &&
        ((cropped_src.width < resize_params->dst->width) || (cropped_src.height < resize_params->dst->height))) {
        LOGGER__ERROR("Error: Area interpolation does not support upscaling");
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }

    in_data->operation = IMAGING_OP_CROP_AND_RESIZE;
    in_data->crop_and_resize_args.interpolation = resize_params->interpolation;
    in_data->crop_and_resize_args.crop.start_x = crop_params->start_x;
    in_data->crop_and_resize_args.crop.start_y = crop_params->start_y;
    in_data->crop_and_resize_args.crop.end_x = crop_params->end_x;
    in_data->crop_and_resize_args.crop.end_y = crop_params->end_y;
    in_data->crop_and_resize_args.letterbox.alignment = INTERFACE_NO_LETTERBOX;
    if (letterbox_params && letterbox_params->alignment != DSP_NO_LETTERBOX) {
        in_data->crop_and_resize_args.letterbox.alignment =
            static_cast<interface_letterbox_alignment_t>(letterbox_params->alignment);
        memcpy(&in_data->crop_and_resize_args.letterbox.color, &letterbox_params->color,
               sizeof(in_data->crop_and_resize_args.letterbox.color));
    }

    std::vector<command_image_t> images = {
        {
            .user_api_image = resize_params->src,
            .dsp_api_image = &in_data->crop_and_resize_args.src,
            .access_type = BufferAccessType::Read,
        },
        {
            .user_api_image = resize_params->dst,
            .dsp_api_image = &in_data->crop_and_resize_args.dst,
            .access_type = BufferAccessType::Write,
        },
    };

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status =
        send_command(device, images, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size, timeframe);
    tracer.update_event_timestamp_now(timeframe, DSP_TASK_FINISHED_EVENT);

    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}", status);
    }

    return status;
}

dsp_status dsp_resize_perf(dsp_device device,
                           const dsp_resize_params_t *resize_params,
                           const dsp_letterbox_properties_t *letterbox_params,
                           perf_info_t *perf_info)
{
    if (!resize_params) {
        LOGGER__ERROR("Error: resize_params is NULL");
        return DSP_INVALID_ARGUMENT;
    }

    if (!resize_params->src) {
        LOGGER__ERROR("Error: resize_params->src is NULL");
        return DSP_INVALID_ARGUMENT;
    }

    dsp_roi_t crop_params = {
        .start_x = 0,
        .start_y = 0,
        .end_x = resize_params->src->width,
        .end_y = resize_params->src->height,
    };

    return dsp_crop_and_resize_perf(device, resize_params, &crop_params, letterbox_params, perf_info);
}

dsp_status dsp_crop_and_resize(dsp_device device,
                               const dsp_resize_params_t *resize_params,
                               const dsp_roi_t *crop_params)
{
    return dsp_crop_and_resize_perf(device, resize_params, crop_params, NULL, NULL);
}

dsp_status dsp_crop_and_resize_letterbox(dsp_device device,
                                         const dsp_resize_params_t *resize_params,
                                         const dsp_roi_t *crop_params,
                                         const dsp_letterbox_properties_t *letterbox_params)
{
    if (!crop_params) {
        return dsp_resize_perf(device, resize_params, letterbox_params, NULL);
    } else {
        return dsp_crop_and_resize_perf(device, resize_params, crop_params, letterbox_params, NULL);
    }
}

dsp_status dsp_resize(dsp_device device, const dsp_resize_params_t *resize_params)
{
    return dsp_resize_perf(device, resize_params, NULL, NULL);
}

dsp_status dsp_multi_crop_and_resize_perf(dsp_device device,
                                          const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
                                          const dsp_privacy_mask_t *privacy_mask_params,
                                          perf_info_t *perf_info)
{
    if ((!device) || (!mutli_crop_resize_params)) {
        LOGGER__ERROR("Error: NULL argument (device={}, mutli_crop_resize_params={})", fmt::ptr(device),
                      fmt::ptr(mutli_crop_resize_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(IMAGING_OP_MULTI_CROP_AND_RESIZE);

    auto status = verify_privacy_mask_params(mutli_crop_resize_params->src, privacy_mask_params, true);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Privacy mask parameters check failed");
        return status;
    }

    if (mutli_crop_resize_params->src->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Src format ({}) is not supported",
                      format_arg_to_string(mutli_crop_resize_params->src->format));
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }

    in_data->multi_crop_and_resize_args.privacy_mask.enabled = false;
    in_data->multi_crop_and_resize_args.image_enhancement.enabled = false;
    in_data->multi_crop_and_resize_args.image_enhancement.histogram.enabled = false;
    in_data->multi_crop_and_resize_args.multi_resizes_count = mutli_crop_resize_params->crop_resize_params_count;
    in_data->operation = IMAGING_OP_MULTI_CROP_AND_RESIZE;

    if ((mutli_crop_resize_params->interpolation != INTERPOLATION_TYPE_BILINEAR) &&
        (mutli_crop_resize_params->interpolation != INTERPOLATION_TYPE_BICUBIC)) {
        LOGGER__ERROR("Error: interpolation type ({}) not supported", mutli_crop_resize_params->interpolation);
        return DSP_INVALID_ARGUMENT;
    }

    in_data->multi_crop_and_resize_args.interpolation = mutli_crop_resize_params->interpolation;

    std::vector<command_image_t> images;
    images.reserve(1 + MAX_MULTI_RESIZE_OUTPUTS * mutli_crop_resize_params->crop_resize_params_count);
    images.emplace_back(command_image_t{
        .user_api_image = mutli_crop_resize_params->src,
        .dsp_api_image = &in_data->multi_crop_and_resize_args.src,
        .access_type = BufferAccessType::Read,
    });

    for (size_t j = 0; j < mutli_crop_resize_params->crop_resize_params_count; j++) {
        auto crop_and_resize_params = &mutli_crop_resize_params->crop_resize_params[j];
        auto crop_and_resize_args = &in_data->multi_crop_and_resize_args.multi_resizes[j];

        auto status = verify_crop_params(mutli_crop_resize_params->src, crop_and_resize_params->crop);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Crop parameters check failed for crop&resize[{}]", j);
            return status;
        }

        dsp_image_properties_t cropped_src = *mutli_crop_resize_params->src;
        cropped_src.width = crop_and_resize_params->crop->end_x - crop_and_resize_params->crop->start_x;
        cropped_src.height = crop_and_resize_params->crop->end_y - crop_and_resize_params->crop->start_y;

        status = verify_image_properties(&cropped_src);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for src (after crop)");
            return status;
        }

        size_t dst_count = 0;
        for (int i = 0; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; ++i) {
            auto dst_image = crop_and_resize_params->dst[i];
            if (dst_image == NULL) {
                continue;
            }

            status = verify_image_properties(dst_image);
            if (status != DSP_SUCCESS) {
                LOGGER__ERROR("Error: Image properties check failed for dst[{}, {}]", j, i);
                return status;
            }

            if (dst_image->format != DSP_IMAGE_FORMAT_NV12) {
                LOGGER__ERROR("Error: Dst[{} {}] format ({}) is not supported\n", j, i,
                              format_arg_to_string(dst_image->format));
                return DSP_INVALID_ARGUMENT;
            }

            status = verify_resize_scaling(cropped_src.width, cropped_src.height, dst_image->width, dst_image->height,
                                           MULTI_CROP_RESIZE_MAX_SCALE, DSP_SCALING_MODE_STRETCH);
            if (status != DSP_SUCCESS) {
                LOGGER__ERROR("Error: Image resize scale check failed for dst[{}, {}]", j, i);
                return status;
            }

            images.emplace_back(command_image_t{
                .user_api_image = dst_image,
                .dsp_api_image = &crop_and_resize_args->dst[dst_count],
                .access_type = BufferAccessType::Write,
            });

            dst_count++;
        }

        crop_and_resize_args->dst_count = dst_count;
        crop_and_resize_args->crop.start_x = crop_and_resize_params->crop->start_x;
        crop_and_resize_args->crop.start_y = crop_and_resize_params->crop->start_y;
        crop_and_resize_args->crop.end_x = crop_and_resize_params->crop->end_x;
        crop_and_resize_args->crop.end_y = crop_and_resize_params->crop->end_y;
    }

    BufferList buffer_list;
    status = add_images_to_buffer_list(buffer_list, images);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed adding images to buffer list. Error code: {}", status);
        return status;
    }

    if (privacy_mask_params) {
        in_data->multi_crop_and_resize_args.privacy_mask.enabled = true;
        const size_t bitmask_width =
            ceil(mutli_crop_resize_params->src->width / static_cast<double>(PRIVACY_MASK_QUANTIZATION * 8));
        const size_t bitmask_stride = ROUND_UP(bitmask_width, 8);
        const size_t bitmask_height =
            ceil(mutli_crop_resize_params->src->height / static_cast<double>(PRIVACY_MASK_QUANTIZATION));
        in_data->multi_crop_and_resize_args.privacy_mask.bitmask.line_stride = bitmask_stride;
        in_data->multi_crop_and_resize_args.privacy_mask.bitmask.plane_size = bitmask_stride * bitmask_height;
        in_data->multi_crop_and_resize_args.privacy_mask.bitmask.xrp_buffer_index = buffer_list.add_buffer(
            privacy_mask_params->bitmask, bitmask_stride * bitmask_height, BufferAccessType::Read);

        in_data->multi_crop_and_resize_args.privacy_mask.color.y = privacy_mask_params->color.y;
        in_data->multi_crop_and_resize_args.privacy_mask.color.u = privacy_mask_params->color.u;
        in_data->multi_crop_and_resize_args.privacy_mask.color.v = privacy_mask_params->color.v;

        in_data->multi_crop_and_resize_args.privacy_mask.rois_count = privacy_mask_params->rois_count;
        for (size_t i = 0; i < privacy_mask_params->rois_count; ++i) {
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].start_x = privacy_mask_params->rois[i].start_x;
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].start_y = privacy_mask_params->rois[i].start_y;
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].end_x = privacy_mask_params->rois[i].end_x;
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].end_y = privacy_mask_params->rois[i].end_y;
        }
    }

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status = send_command(device, buffer_list, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size,
                          timeframe);
    tracer.update_event_timestamp_now(timeframe, DSP_TASK_FINISHED_EVENT);

    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}", status);
    }

    return status;
}

dsp_status dsp_multi_crop_and_resize(dsp_device device, const dsp_multi_crop_resize_params_t *mutli_crop_resize_params)
{
    return dsp_multi_crop_and_resize_perf(device, mutli_crop_resize_params, NULL, NULL);
}

dsp_status dsp_multi_crop_and_resize_privacy_mask(dsp_device device,
                                                  const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
                                                  const dsp_privacy_mask_t *privacy_mask_params)
{
    return dsp_multi_crop_and_resize_perf(device, mutli_crop_resize_params, privacy_mask_params, NULL);
}

dsp_status dsp_telescopic_multi_crop_and_resize_perf(dsp_device device,
                                                     const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
                                                     const dsp_privacy_mask_t *privacy_mask_params,
                                                     const dsp_image_enhancement_params_t *image_enhancement_params,
                                                     const dsp_flip_rotate_params_t *flip_rotate_params,
                                                     perf_info_t *perf_info)
{
    if ((!device) || (!mutli_crop_resize_params)) {
        LOGGER__ERROR("Error: NULL argument (device={}, mutli_crop_resize_params={})", fmt::ptr(device),
                      fmt::ptr(mutli_crop_resize_params));
        return DSP_INVALID_ARGUMENT;
    }

    auto imaging_op = IMAGING_OP_TELESCOPIC_MULTI_CROP_AND_RESIZE;

    auto &tracer = DspTracer::get_instance();
    task_timeframe_t *timeframe = tracer.start_tracing(imaging_op);

    auto status = verify_privacy_mask_params(mutli_crop_resize_params->src, privacy_mask_params, false);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Privacy mask parameters check failed");
        return status;
    }

    status = verify_image_enhancement_params(image_enhancement_params, mutli_crop_resize_params);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Image enhancement parameters check failed");
        return status;
    }

    if (mutli_crop_resize_params->src->format != DSP_IMAGE_FORMAT_NV12) {
        LOGGER__ERROR("Error: Src format ({}) is not supported",
                      format_arg_to_string(mutli_crop_resize_params->src->format));
        return DSP_INVALID_ARGUMENT;
    }

    if (mutli_crop_resize_params->interpolation != INTERPOLATION_TYPE_BILINEAR) {
        LOGGER__ERROR("Error: Interpolation type ({}) not supported", mutli_crop_resize_params->interpolation);
        return DSP_INVALID_ARGUMENT;
    }

    auto in_data = ThreadLocalBuffer<imaging_request_t>::get();
    if (!in_data) {
        LOGGER__ERROR("Failed to allocate in_data");
        return DSP_CREATE_BUFFER_FAILED;
    }

    in_data->multi_crop_and_resize_args.privacy_mask.enabled = false;
    in_data->multi_crop_and_resize_args.image_enhancement.enabled = false;
    in_data->multi_crop_and_resize_args.image_enhancement.histogram.enabled = false;
    in_data->multi_crop_and_resize_args.image_enhancement.histogram_equalization.enabled = false;
    in_data->multi_crop_and_resize_args.flip_rotate = INTERFACE_FLIP_ROTATE_NONE;
    in_data->multi_crop_and_resize_args.multi_resizes_count = mutli_crop_resize_params->crop_resize_params_count;
    in_data->multi_crop_and_resize_args.interpolation = mutli_crop_resize_params->interpolation;
    in_data->operation = imaging_op;

    status = verify_and_convert_flip_rotate_params(
        flip_rotate_params,
        reinterpret_cast<interface_flip_rotate_t &>(in_data->multi_crop_and_resize_args.flip_rotate));
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Flip rotate parameters check failed");
        return status;
    }
    bool is_rotated = flip_rotate_params && (flip_rotate_params->rot_ang == DSP_ROTATION_ANGLE_90 ||
                                             flip_rotate_params->rot_ang == DSP_ROTATION_ANGLE_270);

    std::vector<command_image_t> images;
    images.reserve(1 + MAX_MULTI_RESIZE_OUTPUTS * mutli_crop_resize_params->crop_resize_params_count);
    images.emplace_back(command_image_t{
        .user_api_image = mutli_crop_resize_params->src,
        .dsp_api_image = &in_data->multi_crop_and_resize_args.src,
        .access_type = BufferAccessType::Read,
    });

    for (size_t j = 0; j < mutli_crop_resize_params->crop_resize_params_count; j++) {
        auto crop_and_resize_params = &mutli_crop_resize_params->crop_resize_params[j];
        auto crop_and_resize_args = &in_data->multi_crop_and_resize_args.multi_resizes[j];

        // crop params are matching the input image post flip rotate transformation
        // when rotate 90 or 270, the input image dimension are rotated with respect to the crop params
        // so we rotate the crop params before verifying them
        dsp_roi_t nonrotated_crop = *crop_and_resize_params->crop;
        if (is_rotated) {
            std::swap(nonrotated_crop.start_x, nonrotated_crop.start_y);
            std::swap(nonrotated_crop.end_x, nonrotated_crop.end_y);
        }
        status = verify_crop_params(mutli_crop_resize_params->src, &nonrotated_crop);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Crop parameters check failed for crop&resize[{}]", j);
            return status;
        }

        dsp_image_properties_t nonrotated_cropped_src = *mutli_crop_resize_params->src;
        nonrotated_cropped_src.width = nonrotated_crop.end_x - nonrotated_crop.start_x;
        nonrotated_cropped_src.height = nonrotated_crop.end_y - nonrotated_crop.start_y;

        status = verify_image_properties(&nonrotated_cropped_src);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image properties check failed for src[{}] (after crop)", j);
            return status;
        }

        status = verify_telescopic_resolutions(crop_and_resize_params);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Telescopic resolutions check failed for crop&resize[{}]", j);
            return status;
        }

        status = verify_image_enhancement_histogram_params(image_enhancement_params, &nonrotated_cropped_src);
        if (status != DSP_SUCCESS) {
            LOGGER__ERROR("Error: Image enhancement histogram params check failed for crop[{}]", j);
            return status;
        }

        size_t dst_count = 0;
        for (int i = 0; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; ++i) {
            auto dst_image = crop_and_resize_params->dst[i];
            if (dst_image == NULL) {
                continue;
            }

            status = verify_image_properties(dst_image);
            if (status != DSP_SUCCESS) {
                LOGGER__ERROR("Error: Image properties check failed for dst[{}, {}]", j, i);
                return status;
            }

            if (dst_image->format != DSP_IMAGE_FORMAT_NV12) {
                LOGGER__ERROR("Error: Dst[{} {}] format ({}) is not supported", j, i,
                              format_arg_to_string(dst_image->format));
                return DSP_INVALID_ARGUMENT;
            }

            status = verify_resize_scaling(crop_and_resize_params->crop->end_x - crop_and_resize_params->crop->start_x,
                                           crop_and_resize_params->crop->end_y - crop_and_resize_params->crop->start_y,
                                           dst_image->width, dst_image->height, MULTI_CROP_RESIZE_MAX_SCALE,
                                           crop_and_resize_params->scaling_params[i].scaling_mode);
            if (status != DSP_SUCCESS) {
                LOGGER__ERROR("Error: Image resize scale check failed for dst[{}, {}]", j, i);
                return status;
            }

            crop_and_resize_args->scaling_params[dst_count].mode =
                crop_and_resize_params->scaling_params[i].scaling_mode;

            crop_and_resize_args->scaling_params[dst_count].color.y = crop_and_resize_params->scaling_params[i].color.y;
            crop_and_resize_args->scaling_params[dst_count].color.u = crop_and_resize_params->scaling_params[i].color.u;
            crop_and_resize_args->scaling_params[dst_count].color.v = crop_and_resize_params->scaling_params[i].color.v;

            images.emplace_back(command_image_t{
                .user_api_image = dst_image,
                .dsp_api_image = &crop_and_resize_args->dst[dst_count],
                .access_type = BufferAccessType::Write,
            });

            dst_count++;
        }

        crop_and_resize_args->dst_count = dst_count;
        crop_and_resize_args->crop.start_x = crop_and_resize_params->crop->start_x;
        crop_and_resize_args->crop.start_y = crop_and_resize_params->crop->start_y;
        crop_and_resize_args->crop.end_x = crop_and_resize_params->crop->end_x;
        crop_and_resize_args->crop.end_y = crop_and_resize_params->crop->end_y;
    }

    BufferList buffer_list;
    status = add_images_to_buffer_list(buffer_list, images);
    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed adding images to buffer list. Error code: {}", status);
        return status;
    }

    if (privacy_mask_params) {
        in_data->multi_crop_and_resize_args.privacy_mask.enabled = true;
        const size_t bitmask_width =
            ceil(mutli_crop_resize_params->src->width / static_cast<double>(PRIVACY_MASK_QUANTIZATION * 8));
        const size_t bitmask_stride = ROUND_UP(bitmask_width, 8);
        const size_t bitmask_height =
            ceil(mutli_crop_resize_params->src->height / static_cast<double>(PRIVACY_MASK_QUANTIZATION));
        in_data->multi_crop_and_resize_args.privacy_mask.bitmask.line_stride = bitmask_stride;
        in_data->multi_crop_and_resize_args.privacy_mask.bitmask.plane_size = bitmask_stride * bitmask_height;
        in_data->multi_crop_and_resize_args.privacy_mask.bitmask.xrp_buffer_index = buffer_list.add_buffer(
            privacy_mask_params->bitmask, bitmask_stride * bitmask_height, BufferAccessType::Read);

        in_data->multi_crop_and_resize_args.privacy_mask.type =
            static_cast<privacy_mask_type_in_data_t>(privacy_mask_params->type);
        if (privacy_mask_params->type == DSP_PRIVACY_MASK_COLOR) {
            in_data->multi_crop_and_resize_args.privacy_mask.color.y = privacy_mask_params->color.y;
            in_data->multi_crop_and_resize_args.privacy_mask.color.u = privacy_mask_params->color.u;
            in_data->multi_crop_and_resize_args.privacy_mask.color.v = privacy_mask_params->color.v;
        } else {
            in_data->multi_crop_and_resize_args.privacy_mask.blur_radius = privacy_mask_params->blur_radius;
        }

        in_data->multi_crop_and_resize_args.privacy_mask.rois_count = privacy_mask_params->rois_count;
        for (size_t i = 0; i < privacy_mask_params->rois_count; ++i) {
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].start_x = privacy_mask_params->rois[i].start_x;
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].start_y = privacy_mask_params->rois[i].start_y;
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].end_x = privacy_mask_params->rois[i].end_x;
            in_data->multi_crop_and_resize_args.privacy_mask.rois[i].end_y = privacy_mask_params->rois[i].end_y;
        }
    }

    if (image_enhancement_params) {
        in_data->multi_crop_and_resize_args.image_enhancement.enabled = true;
        in_data->multi_crop_and_resize_args.image_enhancement.blur = image_enhancement_params->blur.level;
        in_data->multi_crop_and_resize_args.image_enhancement.bilateral.enabled =
            image_enhancement_params->bilateral.enabled;
        in_data->multi_crop_and_resize_args.image_enhancement.bilateral.sigma_color =
            image_enhancement_params->bilateral.sigma_color;
        in_data->multi_crop_and_resize_args.image_enhancement.sharpness = image_enhancement_params->sharpness.level;
        in_data->multi_crop_and_resize_args.image_enhancement.amount = image_enhancement_params->sharpness.amount;
        in_data->multi_crop_and_resize_args.image_enhancement.threshold = image_enhancement_params->sharpness.threshold;
        in_data->multi_crop_and_resize_args.image_enhancement.contrast = image_enhancement_params->color.contrast;
        in_data->multi_crop_and_resize_args.image_enhancement.brightness = image_enhancement_params->color.brightness;
        in_data->multi_crop_and_resize_args.image_enhancement.saturation_u_a =
            image_enhancement_params->color.saturation_u_a;
        in_data->multi_crop_and_resize_args.image_enhancement.saturation_u_b =
            image_enhancement_params->color.saturation_u_b;
        in_data->multi_crop_and_resize_args.image_enhancement.saturation_v_a =
            image_enhancement_params->color.saturation_v_a;
        in_data->multi_crop_and_resize_args.image_enhancement.saturation_v_b =
            image_enhancement_params->color.saturation_v_b;

        if (image_enhancement_params->histogram_params) {
            in_data->multi_crop_and_resize_args.image_enhancement.histogram.enabled = true;
            in_data->multi_crop_and_resize_args.image_enhancement.histogram.x_sample_step =
                image_enhancement_params->histogram_params->x_sample_step;
            in_data->multi_crop_and_resize_args.image_enhancement.histogram.y_sample_step =
                image_enhancement_params->histogram_params->y_sample_step;
            in_data->multi_crop_and_resize_args.image_enhancement.histogram.histogram.line_stride =
                sizeof(image_enhancement_params->histogram_params->histogram);
            in_data->multi_crop_and_resize_args.image_enhancement.histogram.histogram.plane_size =
                sizeof(image_enhancement_params->histogram_params->histogram);
            in_data->multi_crop_and_resize_args.image_enhancement.histogram.histogram.xrp_buffer_index =
                buffer_list.add_buffer(image_enhancement_params->histogram_params->histogram,
                                       sizeof(image_enhancement_params->histogram_params->histogram),
                                       BufferAccessType::Write);
        }

        if (image_enhancement_params->histogram_equalization_params) {
            in_data->multi_crop_and_resize_args.image_enhancement.histogram_equalization.enabled = true;
            std::memcpy(in_data->multi_crop_and_resize_args.image_enhancement.histogram_equalization.lut,
                        image_enhancement_params->histogram_equalization_params->lut,
                        sizeof(image_enhancement_params->histogram_equalization_params->lut));
        }
    }

    size_t perf_info_size = perf_info ? sizeof(*perf_info) : 0;
    status = send_command(device, buffer_list, in_data.get(), sizeof(imaging_request_t), perf_info, perf_info_size,
                          timeframe);
    tracer.update_event_timestamp_now(timeframe, DSP_TASK_FINISHED_EVENT);

    if (perf_info) {
        tracer.update_perf_info(timeframe, *perf_info);
    }

    if (status != DSP_SUCCESS) {
        LOGGER__ERROR("Error: Failed executing resize operation. Error code: {}", status);
        if (status == DSP_RUN_COMMAND_FAILED) {
            dump_telescopic_parameters(mutli_crop_resize_params, privacy_mask_params, image_enhancement_params,
                                       flip_rotate_params);
        }
    }

    return status;
}

dsp_status dsp_telescopic_multi_crop_and_resize(dsp_device device,
                                                const dsp_multi_crop_resize_params_t *mutli_crop_resize_params)
{
    return dsp_telescopic_multi_crop_and_resize_perf(device, mutli_crop_resize_params, NULL, NULL, NULL, NULL);
}

dsp_status dsp_telescopic_multi_crop_and_resize_privacy_mask(
    dsp_device device,
    const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
    const dsp_privacy_mask_t *privacy_mask_params)
{
    return dsp_telescopic_multi_crop_and_resize_perf(device, mutli_crop_resize_params, privacy_mask_params, NULL, NULL,
                                                     NULL);
}

dsp_status dsp_telescopic_multi_resize_privacy_mask_image_enhancement(
    dsp_device device,
    const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
    const dsp_privacy_mask_t *privacy_mask_params,
    const dsp_image_enhancement_params_t *image_enhancement_params)
{
    return dsp_telescopic_multi_crop_and_resize_perf(device, mutli_crop_resize_params, privacy_mask_params,
                                                     image_enhancement_params, NULL, NULL);
}

dsp_status dsp_frontend_process(dsp_device device, const dsp_frontend_params_t *params)
{
    return dsp_telescopic_multi_crop_and_resize_perf(device, params->multi_crop_resize_params,
                                                     params->privacy_mask_params, params->image_enhancement_params,
                                                     params->flip_rotate_params, NULL);
}