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
#include <stdint.h>
#include <stdlib.h>

#define DSP_MAX_ENUM (INT_MAX)

/** @defgroup status DSP Status codes
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

    DSP_STATUS_COUNT,                  /* Must be last */
    DSP_STATUS_MAX_ENUM = DSP_MAX_ENUM /**< Max enum value to maintain ABI Integrity */
} dsp_status;

/** @}
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

    /* Must be last */
    DSP_IMAGE_FORMAT_COUNT,
    /** Max enum value to maintain ABI Integrity */
    DSP_IMAGE_FORMAT_MAX_ENUM = DSP_MAX_ENUM
} dsp_image_format_t;

/** Image plane metadata */
typedef struct {
    /** Userspace pointer to the first pixel in the plane
     * @note Although it is possible to use any buffer that has been mapped to userspace, optimal performance can be
     * achieved if the plane data is physically contiguous. To create a contiguous buffer, the ::dsp_create_buffer
     * function can be used
     */
    void *userptr;
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
} dsp_image_properties_t;

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

/**
 *  @}
 *  @defgroup group_device_management Device Management API
 *  @{
 */

/** Opaque pointer to dsp_device object. The device object holds state and data required to issue commands */
typedef struct _dsp_device *dsp_device;

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
 *
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
 *  @}
 *
 *  @defgroup resize Crop & Resize API
 *  @{
 */

/** Maximum number of outputs supported in ::dsp_multi_crop_and_resize */
#define DSP_MULTI_RESIZE_OUTPUTS_COUNT (5)

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
 */
typedef struct {
    /** Offset of the left most pixel in the cropped image. valid range: [0, width-1] */
    size_t start_x;
    /** Offset of the up most pixel in the cropped image. valid range: [0, height-1] */
    size_t start_y;
    /** Offset of the right most pixel in the cropped image. valid range: [1, width] */
    size_t end_x;
    /** Offset of the bottom most pixel in the cropped image. valid range: [1, height] */
    size_t end_y;
} dsp_crop_api_t;

/** Multi-Resize parameters */
typedef struct {
    /** Image metadata for source image. Image data will not change */
    const dsp_image_properties_t *src;
    /** Image metadata for destination images.
     *  Specify the required size in the image metadata.
     *  The operation will write the result to this image data pointer.
     *  Assign NULL on the array entries to reduce the number of outputs.
     */
    const dsp_image_properties_t *dst[DSP_MULTI_RESIZE_OUTPUTS_COUNT];
    /** Interpolation method to use.
     *  Only ::INTERPOLATION_TYPE_BILINEAR and ::INTERPOLATION_TYPE_BICUBIC are supported. */
    dsp_interpolation_type_t interpolation;
} dsp_multi_resize_params_t;

/**
 * @brief Perform resize operation
 * @details The function resizes the image down to or up to the specified size
 *          Supported formats of the operation are ::DSP_IMAGE_FORMAT_GRAY8, ::DSP_IMAGE_FORMAT_RGB and
 *          ::DSP_IMAGE_FORMAT_NV12. The formats of the src image and dst image must be identical
 * @param device A ::dsp_device object
 * @param resize_params Pointer to ::dsp_resize_params_t with the required resize parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_resize(dsp_device device, const dsp_resize_params_t *resize_params);

/**
 * @brief Perform crop&resize operation
 * @details Perform crop operation on an image and then resize the cropped image to the specified size.
 *          Supported formats of the operation are ::DSP_IMAGE_FORMAT_GRAY8, ::DSP_IMAGE_FORMAT_RGB and
 *          ::DSP_IMAGE_FORMAT_NV12. The formats of the src image and dst image must be identical
 * @param device A ::dsp_device object
 * @param resize_params Pointer to ::dsp_resize_params_t with the required resize parameters
 * @param crop_params Pointer to ::dsp_crop_api_t with the required crop parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_crop_and_resize(dsp_device device,
                               const dsp_resize_params_t *resize_params,
                               const dsp_crop_api_t *crop_params);

/**
 * @brief Perform multi crop&resize operation
 * @details Perform crop operation on an image and then resize the cropped image to the specified sizes.
 *          Supported format of the operation is ::DSP_IMAGE_FORMAT_NV12.
 *          The formats of the src image and dst image must be identical.
 * @param device A ::dsp_device object
 * @param resize_params Pointer to ::dsp_multi_resize_params_t with the required resize parameters
 * @param crop_params Pointer to ::dsp_crop_api_t with the required crop parameters
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error */
dsp_status dsp_multi_crop_and_resize(dsp_device device,
                                     const dsp_multi_resize_params_t *resize_params,
                                     const dsp_crop_api_t *crop_params);
/**
 *  @}
 *
 *  @defgroup blend Blend API
 *  @{
 */

/** Overlay parameters */
typedef struct {
    /** Overlay image to blend. Only ::DSP_IMAGE_FORMAT_A420 format is supported */
    dsp_image_properties_t overlay;
    /** Offset in the horizontal axis to place the blended overlay */
    size_t x_offset;
    /** Offset in the vertical axis to place the blended overlay */
    size_t y_offset;
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
 *
 *  @defgroup blur Blur API
 *  @{
 */

/** Blur region of interest (ROI) parameters */
typedef struct {
    /** Offset of the left most pixel in the ROI. valid range: [0, width-1] */
    size_t start_x;
    /** Offset of the up most pixel in the ROI. valid range: [0, height-1] */
    size_t start_y;
    /** Offset of the right most pixel in the ROI. valid range: [1, width] */
    size_t end_x;
    /** Offset of the bottom most pixel in the ROI. valid range: [1, height] */
    size_t end_y;
} dsp_blur_roi_t;

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
                    const dsp_blur_roi_t rois[],
                    size_t rois_count,
                    uint32_t kernel_size);

/**
 *  @}
 *
 *  @defgroup convert Format Conversion API
 *  @{
 */

/**
 * @brief Perform format conversion operation
 * @details The function converts an image of a specified format to an image of another format
 *          Supported format conversions are:
 *          From ::DSP_IMAGE_FORMAT_RGB to ::DSP_IMAGE_FORMAT_NV12
 *          From ::DSP_IMAGE_FORMAT_NV12 to ::DSP_IMAGE_FORMAT_RGB
 *          The sizes of the src image and dst image must be identical
 * @param device A ::dsp_device object
 * @param src Source image metadata - holds the image to convert
 * @param dst Destination image metadata - will hold the converted image
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_convert_format(dsp_device device, const dsp_image_properties_t *src, dsp_image_properties_t *dst);

/**
 *  @}
 *
 *  @defgroup dewarp Dewarp API
 *  @{
 */

/** Grid of pixel coordinates in the input image, corresponding to even grid in the output image.
 * The grid cells in the output image are squares with size :mesh_sq_size. They have to cover the
 * whole output image.
 */
typedef struct {
    /** Number of vertices in horizontal */
    int mesh_width;
    /** Number of vertices in vertical */
    int mesh_height;
    /** Output cell size in pixels. Must be 64. This field will be removed in the future */
    int mesh_sq_size;
    /** Pointer to vertices, ordered x,y,x,y,....
     * Numbers are Q15.16. */
    int *mesh_table;
} dsp_dewarp_mesh_t;

/**
 * @brief Perform dewarp operation
 * @details Perform dewarp operation on an image.
 *          Supported format of the operation is ::DSP_IMAGE_FORMAT_NV12.
 *          The formats of the src image and dst image must be identical.
 *          Only 4k image resolution for src and dst is supported
 * @param device A ::dsp_device object
 * @param src Image metadata for source image. Image data will not change
 * @param dst Image metadata for destination image. Image data will not change
 * @param mesh Mesh information
 * @param interpolation Interpolation method to use.
 *                      Only ::INTERPOLATION_TYPE_BILINEAR and ::INTERPOLATION_TYPE_BICUBIC are supported
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error */
dsp_status dsp_dewarp(dsp_device device,
                      const dsp_image_properties_t *src,
                      const dsp_image_properties_t *dst,
                      const dsp_dewarp_mesh_t *mesh,
                      dsp_interpolation_type_t interpolation);

/**
 *  @}
 */

#ifdef __cplusplus
}
#endif

#endif