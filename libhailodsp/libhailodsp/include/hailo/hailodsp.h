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

#ifndef HAILODSP_H
#define HAILODSP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define DSP_MAX_ENUM (INT_MAX)

/**
 *  @defgroup status DSP Status codes
 *  @{
 */

/** HailoDSP return codes */
typedef enum {
    DSP_SUCCESS = 0,                /**< Success - No error */
    DSP_UNINITIALIZED,              /**< No error code was initialized */
    DSP_INVALID_ARGUMENT,           /**< Invalid argument passed to function */
    DSP_OUT_OF_HOST_MEMORY,         /**< Cannot allocate more memory at host */
    DSP_OPEN_DEVICE_FAILED,         /**< Failed opening dsp device */
    DSP_CREATE_QUEUE_FAILED,        /**< Failed creating internal command queue */
    DSP_CREATE_BUFFER_FAILED,       /**< Failed creating/allocating buffer. Usually caused by Kernel Driver error */
    DSP_CREATE_BUFFER_GROUP_FAILED, /**< Failed creating internal buffer group */
    DSP_ADD_BUFFER_GROUP_FAILED,    /**< Failed adding buffer to internal buffer group */
    DSP_RUN_COMMAND_FAILED,         /**< Failed running the requested command. Check Kernel or FW logs */
    DSP_MAP_BUFFER_FAILED,          /**< Failed mapping buffer */
    DSP_UNMAP_BUFFER_FAILED,        /**< Failed unmapping buffer */
    DSP_SYNC_BUFFER_FAILED,         /**< Failed synching buffer */
    DSP_IOCTL_FAILED,               /**< General error for driver communication failure */

    DSP_STATUS_COUNT,                  /* Must be last */
    DSP_STATUS_MAX_ENUM = DSP_MAX_ENUM /**< Max enum value to maintain ABI Integrity */
} dsp_status;

/**
 *  @}
 *
 *  @defgroup group_image_properties Image Properties
 *  @{
 */

typedef enum {
    /** Grayscale format. One plane, each pixel is 8bit */
    DSP_IMAGE_FORMAT_GRAY8,

    /**
     * RGB (packed) format. One plane, each color component is 8bit \n
     * @code
     * +--+--+--+ +--+--+--+
     * |R0|G0|B0| |R1|G1|B1|
     * +--+--+--+ +--+--+--+
     * @endcode
     */
    DSP_IMAGE_FORMAT_RGB,

    /**
     * NV12 Format - semiplanar 4:2:0 YUV with interleaved UV plane. Each component is 8bit \n
     * For NV12 format, the dimensions of the image, both width and height, need to be even numbers \n
     * First plane (Y plane): \n
     * @code
     * +--+--+--+
     * |Y0|Y1|Y2|
     * +--+--+--+
     * @endcode
     * Second plane (UV plane): \n
     * @code
     * +--+--+ +--+--+
     * |U0|V0| |U1|V1|
     * +--+--+ +--+--+
     * @endcode
     */
    DSP_IMAGE_FORMAT_NV12,

    /**
     * A420 Format - planar 4:4:2:0 AYUV. Each component is 8bit \n
     * For A420 format, the dimensions of the image, both width and height, need to be even numbers \n
     * Four planes in the following order: Y plane, U plane, V plane, Alpha plane
     */
    DSP_IMAGE_FORMAT_A420,

    /**
     * ARGB - RGB with alpha channel first (packed) format. One plane, each color component is 8bit \n
     * @code
     * +--+--+--+--+ +--+--+--+--+
     * |A0|R0|G0|B0| |A1|R1|G1|B1| ...
     * +--+--+--+--+ +--+--+--+--+
     * @endcode
     */
    DSP_IMAGE_FORMAT_ARGB,

    /** Grayscale format. One plane, each pixel is 16bit */
    DSP_IMAGE_FORMAT_GRAY16,

    /**
     * BGR (packed) format. One plane, each color component is 8bit \n
     * @code
     * +--+--+--+ +--+--+--+
     * |B0|G0|R0| |B1|G1|R1|
     * +--+--+--+ +--+--+--+
     * @endcode
     */
    DSP_IMAGE_FORMAT_BGR,

    /* Must be last */
    DSP_IMAGE_FORMAT_COUNT,
    /** Max enum value to maintain ABI Integrity */
    DSP_IMAGE_FORMAT_MAX_ENUM = DSP_MAX_ENUM
} dsp_image_format_t;

/** Memory type */
typedef enum {
    /** Represents a memory type that uses user pointers. */
    DSP_MEMORY_TYPE_USERPTR,
    /** Represents a memory type that uses dma-buf file descriptors. */
    DSP_MEMORY_TYPE_DMABUF,

    /* Must be last */
    DSP_MEMORY_TYPE_COUNT,
    /** Max enum value to maintain ABI Integrity */
    DSP_MEMORY_TYPE_MAX_ENUM = DSP_MAX_ENUM
} dsp_memory_type_t;

