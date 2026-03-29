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

#ifndef HAILODSP_BASE_H
#define HAILODSP_BASE_H
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
    DSP_SYNC_BUFFER_FAILED,         /**< Failed syncing buffer */
    DSP_IOCTL_FAILED,               /**< General error for driver communication failure */
    DSP_TIMEOUT,                    /**< Timeout occurred */
    DSP_ABORTED,                    /**< Operation was aborted */

    DSP_STATUS_COUNT,                  /* Must be last */
    DSP_STATUS_MAX_ENUM = DSP_MAX_ENUM /**< Max enum value to maintain ABI Integrity */
} dsp_status;

/**
 *  @}
 *
 *  @defgroup group_device_management Device Management API
 *  @{
 */

/** Opaque pointer to dsp_device object. The device object holds state and data required to issue commands */
typedef struct dsp_device_internal *dsp_device;

/** Priority level of dsp_device object */
typedef int priority_t;

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
 * Set priority level to operations of dsp_device object. Higher priority levels will be executed first.
 * The default priority level is @a DSP_DEFAULT_DEVICE_PRIORITY.
 *
 * @param device A ::dsp_device to be prioritized
 * @param priority Priority level to set
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_set_priority(dsp_device device, priority_t priority);

/**
 *  @}
 *  @defgroup buffer_mgnt Buffer Management API
 *  @{
 */

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

/**
 * Allocates contiguous physical-memory buffer
 *
 * @param device A ::dsp_device object
 * @param size Required buffer size to allocate
 * @param[out] buffer A pointer to void* that receives the allocated buffer
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
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif // HAILODSP_BASE_H