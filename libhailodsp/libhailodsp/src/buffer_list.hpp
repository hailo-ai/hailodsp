/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
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

#include "xrp_types.h"

#include <hailo/hailodsp.h>
#include <vector>

#include "xrp_kernel_defs.h"

enum class BufferAccessType {
    Read = 1 << 0,
    Write = 1 << 1,
    ReadWrite = Read | Write,
};

static_assert((int)XRP_FLAG_READ == (int)BufferAccessType::Read,
              "ioctl_buffer_flags must be identical to BufferAccessType");
static_assert((int)XRP_FLAG_WRITE == (int)BufferAccessType::Write,
              "ioctl_buffer_flags must be identical to BufferAccessType");
static_assert((int)XRP_FLAG_READ_WRITE == (int)BufferAccessType::ReadWrite,
              "ioctl_buffer_flags must be identical to BufferAccessType");

static_assert((int)XRP_MEMORY_TYPE_USERPTR == (int)DSP_MEMORY_TYPE_USERPTR,
              "ioctl_memory_type must be identical to dsp_memory_type_t");
static_assert((int)XRP_MEMORY_TYPE_DMABUF == (int)DSP_MEMORY_TYPE_DMABUF,
              "ioctl_memory_type must be identical to dsp_memory_type_t");

class BufferList {
   public:
    int add_plane(const dsp_data_plane_t &plane, BufferAccessType access_type, dsp_memory_type_t memory_type)
    {
        struct xrp_ioctl_buffer buffer = {
            .flags = static_cast<__u32>(access_type),
            .size = static_cast<__u32>(plane.bytesused),
            .memory_type = static_cast<__u32>(memory_type),
        };

        if (memory_type == DSP_MEMORY_TYPE_USERPTR) {
            buffer.addr = reinterpret_cast<uintptr_t>(plane.userptr);
        } else {
            buffer.fd = plane.fd;
        }

        buffers.push_back(buffer);
        return buffers.size() - 1;
    }

    int add_buffer(void *buffer, size_t size, BufferAccessType access_type)
    {
        buffers.emplace_back((struct xrp_ioctl_buffer){
            .flags = static_cast<__u32>(access_type),
            .size = static_cast<__u32>(size),
            .memory_type = XRP_MEMORY_TYPE_USERPTR,
            .addr = reinterpret_cast<uintptr_t>(buffer),
        });
        return buffers.size() - 1;
    }

    int add_buffer(int fd, size_t size, BufferAccessType access_type)
    {
        buffers.emplace_back((struct xrp_ioctl_buffer){
            .flags = static_cast<__u32>(access_type),
            .size = static_cast<__u32>(size),
            .memory_type = XRP_MEMORY_TYPE_DMABUF,
            .fd = fd,
        });
        return buffers.size() - 1;
    }

    std::vector<struct xrp_ioctl_buffer> &get_buffers() { return buffers; }

   private:
    std::vector<struct xrp_ioctl_buffer> buffers;
};