/** Image plane metadata */
typedef struct {
    union {
        /** When the memory type in the containing struct dsp_image_properties_t is ::DSP_MEMORY_TYPE_USERPTR,
         * this is a userspace pointer to the first pixel in the plane
         * @note Although it is possible to use any buffer that has been mapped to userspace, optimal performance can be
         * achieved if the plane data is physically contiguous. To create a contiguous buffer, the ::dsp_create_buffer
         * function can be used
         */
        void *userptr;
        /** When the memory type in the containing struct dsp_image_properties_t is ::DSP_MEMORY_TYPE_DMABUF,
         * this is a file descriptor associated with a DMABUF buffer */
        int fd;
    };
    /** Distance in bytes between the leftmost pixels in two adjacent lines */
    size_t bytesperline;
    /** Number of bytes occupied by data (payload) in the plane */
    size_t bytesused;
} dsp_data_plane_t;

/** Image metadata */
typedef struct {
    /** Number of pixels in each row */
    size_t width;
    /** Number of pixels for each column */
    size_t height;
    /** Array of planes */
    dsp_data_plane_t *planes;
    /** Number of planes in the #planes array */
    size_t planes_count;
    /** Image format */
    dsp_image_format_t format;
    /** Image planes memory type. For more information, refer to ::dsp_data_plane_t */
    dsp_memory_type_t memory;
} dsp_image_properties_t;

/** Region of Interest (ROI)
 * @brief This struct is used by image processing APIs to specify a region of interest (ROI) in an image.
 * @note All fields are in pixel units
 */
typedef struct {
    /** Offset of the left most pixel in the ROI. valid range: [0, width-1] */
    size_t start_x;
    /** Offset of the up most pixel in the ROI. valid range: [0, height-1] */
    size_t start_y;
    /** Offset of the right most pixel in the ROI. valid range: [1, width] */
    size_t end_x;
    /** Offset of the bottom most pixel in the ROI. valid range: [1, height] */
    size_t end_y;
} dsp_roi_t;

/** Interploation methods */
typedef enum {
    INTERPOLATION_TYPE_NEAREST_NEIGHBOR, /**< Nearest neighbor interpolation */
    INTERPOLATION_TYPE_BILINEAR,         /**< Bilinear interpolation */
    INTERPOLATION_TYPE_AREA,             /**< Area interpolation */
    INTERPOLATION_TYPE_BICUBIC,          /**< Bicubic interpolation */

    /* Must be last */
    INTERPOLATION_TYPE_COUNT,
    /** Max enum value to maintain ABI Integrity */
    INTERPOLATION_TYPE_MAX_ENUM = DSP_MAX_ENUM
} dsp_interpolation_type_t;

/** Letterbox aligment types */
typedef enum {
    DSP_NO_LETTERBOX = 0,  /**< No letterbox resizing is performed */
    DSP_LETTERBOX_MIDDLE,  /**< Align the image to the middle */
    DSP_LETTERBOX_UP_LEFT, /**< Align the image up or left */

    /* Must be last */
    DSP_LETTERBOX_COUNT,
    /** Max enum value to maintain ABI Integrity */
    DSP_LETTERBOX_MAX_ENUM = DSP_MAX_ENUM
} dsp_letterbox_alignment_t;

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
} dsp_color_t;

/** Letterbox properties */
typedef struct {
    /** Image alignment */
    dsp_letterbox_alignment_t alignment;
    /** The color used for letterboxes */
    dsp_color_t color;
} dsp_letterbox_properties_t;

/**
 *  @}
 *
 *  @defgroup group_device_management Device Management API
 *  @{
 */

/** Opaque pointer to dsp_device object. The device object holds state and data required to issue commands */
typedef struct dsp_device_internal *dsp_device;

/**
 * Create new dsp_device object
 *
 * @param[out] device A pointer to a ::dsp_device that receives the allocated device
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 * @note To release a device, call the ::dsp_release_device function with the returned ::dsp_device
 */
dsp_status dsp_create_device(dsp_device *device);

/**
 * Release dsp_device object
 *
 * @param device A ::dsp_device to be released
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_release_device(dsp_device device);

/**
 *  @}
 *  @defgroup buffer_mgnt Buffer Managment API
 *  @{
 */

/**
 * Allocates contiguous physical-memory buffer
 *
 * @param device A ::dsp_device object
 * @param size Required buffer size to allocate
 * @param[out] buffer A pointer to void* that recieves the allocated buffer
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 * @note To release a buffer, call ::dsp_release_buffer with the returned pointer
 */
dsp_status dsp_create_buffer(dsp_device device, size_t size, void **buffer);
/**
 * Free buffer that was allocated using ::dsp_create_buffer
 *
 * @param device A ::dsp_device object
 * @param buffer The buffer to release
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_release_buffer(dsp_device device, void *buffer);

/**
 * @brief Enum representing the synchronization direction for DSP buffers.
 */
