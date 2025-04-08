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

#pragma once

#include "logger_macros.hpp"

#include <cerrno>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <memory>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

template <typename T>
class ThreadLocalBuffer {
   public:
    static std::shared_ptr<T> get()
    {
        thread_local ThreadLocalBuffer instance;

        if (!instance.m_memory) {
            auto dma_heap_fd = file_descriptor(new int(open("/dev/dma_heap/linux,cma", O_RDWR)), &close_deleter);
            if (*dma_heap_fd < 0) {
                LOGGER__ERROR("Failed to open dma heap: {}", strerror(errno));
                return nullptr;
            }

            struct dma_heap_allocation_data alloc_data = {
                .len = sizeof(T),
                .fd_flags = O_RDWR | O_CLOEXEC,
            };

            int ret = ioctl(*dma_heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data);
            if (ret < 0) {
                LOGGER__ERROR("Failed to allocate dma heap buffer: {}", strerror(errno));
                return nullptr;
            }

            instance.m_fd = file_descriptor(new int(alloc_data.fd), &close_deleter);

            void *memory = mmap(NULL, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, *instance.m_fd, 0);
            if (memory == MAP_FAILED) {
                LOGGER__ERROR("Failed to mmap memory: {}", strerror(errno));
                return nullptr;
            }

            instance.m_memory = std::shared_ptr<T>(static_cast<T *>(memory), &unmap_deleter);
        }

        return instance.m_memory;
    }

    ThreadLocalBuffer(const ThreadLocalBuffer &) = delete;
    ThreadLocalBuffer &operator=(const ThreadLocalBuffer &) = delete;
    ThreadLocalBuffer(const ThreadLocalBuffer &&) = delete;
    ThreadLocalBuffer &operator=(const ThreadLocalBuffer &&) = delete;

   private:
    static void close_deleter(int *fd)
    {
        if (fd) {
            (void)close(*fd);
            delete fd;
        }
    }

    static void unmap_deleter(T *memory)
    {
        if (memory) {
            (void)munmap(memory, sizeof(T));
        }
    }

    using file_descriptor = std::unique_ptr<int, decltype(&ThreadLocalBuffer::close_deleter)>;

    file_descriptor m_fd = file_descriptor(nullptr, &close_deleter);
    std::shared_ptr<T> m_memory;

    ThreadLocalBuffer() = default;
    ~ThreadLocalBuffer()
    {
        // unmap memory before closing the file descriptor
        m_memory.reset();
        m_fd.reset();
    }
};
