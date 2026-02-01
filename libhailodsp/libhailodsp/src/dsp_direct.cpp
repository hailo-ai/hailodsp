#include "buffer_list.hpp"
#include "hailo/hailodsp_direct.h"
#include "logger_macros.hpp"
#include "send_command.hpp"

static_assert((int)DSP_DIRECT_BUFFER_ACCESS_READ == (int)BufferAccessType::Read,
              "dsp_direct_buffer_access_type_t must be identical to BufferAccessType");
static_assert((int)DSP_DIRECT_BUFFER_ACCESS_WRITE == (int)BufferAccessType::Write,
              "dsp_direct_buffer_access_type_t must be identical to BufferAccessType");
static_assert((int)DSP_DIRECT_BUFFER_ACCESS_READWRITE == (int)BufferAccessType::ReadWrite,
              "dsp_direct_buffer_access_type_t must be identical to BufferAccessType");

dsp_status dsp_direct_send_command(dsp_device device,
                                   const char *nsid,
                                   const dsp_direct_buffer_t *buffers,
                                   size_t num_buffers,
                                   const void *in_data,
                                   size_t in_data_size,
                                   void *out_data,
                                   size_t out_data_size)
{
    BufferList buffer_list;

    for (size_t i = 0; i < num_buffers; ++i) {
        const dsp_direct_buffer_t &buf = buffers[i];
        BufferAccessType access_type = static_cast<BufferAccessType>(buf.access_type);
        if (buf.memory_type == DSP_MEMORY_TYPE_USERPTR) {
            buffer_list.add_buffer(buf.addr, buf.size, access_type);
        } else if (buf.memory_type == DSP_MEMORY_TYPE_DMABUF) {
            buffer_list.add_buffer(buf.fd, buf.size, access_type);
        } else {
            LOGGER__ERROR("Invalid memory type: {} for buffer {}", buf.memory_type, i);
            return DSP_INVALID_ARGUMENT;
        }
    }

    return send_command(device, buffer_list, in_data, in_data_size, out_data, out_data_size, nsid);
}