typedef enum {
    /** Synchronize the buffer for reading */
    DSP_BUFFER_SYNC_READ = 1,
    /** Synchronize the buffer for writing */
    DSP_BUFFER_SYNC_WRITE,
    /** Synchronize the buffer for both reading and writing */
    DSP_BUFFER_SYNC_RW,
} dsp_sync_direction_t;

/**
 * @brief Synchronizes a DSP buffer before access
 * @details If a buffer was created with ::dsp_create_buffer, and was used by the DSP,
 *          (or any other hardware device external to the CPU), it might be out of sync from the CPU's perspective.
 *          This function ensures that the CPU sees the correct buffer data before access
 * @param buffer A buffer to be synchronized. Must be a buffer created using ::dsp_create_buffer
 * @param direction One of ::dsp_sync_direction_t - depends on the operations performed on the buffer
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_buffer_sync_start(void *buffer, dsp_sync_direction_t direction);

/**
 * @brief Synchronizes a DSP buffer after access
 * @details If a buffer was created with ::dsp_create_buffer, and was used by the CPU,
 *          it might be out of sync from the DSP's perspective (or any other hardware device external to the CPU).
 *          This function ensures that external devices see the correct buffer after CPU access
 * @param buffer A buffer to be synchronized. Must be a buffer created using ::dsp_create_buffer
 * @param direction One of ::dsp_sync_direction_t - depends on the operations performed on the buffer
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_buffer_sync_end(void *buffer, dsp_sync_direction_t direction);

/**
 *  @}
 *  @defgroup resize Crop & Resize API
 *  @note some APIs also support an application of a privacy mask
 *  @{
 */

/** Maximum number of outputs supported for each crop in ::dsp_multi_crop_and_resize */
#define DSP_MULTI_RESIZE_OUTPUTS_COUNT (7)

/** Resize parameters */
typedef struct {
    /** Image metadata for source image. Image data will not change */
    const dsp_image_properties_t *src;
    /** Image metadata for destination image.
     *  Specify the required size in this image metadata.
     *  The operation will write the result to this image data pointer  */
    const dsp_image_properties_t *dst;
    /** Interpolation method to use */
    dsp_interpolation_type_t interpolation;
} dsp_resize_params_t;

/**
 * Crop parameters
 * @note All fields are in pixel units
 * @note This typedef is for backwards compatibility only. Use ::dsp_roi_t instead
 */
typedef dsp_roi_t dsp_crop_api_t;

typedef struct {
    /** Pointer to ::dsp_roi_t with the required crop parameters */
    const dsp_roi_t *crop;
    /** Image metadata for destination images.
     *  Specify the required size in the image metadata.
     *  The operation will write the result to this image data pointer.
     *  Assign NULL on the array entries to reduce the number of outputs.
     */
    const dsp_image_properties_t *dst[DSP_MULTI_RESIZE_OUTPUTS_COUNT];
} dsp_crop_resize_params_t;

/** Multi-Resize parameters */
typedef struct {
    /** Image metadata for source image. Image data will not change */
    const dsp_image_properties_t *src;
    /** Metadata of the crops to perform on the source image, and the resizes to perform on each crop.
     *  An Array of ::dsp_crop_resize_params_t structs
     */
    dsp_crop_resize_params_t *crop_resize_params;
    /** \p crop_resize_params array size
     *  @note Max is 260
     */
    size_t crop_resize_params_count;
    /** Interpolation method to use.
     *  Only ::INTERPOLATION_TYPE_BILINEAR and ::INTERPOLATION_TYPE_BICUBIC are supported. */
    dsp_interpolation_type_t interpolation;
} dsp_multi_crop_resize_params_t;

/** Privacy-Mask parameters */
typedef struct {
    /**
     * Bitmask to specify which pixels are part of the privacy mask and therefore need to be colored
     * (1 = apply privacy mask, 0 = don't apply). The bitmask should cover the entire input image,
     * such that each bit in the bitmask represents 4x4 pixels in the original image
     * e.g. a bitmask for a 4K image should be 120x540 bytes in size
     * @note The stride of the bitmask should be divisible by 8, so padding may be required.
     * This padding is ignored and only the actual bitmask width is used
     */
    uint8_t *bitmask;

    /** YUV color (same color for all polygons) */
    uint8_t y_color;
    uint8_t u_color;
    uint8_t v_color;

    /**
     * Since the bitmask convers the entire input image, it's advisable to specify a ractangular ROI surrounding each
     * Polygon. This will allow the DSP to ignore coloring the pixels outside the ROI, and thus improve performance. For
     * simpler (and slower) usage, it is possible to pass 1 ROI which covers the entire image
     * @note The ROI coordinates are for a 4x4 quantized image
     * @note Supports between 1 and 8 \p rois
     */
    dsp_roi_t *rois;
    size_t rois_count;
} dsp_privacy_mask_t;

