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

#include <cstring>
#include <iostream>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

// Helper class for DMA-BUF buffer allocation and management
class DmaHeapBuffer {
   public:
    DmaHeapBuffer() : fd_(-1), size_(0), addr_(nullptr)
    {
        // Default constructor initializes to invalid state
    }

    // Allocate a DMA-BUF buffer of the given size from the Hailo media heap
    DmaHeapBuffer(size_t buffer_size) : fd_(-1), size_(buffer_size), addr_(nullptr)
    {
        int heap_fd = open("/dev/dma_heap/hailo_media_buf,cma", O_RDWR);
        if (heap_fd < 0) {
            std::cerr << "Failed to open dma heap: " << strerror(errno) << std::endl;
            return;
        }

        struct dma_heap_allocation_data alloc_data = {
            .len = buffer_size,
            .fd = 0,
            .fd_flags = O_RDWR | O_CLOEXEC,
            .heap_flags = 0,
        };

        if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0) {
            std::cerr << "Failed to allocate dma heap buffer: " << strerror(errno) << std::endl;
            close(heap_fd);
            return;
        }

        fd_ = alloc_data.fd;
        close(heap_fd);
    }

    ~DmaHeapBuffer()
    {
        unmap();
        if (fd_ >= 0) {
            if (close(fd_) < 0) {
                std::cerr << "Failed to close dma heap buffer: " << strerror(errno) << std::endl;
            }
        }
    }

    int fd() const { return fd_; }
    size_t size() const { return size_; }
    bool valid() const { return fd_ >= 0; }

    // Map the buffer into user space
    void *map()
    {
        if (fd_ < 0)
            return nullptr;
        if (addr_) // Already mapped
            return addr_;
        addr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd_, 0);
        if (addr_ == MAP_FAILED) {
            std::cerr << "Failed to mmap dma buf: " << strerror(errno) << std::endl;
            addr_ = nullptr;
            return nullptr;
        }
        return addr_;
    }

    // Unmap the buffer from user space
    void unmap()
    {
        if (addr_) {
            munmap(addr_, size_);
            addr_ = nullptr;
        }
    }

    // Synchronize buffer for reading (device to CPU)
    bool start_read() const
    {
        if (fd_ < 0)
            return false;
        struct dma_buf_sync sync = {.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ};
        if (ioctl(fd_, DMA_BUF_IOCTL_SYNC, &sync) < 0) {
            std::cerr << "Failed to start sync dma buf for read (fd=" << fd_ << "): " << strerror(errno) << std::endl;
            return false;
        }
        return true;
    }
    bool end_read() const
    {
        if (fd_ < 0)
            return false;
        struct dma_buf_sync sync = {.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ};
        if (ioctl(fd_, DMA_BUF_IOCTL_SYNC, &sync) < 0) {
            std::cerr << "Failed to end sync dma buf for read (fd=" << fd_ << "): " << strerror(errno) << std::endl;
            return false;
        }
        return true;
    }
    // Synchronize buffer for writing (CPU to device)
    bool start_write() const
    {
        if (fd_ < 0)
            return false;
        struct dma_buf_sync sync = {.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE};
        if (ioctl(fd_, DMA_BUF_IOCTL_SYNC, &sync) < 0) {
            std::cerr << "Failed to start sync dma buf for write (fd=" << fd_ << "): " << strerror(errno) << std::endl;
            return false;
        }
        return true;
    }
    bool end_write() const
    {
        if (fd_ < 0)
            return false;
        struct dma_buf_sync sync = {.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE};
        if (ioctl(fd_, DMA_BUF_IOCTL_SYNC, &sync) < 0) {
            std::cerr << "Failed to end sync dma buf for write (fd=" << fd_ << "): " << strerror(errno) << std::endl;
            return false;
        }
        return true;
    }

    // Non-copyable
    DmaHeapBuffer(const DmaHeapBuffer &) = delete;
    DmaHeapBuffer &operator=(const DmaHeapBuffer &) = delete;

    // Movable
    DmaHeapBuffer(DmaHeapBuffer &&other) noexcept : fd_(other.fd_), size_(other.size_), addr_(other.addr_)
    {
        other.fd_ = -1;
        other.size_ = 0;
        other.addr_ = nullptr;
    }

    DmaHeapBuffer &operator=(DmaHeapBuffer &&other) noexcept
    {
        if (this != &other) {
            fd_ = other.fd_;
            size_ = other.size_;
            addr_ = other.addr_;
            other.fd_ = -1;
            other.size_ = 0;
            other.addr_ = nullptr;
        }
        return *this;
    }

   private:
    int fd_;
    size_t size_;
    void *addr_;
};