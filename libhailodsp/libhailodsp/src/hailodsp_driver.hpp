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

#include "buffer_list.hpp"

#include <chrono>
#include <hailo/hailodsp.h>
#include <optional>
#include <string>

dsp_status driver_open_device(int &fd);
dsp_status driver_close_device(int fd);

dsp_status driver_allocate_buffer(int fd, size_t size, void **buffer);
dsp_status driver_release_buffer(int fd, void *buffer, size_t size);
dsp_status driver_sync_buffer_start(int fd, void *buffer, size_t size, dsp_sync_direction_t direction);
dsp_status driver_sync_buffer_end(int fd, void *buffer, size_t size, dsp_sync_direction_t direction);

dsp_status driver_send_command(int fd,
                               std::optional<std::string> nsid,
                               BufferList &buffer_list,
                               const void *in_data = nullptr,
                               size_t in_data_size = 0,
                               void *out_data = nullptr,
                               size_t out_data_size = 0);

dsp_status driver_send_command(int fd,
                               std::optional<std::string> nsid = std::nullopt,
                               const void *in_data = nullptr,
                               size_t in_data_size = 0,
                               void *out_data = nullptr,
                               size_t out_data_size = 0);

struct kernel_statistics {
    std::chrono::microseconds total_dsp_time;
    std::chrono::microseconds max_dsp_command_time;
    uint32_t total_dsp_commands;
    uint8_t current_threads_using_dsp;
    uint8_t max_threads_using_dsp;
};

dsp_status driver_reset_kernel_statistics(int fd);

dsp_status driver_get_kernel_statistics(int fd, kernel_statistics &stats);