/** Structure representing image enhancement parameters for DSP */
typedef struct {
    /** Sharpness level. Values are in the range of [1, 5]. 1 means most blurry, 5 means most sharp. */
    uint8_t sharpness;
    /** Contrast value. 1.0 means no change. */
    float contrast;
    /** Brightness value. Values are in the range of [-128, 128]. 0 means no change. */
    int16_t brightness;
    /** Saturation parameter for U channel. */
    float saturation_u_a;
    /** Saturation parameter for U channel. Valid range is [-128, 128]. */
    int16_t saturation_u_b;
    /** Saturation parameter for V channel. */
    float saturation_v_a;
    /** Saturation parameter for V channel. Valid range is [-128, 128]. */
    int16_t saturation_v_b;
} dsp_image_enhancement_params_t;

/**
 * @brief Perform resize operation
 * @details The function resizes the image down to or up to the specified size
 *          Supported formats of the operation are ::DSP_IMAGE_FORMAT_GRAY8, ::DSP_IMAGE_FORMAT_RGB,
 *          ::DSP_IMAGE_FORMAT_BGR and ::DSP_IMAGE_FORMAT_NV12.
 *          The formats of the src image and dst image must be identical.
 *          The destination image can only be upscaled or downscaled up to a factor of 16.
 * @param device A ::dsp_device object
 * @param resize_params Pointer to ::dsp_resize_params_t with the required resize parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_resize(dsp_device device, const dsp_resize_params_t *resize_params);

/**
 * @brief Perform crop&resize operation
 * @details Perform crop operation on an image and then resize the cropped image to the specified size.
 *          Supported formats of the operation are ::DSP_IMAGE_FORMAT_GRAY8, ::DSP_IMAGE_FORMAT_RGB,
 *          ::DSP_IMAGE_FORMAT_BGR and ::DSP_IMAGE_FORMAT_NV12.
 *          The formats of the src image and dst image must be identical.
 *          The destination image can only be upscaled or downscaled up to a factor of 16.
 * @param device A ::dsp_device object
 * @param resize_params Pointer to ::dsp_resize_params_t with the required resize parameters
 * @param crop_params Pointer to ::dsp_roi_t with the required crop parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_crop_and_resize(dsp_device device,
                               const dsp_resize_params_t *resize_params,
                               const dsp_roi_t *crop_params);

/**
 * @brief Perform letterbox crop&resize operation
 * @details Perform crop operation on an image and then resize the cropped image to the specified size.
 *          The resized image keeps the aspect ratio of the src image.
 *          Supported formats of the operation are ::DSP_IMAGE_FORMAT_GRAY8, ::DSP_IMAGE_FORMAT_RGB,
 *          ::DSP_IMAGE_FORMAT_BGR and ::DSP_IMAGE_FORMAT_NV12.
 *          The formats of the src image and dst image must be identical.
 *          The destination image can only be upscaled or downscaled up to a factor of 16.
 * @param device A ::dsp_device object
 * @param resize_params Pointer to ::dsp_resize_params_t with the required resize parameters
 * @param crop_params Pointer to ::dsp_roi_t with the required crop parameters
 * @param letterbox_params Pointer to ::dsp_letterbox_properties_t with the required letterbox parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_crop_and_resize_letterbox(dsp_device device,
                                         const dsp_resize_params_t *resize_params,
                                         const dsp_roi_t *crop_params,
                                         const dsp_letterbox_properties_t *letterbox_params);

/**
 * @brief Perform multi crop and multi resize operation
 * @details Perform several crop operations on an image and then resize each cropped image to the specified sizes.
 *          Supported formats of the operation is ::DSP_IMAGE_FORMAT_NV12.
 *          The formats of the src image and dst image must be identical.
 *          The destination image can only be upscaled or downscaled up to a factor of 32.
 * @param device A ::dsp_device object
 * @param mutli_crop_resize_params Pointer to ::dsp_multi_crop_resize_params_t with the required resize parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_multi_crop_and_resize(dsp_device device, const dsp_multi_crop_resize_params_t *mutli_crop_resize_params);

/**
 * @brief Apply a privacy mask and perform multi crop&resize operation
 * @details Same as ::dsp_multi_crop_and_resize, but with an additional privacy mask applied to all the output images.
 *          Supported formats of the operation is ::DSP_IMAGE_FORMAT_NV12.
 *          The formats of the src image and dst image must be identical.
 *          The destination image can only be upscaled or downscaled up to a factor of 32.
 * @param device A ::dsp_device object
 * @param mutli_crop_resize_params Pointer to ::dsp_multi_crop_resize_params_t with the required resize parameters
 * @param privacy_mask_params Pointer to ::dsp_privacy_mask_t with the required privacy mask parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_multi_crop_and_resize_privacy_mask(dsp_device device,
                                                  const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
                                                  const dsp_privacy_mask_t *privacy_mask_params);

/**
 * @brief Perform telescopic multi crop and multi resize operation
 * @details Perform several crop operations on an image and then perform telescopic resize each cropped image to the
 *          specified sizes.
 *          Supported formats of the operation is ::DSP_IMAGE_FORMAT_NV12.
 *          The formats of the src image and dst image must be identical.
 *          The destination image can only be upscaled or downscaled up to a factor of 32.
 * @param device A ::dsp_device object
 * @param mutli_crop_resize_params Pointer to ::dsp_multi_crop_resize_params_t with the required resize parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_telescopic_multi_crop_and_resize(dsp_device device,
                                                const dsp_multi_crop_resize_params_t *mutli_crop_resize_params);

/**
 * @brief Apply a privacy mask and perform telescopic multi crop&resize operation
 * @details Same as ::dsp_telescopic_multi_crop_and_resize, but with an additional privacy mask applied to all the
 *          output images.
 *          Supported formats of the operation is ::DSP_IMAGE_FORMAT_NV12.
 *          The formats of the src image and dst image must be identical.
 *          The destination image can only be upscaled or downscaled up to a factor of 32.
 * @param device A ::dsp_device object
 * @param mutli_crop_resize_params Pointer to ::dsp_multi_crop_resize_params_t with the required resize parameters
 * @param privacy_mask_params Pointer to ::dsp_privacy_mask_t with the required privacy mask parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_telescopic_multi_crop_and_resize_privacy_mask(
    dsp_device device,
    const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
    const dsp_privacy_mask_t *privacy_mask_params);

/**
 * @brief Perform image enhancement filters, apply a privacy mask and perform telescopic multi crop&resize operation
 * @details Same as ::dsp_telescopic_multi_crop_and_resize_privacy_mask, but with an additional enhancement filters
 *          applied to all the output images. Supported formats of the operation is ::DSP_IMAGE_FORMAT_NV12. The formats
 *          of the src image and dst image must be identical. The destination image can only be upscaled or downscaled
 *          up to a factor of 32.
 * @param device A ::dsp_device object
 * @param mutli_crop_resize_params Pointer to ::dsp_multi_crop_resize_params_t with the required resize parameters
 * @param privacy_mask_params Pointer to ::dsp_privacy_mask_t with the required privacy mask parameters
 * @param image_enhancement_params Pointer to ::dsp_image_enhancement_params_t with the required image enhancement
 *                                 parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 * @warning This function is for usage by Hailo Media Library and may change on later releases
 */
