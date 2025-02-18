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

#include "dsp_tracer.hpp"
#include "logger_macros.hpp"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#define DSP_TRACER_ENV_VAR ("DSP_TRACE")
#define DSP_TRACER_OFF (0)

#define DSP_TRACER_MAX_TASKS_ENV_VAR ("DSP_TRACE_MAX_TASKS")
#define DSP_TRACER_MAX_TASKS_DEFAULT_VAL (10000)

#define DSP_TRACER_OUTPUT_DIR_VAR ("DSP_TRACE_OUTPUT_DIR")
#define DSP_TRACER_DEFAULT_OUTPUT_DIR ("/tmp/timestamps/")

#define DSP_TRACER_WRITE_PERIOD_ENV_VAR ("DSP_TRACE_WRITE_PERIOD")
#define DSP_TRACER_WRITE_PERIOD_MSEC_DEFAULT_VAL (10000)

std::string dsp_tasks_strs[] = {
    [IMAGING_OP_CROP_AND_RESIZE] = "Crop and resize",
    [IMAGING_OP_BLEND] = "Blend",
    [IMAGING_OP_BLUR] = "Blur",
    [IMAGING_OP_CONVERT_FORMAT] = "Convert format",
    [IMAGING_OP_DEWARP] = "Dewarp",
    [IMAGING_OP_MULTI_CROP_AND_RESIZE] = "Multi crop and resize",
    [IMAGING_OP_ROT_DIS_DEWARP] = "Rot dis dewarp",
    [IMAGING_OP_MULTI_AFFINE_TRANSFORM] = "Affine transform",
    [IMAGING_OP_MULTI_CROP] = "Multi crop",
    [IMAGING_OP_MULTI_GAUSSIAN] = "Multi gaussian",
    [IMAGING_OP_MULTI_LAPLACIAN] = "Multi laplacian",
    [IMAGING_OP_TELESCOPIC_MULTI_CROP_AND_RESIZE] = "Telescopic multi crop and resize",
};

static_assert(ARRAY_LENGTH(dsp_tasks_strs) == IMAGING_OPS_COUNT,
              "dsp_tasks_strs must have an entry for each imaging operation type");

static struct {
    std::string header;
    void (*write_field)(std::ofstream &stream, const task_timeframe_t &task);
} timeframe_fields[] = {
    {
        .header = "TID",
        .write_field = [](std::ofstream &stream, const task_timeframe_t &task) { stream << task.tid; },
    },
    {
        .header = "Task name",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << dsp_tasks_strs[task.task_type]; },
    },
    {
        .header = "Task started",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.a53_times.task_started; },
    },
    {
        .header = "Task entered priority queue",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.a53_times.task_entered_priority_queue; },
    },
    {
        .header = "User sent IOCTL",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.a53_times.user_sent_ioctl; },
    },
    {
        .header = "Kernel received IOCTL",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.a53_times.kernel_received_ioctl; },
    },
    {
        .header = "Waiting on mutex",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.a53_times.waiting_on_mutex; },
    },
    {
        .header = "Mutex acquired",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.a53_times.mutex_acquired; },
    },
    {
        .header = "IRQ sent",
        .write_field = [](std::ofstream &stream, const task_timeframe_t &task) { stream << task.a53_times.irq_sent; },
    },
    {
        .header = "FW finished",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.a53_times.fw_finished; },
    },
    {
        .header = "Mutex released",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.a53_times.mutex_released; },
    },
    {
        .header = "Task finished",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.a53_times.task_finished; },
    },
    {
        .header = "XRP handler",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.xrp_handler; },
    },
    {
        .header = "Get arg params context",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.get_arg_params_context; },
    },
    {
        .header = "Process tiles total",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.process_tiles_total; },
    },
    {
        .header = "Process tiles setup",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.process_tiles_setup; },
    },
    {
        .header = "Kernel",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.kernel; },
    },
    {
        .header = "In DMA wait",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.in_dma_wait; },
    },
    {
        .header = "Out DMA wait",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.out_dma_wait; },
    },
    {
        .header = "Setup updates tiles",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.setup_updates_tiles; },
    },
    {
        .header = "Pad edges",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.pad_edges; },
    },
    {
        .header = "Ref tile setup",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.ref_tile_setup; },
    },
    {
        .header = "In DMA config",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.in_dma_config; },
    },
    {
        .header = "Out dma config",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.out_dma_config; },
    },
    {
        .header = "Optimal Tile Calc",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.optimal_tile_calc; },
    },
    {
        .header = "Tiles_count",
        .write_field = [](std::ofstream &stream,
                          const task_timeframe_t &task) { stream << task.dsp_perf_info.fik.tiles_count; },
    },
};

