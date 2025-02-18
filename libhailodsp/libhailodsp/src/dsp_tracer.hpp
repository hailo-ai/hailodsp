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
#include "user_dsp_interface.h"

#include <mutex>
#include <queue>
#include <thread>
#include <vector>

typedef enum {
    DSP_TASK_STARTED_EVENT,
    DSP_TASK_ENTERED_PRIORITY_QUEUE_EVENT,
    DSP_USER_SENT_IOCTL_EVENT,
    DSP_KERNEL_RECEIVED_IOCTL_EVENT,
    DSP_WAITING_ON_MUTEX_EVENT,
    DSP_MUTEX_ACQUIRED_EVENT,
    DSP_IRQ_SENT_EVENT,
    DSP_FW_FINISHED_EVENT,
    DSP_MUTEX_RELEASED_EVENT,
    DSP_TASK_FINISHED_EVENT,
} dsp_trace_event_type_t;

typedef struct {
    uint64_t task_started;
    uint64_t task_entered_priority_queue;
    uint64_t user_sent_ioctl;
    uint64_t kernel_received_ioctl;
    uint64_t waiting_on_mutex;
    uint64_t mutex_acquired;
    uint64_t irq_sent;
    uint64_t fw_finished;
    uint64_t mutex_released;
    uint64_t task_finished;
} a53_timeframe_t;

typedef struct {
    std::thread::id tid;
    imaging_operation_t task_type;
    a53_timeframe_t a53_times;
    perf_info_t dsp_perf_info;
} task_timeframe_t;

class DspTracer {
   public:
    DspTracer(const DspTracer &) = delete;
    DspTracer &operator=(const DspTracer &) = delete;

    static DspTracer &get_instance()
    {
        static DspTracer tracer;
        return tracer;
    }

    task_timeframe_t *start_tracing(imaging_operation_t task_type);
    void update_event_timestamp_now(task_timeframe_t *task, dsp_trace_event_type_t event_type);
    void update_event_timestamp(task_timeframe_t *task, dsp_trace_event_type_t event_type, int64_t time);
    void update_perf_info(task_timeframe_t *task, perf_info_t &perf_info);
    uint64_t get_time_since_boot_ns();
    bool is_tracing_enabled;

   private:
    DspTracer();
    ~DspTracer();

    void write_to_file_periodically(std::chrono::milliseconds period);
    dsp_status write_timeframes_to_file();
    bool is_task_finished(task_timeframe_t &task);
    std::string create_output_filename();
    dsp_status create_output_file();
    size_t get_env_var_val_num(std::string env_var_name, size_t default_val);

    std::vector<task_timeframe_t> trace_handlers;
    std::queue<size_t> available_indices;
    size_t max_tasks_amount;
    size_t max_task_index;

    std::mutex trace_handlers_mutex;
    std::unique_ptr<std::ofstream> output_file;
    std::unique_ptr<std::thread> tracer_thread;
    bool stop_thread;
    std::string output_filename;
    bool is_header_written;
    bool started_tracing;
};