dsp_status dsp_telescopic_multi_resize_privacy_mask_image_enhancement(
    dsp_device device,
    const dsp_multi_crop_resize_params_t *mutli_crop_resize_params,
    const dsp_privacy_mask_t *privacy_mask_params,
    const dsp_image_enhancement_params_t *image_enhancement_params);

/**
 *  @}
 *  @defgroup blend Blend API
 *  @{
 */

/** Overlay parameters */
typedef struct {
    /** Overlay image to blend. Only ::DSP_IMAGE_FORMAT_A420 and ::DSP_IMAGE_FORMAT_ARGB formats are supported */
    dsp_image_properties_t overlay;
    /** Offset in the horizontal axis to place the blended overlay */
    int x_offset;
    /** Offset in the vertical axis to place the blended overlay */
    int y_offset;
} dsp_overlay_properties_t;

/**
 * @brief Perform alpha blend operation
 * @details Alpha blend an array of overlay images into a base image.
 *          The base image data is overwritten with the alpha blend result
 * @param device A ::dsp_device object
 * @param image A base image to alpha blend the overlays into. Only ::DSP_IMAGE_FORMAT_NV12 format is supported
 * @param overlays An array of overlays to alpha blend into the base image
 * @param overlays_count \p overlays array size
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 * @note The operation supports up to 50 \p overlays
 */
dsp_status dsp_blend(dsp_device device,
                     const dsp_image_properties_t *image, // image data is overwritten
                     const dsp_overlay_properties_t overlays[],
                     size_t overlays_count);

/**
 *  @}
 *  @defgroup blur Blur API
 *  @{
 */

/**
 * Blur region of interest (ROI) parameters
 * @note This typedef is for backwards compatibility only. Use ::dsp_roi_t instead
 */
typedef dsp_roi_t dsp_blur_roi_t;

/**
 * @brief Perform box blur operation
 * @details Box blur (filter) an array of regions of interet (ROIs) in a base image.
 *          The base image data is overwritten with the blurred result.
 *          Supported formats are ::DSP_IMAGE_FORMAT_GRAY8 and ::DSP_IMAGE_FORMAT_NV12
 * @param device A ::dsp_device object
 * @param image A base image to blur
 * @param rois An array of ROIs to blur in the base image
 * @param rois_count \p rois array size
 * @param kernel_size blurring kernel (matrix) size
 *                    odd number between 1 and 33
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 * @note The operation supports up to 80 \p ROIs
 */
