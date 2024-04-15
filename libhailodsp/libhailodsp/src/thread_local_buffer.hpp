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

#include "hailo/hailodsp.h"
#include "logger_macros.hpp"

#include <memory>

template <typename T>
class ThreadLocalBuffer {
   public:
    static std::shared_ptr<T> get()
    {
        thread_local ThreadLocalBuffer instance;

        if (!instance.m_memory) {
            auto status = dsp_create_device(&instance.m_device);
            if (status != DSP_SUCCESS) {
                LOGGER__ERROR("Failed to open device {}", status);
                return nullptr;
            }

            void *memory;
            status = dsp_create_buffer(instance.m_device, sizeof(T), &memory);
            if (status != DSP_SUCCESS) {
                LOGGER__ERROR("Failed to allocate memory {}", status);
                return nullptr;
            }

            auto empty_deleter = [](__attribute__((unused)) T *memory) {};

            instance.m_memory = std::shared_ptr<T>(static_cast<T *>(memory), empty_deleter);
        }

        return instance.m_memory;
    }

    ThreadLocalBuffer(const ThreadLocalBuffer &) = delete;
    ThreadLocalBuffer &operator=(const ThreadLocalBuffer &) = delete;
    ThreadLocalBuffer(const ThreadLocalBuffer &&) = delete;
    ThreadLocalBuffer &operator=(const ThreadLocalBuffer &&) = delete;

   private:
    std::shared_ptr<T> m_memory;
    dsp_device m_device;

    ThreadLocalBuffer() = default;
    ~ThreadLocalBuffer()
    {
        if (m_memory) {
            (void)dsp_release_buffer(m_device, m_memory.get());
            (void)dsp_release_device(m_device);
        }
    }
};