DspTracer::DspTracer()
{
    is_tracing_enabled = get_env_var_val_num(DSP_TRACER_ENV_VAR, DSP_TRACER_OFF);
    if (!is_tracing_enabled) {
        return;
    }

    is_header_written = false;
    started_tracing = false;

    max_tasks_amount = get_env_var_val_num(DSP_TRACER_MAX_TASKS_ENV_VAR, DSP_TRACER_MAX_TASKS_DEFAULT_VAL);
    trace_handlers.reserve(max_tasks_amount);
    max_task_index = 0;

    for (size_t i = 0; i < max_tasks_amount; i++) {
        available_indices.push(i);
    }

    size_t flush_rate = get_env_var_val_num(DSP_TRACER_WRITE_PERIOD_ENV_VAR, DSP_TRACER_WRITE_PERIOD_MSEC_DEFAULT_VAL);
    write_to_file_periodically(std::chrono::milliseconds(flush_rate));
    LOGGER__INFO("DspTracer initialized, max tasks: {}, flush rate: {} ns", max_tasks_amount, flush_rate);
}

dsp_status DspTracer::create_output_file()
{
    output_filename = create_output_filename();
    output_file = std::make_unique<std::ofstream>(output_filename, std::ios::app);
    if (!output_file->is_open()) {
        LOGGER__ERROR("Failed to open output file: {}", output_filename);
        is_tracing_enabled = false;
        return DSP_OUT_OF_HOST_MEMORY;
    }
    for (const auto &data_point : timeframe_fields) {
        *output_file << data_point.header << ",";
    }
    *output_file << std::endl;
    LOGGER__INFO("DspTracer: output file created: {}", output_filename);
    return DSP_SUCCESS;
}

std::string DspTracer::create_output_filename()
{
    std::string output_filename;

    const char *output_dir_env_val = std::getenv(DSP_TRACER_OUTPUT_DIR_VAR);
    if (output_dir_env_val != nullptr) {
        output_filename = output_dir_env_val;
    } else {
        output_filename = DSP_TRACER_DEFAULT_OUTPUT_DIR;
    }

    if (!std::filesystem::exists(output_filename)) {
        std::filesystem::create_directory(output_filename);
    }

    output_filename += std::to_string(get_time_since_boot_ns()) + ".csv";
    return output_filename;
}

task_timeframe_t *DspTracer::start_tracing(imaging_operation_t task_type)
{
    if (!is_tracing_enabled) {
        return nullptr;
    }

    started_tracing = true;

    std::lock_guard<std::mutex> lock(trace_handlers_mutex);
    if (available_indices.size() == 0) {
        if (write_timeframes_to_file() != DSP_SUCCESS) {
            return nullptr;
        }
    }

    int available_index = available_indices.front();
    available_indices.pop();
    trace_handlers[available_index] = {
        .tid = std::this_thread::get_id(),
        .task_type = task_type,
        .a53_times = {0},
        .dsp_perf_info = {0},
    };
    trace_handlers[available_index].a53_times.task_started = get_time_since_boot_ns();

    if (max_task_index < max_tasks_amount) {
        max_task_index++;
    }

    return &trace_handlers[available_index];
}