dsp_status dsp_blur(dsp_device device,
                    dsp_image_properties_t *image,
                    const dsp_roi_t rois[],
                    size_t rois_count,
                    uint32_t kernel_size);

/**
 *  @}
 *  @defgroup convert Format Conversion API
 *  @{
 */

/**
 * @brief Perform format conversion operation
 * @details The function converts an image of a specified format to an image of another format
 *          Supported format conversions are:
 *          From ::DSP_IMAGE_FORMAT_RGB to ::DSP_IMAGE_FORMAT_NV12
 *          From ::DSP_IMAGE_FORMAT_BGR to ::DSP_IMAGE_FORMAT_NV12
 *          From ::DSP_IMAGE_FORMAT_NV12 to ::DSP_IMAGE_FORMAT_RGB
 *          From ::DSP_IMAGE_FORMAT_NV12 to ::DSP_IMAGE_FORMAT_BGR
 *          The sizes of the src image and dst image must be identical
 * @param device A ::dsp_device object
 * @param src Source image metadata - holds the image to convert
 * @param dst Destination image metadata - will hold the converted image
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_convert_format(dsp_device device, const dsp_image_properties_t *src, dsp_image_properties_t *dst);

/**
 *  @}
 *  @defgroup dewarp Dewarp API
 *  @{
 */

/** Grid of pixel coordinates in the input image, corresponding to even grid in the output image.
 * The grid cells in the output image are squares with size 64. They have to cover the
 * whole output image.
 */
typedef struct {
    /** Number of vertices in horizontal */
    size_t mesh_width;
    /** Number of vertices in vertical */
    size_t mesh_height;
    /** Pointer to vertices, ordered x,y,x,y,....
     * Numbers are Q15.16. */
    void *mesh_table;
} dsp_dewarp_mesh_t;

/** VSM config parameters */
typedef struct {
    /** Horizontal offset to the start of the VSM window */
    size_t hoffset;
    /** Vertical offset to the start of the VSM window */
    size_t voffset;
    /** Width of the VSM window */
    size_t width;
    /** Height of the VSM window */
    size_t height;
    /** Maximum displacement allowed in the VSM window (in pixels, in both horizontal and vertical directions)
     *  Calculated as: (16 * segments_count) / 2 */
    size_t max_displacement;
} dsp_vsm_config_t;

/** VSM parameters */
typedef struct {
    /** VSM configuration */
    dsp_vsm_config_t config;
    /** Buffer of size (config.height * sizeof(uint16_t)) with the sum of each row in the VSM window for the previous
     * frame */
    const uint16_t *prev_rows_sum;
    /** Buffer of size (config.width * sizeof(uint16_t)) with the sum of each column in the VSM window for the previous
     * frame */
    const uint16_t *prev_columns_sum;
    /** Buffer of size (config.height * sizeof(uint16_t)) to store the sum of each row in the VSM window for the current
     * frame. Can be the same buffer as prev_rows_sum */
    uint16_t *cur_rows_sum;
    /** Buffer of size (config.width * sizeof(uint16_t)) to store the sum of each column in the VSM window for the
     * current frame. Can be the same buffer as prev_columns_sum */
    uint16_t *cur_columns_sum;
} dsp_vsm_t;

/** ISP VSM parameters */
typedef struct {
    /** Horizontal axis center point of the VSP window used for ISP VSM (calculated as: hoffset + width / 2) */
    size_t center_x;
    /** Vertical axis center point of the VSP window used for ISP VSM (calculated as: voffset + height / 2) */
    size_t center_y;
    /** VSM dx reading from ISP */
    int dx;
    /** VSM dy reading from ISP */
    int dy;
} dsp_isp_vsm_t;

/** Angular tehta filter parameters */
typedef struct {
    /** Maximum allowed theta angle */
    float maximum_theta;
    /** Filter alpha */
    float alpha;
    /** The cur_angles_sum from the previous frame */
    float prev_angles_sum;
    /** The cur_traj from the previous frame */
    float prev_traj;
    /** Output of angles sum for current frame */
    float *cur_angles_sum;
    /** Output of trajectory for current frame */
    float *cur_traj;
    /** Output of stabilized theta */
    float *stabilized_theta;
} dsp_filter_angle_t;

/** Dewarp angular DIS parameters */
typedef struct {
    /** Image metadata for source image. Image data will not change */
    const dsp_image_properties_t *src;
    /** Image metadata for destination image */
    dsp_image_properties_t *dst;
    /** Mesh information */
    const dsp_dewarp_mesh_t *mesh;
    /** Interpolation method to use.
     *  Only ::INTERPOLATION_TYPE_BILINEAR is supported */
    dsp_interpolation_type_t interpolation;
    /** Determine if angular correction of the frame should be performed */
    bool do_mesh_correction;
    /** ISP VSM parameters */
    dsp_isp_vsm_t isp_vsm;
    /** VSM parameters */
    dsp_vsm_t vsm;
    /** Angular theta filter parameters */
    dsp_filter_angle_t filter_angle;
} dsp_dewarp_angular_dis_params_t;

