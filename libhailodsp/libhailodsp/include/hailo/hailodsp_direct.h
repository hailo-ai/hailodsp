#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <hailo/hailodsp_base.h>

/**
 *  @defgroup direct_api Direct API
 *  @{
 */

/** Buffer access type */
typedef enum {
    /** Read access */
    DSP_DIRECT_BUFFER_ACCESS_READ = 1 << 0,
    /** Write access */
    DSP_DIRECT_BUFFER_ACCESS_WRITE = 1 << 1,
    /** Read and write access */
    DSP_DIRECT_BUFFER_ACCESS_READWRITE = (1 << 0) | (1 << 1),

    /* Must be last */
    DSP_DIRECT_BUFFER_ACCESS_COUNT,
    /** Max enum value to maintain ABI Integrity */
    DSP_DIRECT_BUFFER_ACCESS_MAX_ENUM = DSP_MAX_ENUM
} dsp_direct_buffer_access_type_t;

/**
 * @brief Direct buffer descriptor
 * @details Describes a buffer to be used in direct DSP commands.
 */
typedef struct {
    /** Buffer access type */
    dsp_direct_buffer_access_type_t access_type;
    /** Buffer memory type */
    dsp_memory_type_t memory_type;
    /** Buffer size in bytes */
    uint32_t size;
    /** Buffer address or file descriptor */
    union {
        /** User pointer to buffer (if memory_type is ::DSP_MEMORY_TYPE_USERPTR) */
        const void *addr;
        /** DMA-BUF file descriptor (if memory_type is ::DSP_MEMORY_TYPE_DMABUF) */
        int32_t fd;
    };
} dsp_direct_buffer_t;

/**
 * @brief Send a direct command to the DSP
 * @details Sends a command to the DSP with the specified namespace, buffers, and input/output data.
 * @param device DSP device handle
 * @param nsid Namespace identifier string
 * @param buffers Array of ::dsp_direct_buffer_t describing the buffers to use
 * @param num_buffers Number of buffers in the \p buffers array
 * @param in_data Pointer to input data for the command
 * @param in_data_size Size of the input data in bytes
 * @param out_data Pointer to output data buffer
 * @param out_data_size Size of the output data buffer in bytes
 * @return Upon success, returns ::DSP_SUCCESS. Otherwise, returns a ::dsp_status error
 */
dsp_status dsp_direct_send_command(dsp_device device,
                                   const char *nsid,
                                   const dsp_direct_buffer_t *buffers,
                                   size_t num_buffers,
                                   const void *in_data,
                                   size_t in_data_size,
                                   void *out_data,
                                   size_t out_data_size);

/**
 *  @}
 */

#ifdef __cplusplus
}
#endif