void DspTracer::update_event_timestamp(task_timeframe_t *task, dsp_trace_event_type_t event_type, int64_t time)
{
    if (task == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(trace_handlers_mutex);
    switch (event_type) {
        case DSP_TASK_ENTERED_PRIORITY_QUEUE_EVENT:
            task->a53_times.task_entered_priority_queue = time;
            break;
        case DSP_USER_SENT_IOCTL_EVENT:
            task->a53_times.user_sent_ioctl = time;
            break;
        case DSP_KERNEL_RECEIVED_IOCTL_EVENT:
            task->a53_times.kernel_received_ioctl = time;
            break;
        case DSP_WAITING_ON_MUTEX_EVENT:
            task->a53_times.waiting_on_mutex = time;
            break;
        case DSP_MUTEX_ACQUIRED_EVENT:
            task->a53_times.mutex_acquired = time;
            break;
        case DSP_IRQ_SENT_EVENT:
            task->a53_times.irq_sent = time;
            break;
        case DSP_FW_FINISHED_EVENT:
            task->a53_times.fw_finished = time;
            break;
        case DSP_MUTEX_RELEASED_EVENT:
            task->a53_times.mutex_released = time;
            break;
        case DSP_TASK_FINISHED_EVENT:
            task->a53_times.task_finished = time;
            break;
        default:
            break;
    }
}

void DspTracer::update_event_timestamp_now(task_timeframe_t *task, dsp_trace_event_type_t event_type)
{
    update_event_timestamp(task, event_type, get_time_since_boot_ns());
}

void DspTracer::update_perf_info(task_timeframe_t *task, perf_info_t &perf_info)
{
    if (task == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(trace_handlers_mutex);
    task->dsp_perf_info = perf_info;
}

size_t DspTracer::get_env_var_val_num(std::string env_var_name, size_t default_val)
{
    const char *env_val = std::getenv(env_var_name.c_str());
    if (env_val != nullptr) {
        return std::atoi(env_val);
    } else {
        return default_val;
    }
}

DspTracer::~DspTracer()
{
    if (!is_tracing_enabled || !started_tracing) {
        return;
    }

    if (tracer_thread) {
        stop_thread = true;
        tracer_thread->join();
    }
    write_timeframes_to_file();

    output_file->close();
}

void DspTracer::write_to_file_periodically(std::chrono::milliseconds period)
{
    stop_thread = false;
    tracer_thread = std::make_unique<std::thread>([this, period]() {
        while (!this->stop_thread) {
            std::this_thread::sleep_for(period);
            std::lock_guard<std::mutex> lock(trace_handlers_mutex);
            this->write_timeframes_to_file();
        }
    });
}

bool DspTracer::is_task_finished(task_timeframe_t &task)
{
    return task.a53_times.task_finished != 0;
}

dsp_status DspTracer::write_timeframes_to_file()
{
    if (!is_tracing_enabled || !started_tracing) {
        return DSP_UNINITIALIZED;
    }

    if (!is_header_written) {
        dsp_status status = create_output_file();
        if (status != DSP_SUCCESS) {
            return status;
        }
        is_header_written = true;
    }

    std::thread::id no_thread_id;
    for (size_t i = 0; i < max_task_index; i++) {
        auto &handler = trace_handlers[i];
        if ((handler.tid == no_thread_id) || (!is_task_finished(handler))) {
            continue;
        }

        for (const auto &data_point : timeframe_fields) {
            data_point.write_field(*output_file, handler);
            *output_file << ",";
        }
        *output_file << std::endl;

        trace_handlers[i].tid = no_thread_id;
        available_indices.push(i);
    }
    output_file->flush();

    return DSP_SUCCESS;
}

uint64_t DspTracer::get_time_since_boot_ns()
{
    return std::chrono::steady_clock::now().time_since_epoch().count();
}