/**
 * @brief Perform dewarp operation
 * @details Perform dewarp operation on an image.
 *          Supported format of the operation is ::DSP_IMAGE_FORMAT_NV12.
 *          Only 4k image resolution for src and dst is supported
 * @param device A ::dsp_device object
 * @param src Image metadata for source image. Image data will not change
 * @param dst Image metadata for destination image
 * @param mesh Mesh information
 * @param interpolation Interpolation method to use.
 *                      Only ::INTERPOLATION_TYPE_BILINEAR is supported
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error */
dsp_status dsp_dewarp(dsp_device device,
                      const dsp_image_properties_t *src,
                      const dsp_image_properties_t *dst,
                      const dsp_dewarp_mesh_t *mesh,
                      dsp_interpolation_type_t interpolation);

/**
 * @brief Perform dewarp angular DIS operation
 * @details Perform dewarp angular DIS operation on an image.
 *          Supported format of the operation is ::DSP_IMAGE_FORMAT_NV12.
 *          Only 4k image resolution for src and dst is supported
 * @param device A ::dsp_device object
 * @param params Dewarp angular DIS parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error */
dsp_status dsp_rot_dis_dewarp(dsp_device device, dsp_dewarp_angular_dis_params_t *params);

/**
 *  @}
 *  @defgroup affine Affine transformation API
 *  @{
 */

/** Maximum number of crops supported in ::dsp_multi_warp_affine */
#define MAX_AFFINE_TRANSFORMATION_CROPS (80)

/** Affine transformation matrix parameters.\n
 *  Matrix structure:\n
 * a11 a12 a13\n
 * a21 a22 a23\n
 */
typedef struct {
    float a11;
    float a12;
    float a13;
    float a21;
    float a22;
    float a23;
} dsp_affine_transform_matrix;

/** Affine transformation parameters */
typedef struct {
    /** Image metadata for source image. Image data will not change */
    const dsp_image_properties_t *src;
    /** Image metadata for destination image.
     *  Specify the required size in this image metadata.
     *  The operation will write the result to this image data pointer  */
    const dsp_image_properties_t *dst;
    /** Interpolation method to use. Only Bilinear is supported */
    dsp_interpolation_type_t interpolation;
    /** Affine transformation matrix */
    dsp_affine_transform_matrix *affine_matrix;
} dsp_warp_affine_params_t;

/** The cropped parts of the src image on which the warp affine
 *  will be performed on */
typedef struct {
    /** The wanted crop parameters on the src image */
    dsp_roi_t *params;
    /** Image metadata for the destination image.
     *  Specify the required size in this image metadata.
     *  The operation will write the result to the image data pointers */
    const dsp_image_properties_t *dst;
    /** Affine transformation matrix for this cropped part */
    dsp_affine_transform_matrix *affine_matrix;
} dsp_multi_warp_affine_cropped_part_t;

/** Multiple affine transformation parameters */
typedef struct {
    /** Image metadata for source image. Image data will not change */
    const dsp_image_properties_t *src;
    /** The destination images - cropped parts of the src image that
     *  will undergo an affine transformation */
    dsp_multi_warp_affine_cropped_part_t *crops[MAX_AFFINE_TRANSFORMATION_CROPS];
    /** Interpolation method to use. Only Bilinear is supported */
    dsp_interpolation_type_t interpolation;
    /** The number of images to perform the affine transformation on */
    uint32_t crops_count;
} dsp_multi_warp_affine_params_t;

/** Single affine rotation transformation parameters */
typedef struct {
    /** Image metadata for source image. Image data will not change */
    const dsp_image_properties_t *src;
    /** Image metadata for destination image.
     *  Specify the required size in this image metadata.
     *  The operation will write the result to this image data pointer  */
    const dsp_image_properties_t *dst;
    /** Interpolation method to use. Only Bilinear is supported */
    dsp_interpolation_type_t interpolation;
    /** The angle of the rotation (clockwise, in degrees) */
    float theta;
} dsp_affine_rotation_params_t;

/**
 * @brief Perform a generic affine transformation
 * @details This performs a generic affine transformation on an image
 * @param device A ::dsp_device object
 * @param warp_affine_params Pointer to ::dsp_warp_affine_params_t with the required transformation parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_warp_affine(dsp_device device, const dsp_warp_affine_params_t *warp_affine_params);

/**
 * @brief Perform multiple generic affine transformations
 * @details This performs affine transformations on different crops of the image
 * @param device A ::dsp_device object
 * @param multi_warp_affine_params Pointer to ::dsp_multi_warp_affine_params_t with the required transformation
 *                                 parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_multi_warp_affine(dsp_device device, const dsp_multi_warp_affine_params_t *multi_warp_affine_params);

/**
 * @brief Perform an affine rotation transformation
 * @details This performs an affine rotation transformation on an image
 * @param device A ::dsp_device object
 * @param rotate_params Pointer to ::dsp_affine_rotation_params_t with the required rotate parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_rotate(dsp_device device, const dsp_affine_rotation_params_t *rotate_params);

/**
 *  @}
 *
 *  @defgroup crop Crop API
 *  @{
 */

/** Crop parameters */
typedef struct {
    /** Image metadata for source image. Image data will not change */
    const dsp_image_properties_t *src;
    /** Image metadata for destination image */
    dsp_image_properties_t *dst;
    /** Region of interest (ROI) on src image to crop from */
    dsp_roi_t src_roi;
    /** Region of interest (ROI) on dst image to copy to.
        dst_roi size (width and height) must be identical to src_roi */
    dsp_roi_t dst_roi;
} dsp_crop_params_t;

/**
 * @brief Perform crop and copy operation
 * @details Perform copy of a region of interest (ROI) from the source image to a region of interest in the destination
 *          image. Supported formats of the operation are ::DSP_IMAGE_FORMAT_GRAY8, ::DSP_IMAGE_FORMAT_RGB,
 *          ::DSP_IMAGE_FORMAT_BGR, ::DSP_IMAGE_FORMAT_NV12, ::DSP_IMAGE_FORMAT_A420 and ::DSP_IMAGE_FORMAT_ARGB.
 *          The formats of the src image and dst image must be identical.
 * @param device A ::dsp_device object
 * @param crop_params Pointer to ::dsp_crop_params_t with the required parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_crop(dsp_device device, const dsp_crop_params_t *crop_params);

/**
 * @brief Perform multi crop operation
 * @details Perform multiple crop operations as described in ::dsp_crop
 * @param device A ::dsp_device object
 * @param multi_crop_params Pointer to an array of ::dsp_crop_params_t with the required parameters
 * @param crops_count Number of crops in the \p multi_crop_params array
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 * @note The operation supports up to 80 crops
 */
dsp_status dsp_multi_crop(dsp_device device, const dsp_crop_params_t *multi_crop_params, size_t crops_count);

/**
 *  @}
 *
 *  @defgroup filters Filters API
 *  @{
 */

typedef struct {
    /** Source image (unmodified) */
    const dsp_image_properties_t *src;
    /** Destination image (after filter application) */
    dsp_image_properties_t *dst;
    /**
     * kernel size, must be an odd number between 1 and 33
     * @note For kernel sizes of 3 and 5, a custom implementation is used for optimization. Higher kernel sizes use a
     * generic implementation
     */
    uint32_t kernel_size;
} dsp_gaussian_params_t;

/**
 * @brief Perform gaussian operation
 * @details Apply a gaussian kernel to an image. Supported format is ::DSP_IMAGE_FORMAT_GRAY8
 * @param device A ::dsp_device object
 * @param params Pointer to ::dsp_gaussian_params_t with the required parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_gaussian(dsp_device device, dsp_gaussian_params_t *params);
/**
 * @brief Perform multi gaussian operation
 * @details Perform multiple gaussian operations as described in ::dsp_gaussian
 * @param device A ::dsp_device object
 * @param params Pointer to an array of ::dsp_gaussian_params_t with the required parameters
 * @param params_count Number of entrie in the \p params array
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_multi_gaussian(dsp_device device, dsp_gaussian_params_t *params, size_t params_count);

typedef struct {
    /** Source image (unmodified). Must be of format ::DSP_IMAGE_FORMAT_GRAY8 */
    const dsp_image_properties_t *src;
    /** Destination image (after filter application). Must be of format ::DSP_IMAGE_FORMAT_GRAY16 */
    dsp_image_properties_t *dst;
} dsp_laplacian_params_t;

/**
 * @brief Perform laplacian operation
 * @details Apply a laplacian kernel to an image
 * @param device A ::dsp_device object
 * @param params Pointer to ::dsp_laplacian_params_t with the required parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 * @note The laplacian kernel used in the operation is:
 * \f$\begin{bmatrix}
 * 0 &  1 & 0 \\
 * 1 & -4 & 1 \\
 * 0 &  1 & 0
 * \end{bmatrix}\f$
 */
dsp_status dsp_laplacian(dsp_device device, dsp_laplacian_params_t *params);
/**
 * @brief Perform multi laplacian operation
 * @details Perform multiple laplacian operations as described in ::dsp_laplacian
 * @param device A ::dsp_device object
 * @param params Pointer to an array of ::dsp_laplacian_params_t with the required parameters
 * @param params_count Number of entrie in the \p params array
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_multi_laplacian(dsp_device device, dsp_laplacian_params_t *params, size_t params_count);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif