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

#pragma once

#include "hailo/hailodsp.h"
#include "logger_macros.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <unordered_map>

constexpr static auto INFINITE_TIMEOUT = std::chrono::milliseconds(UINT32_MAX);

class TaskHandle;
class PriorityQueue {
   public:
    PriorityQueue();
    virtual ~PriorityQueue() = default;
    dsp_status enqueue(std::function<dsp_status()> func, priority_t priority, std::shared_ptr<TaskHandle> &out_handle);
    void abort();

   protected:
    dsp_status execute_next();

   private:
    struct Task {
        std::shared_ptr<TaskHandle> task_handle;
        priority_t priority;

        bool operator<(const Task &other) const { return priority < other.priority; }
    };

    std::priority_queue<Task> m_queue;
    bool m_is_aborted;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

class TaskHandle final {
   public:
    TaskHandle(std::function<dsp_status()> func)
        : m_func(func), m_was_executed(false), m_is_aborted(false), m_status(DSP_UNINITIALIZED)
    {
    }
    dsp_status wait(std::chrono::milliseconds timeout, dsp_status &task_status);

   private:
    void execute();
    void abort();

    std::function<dsp_status()> m_func;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_was_executed;
    bool m_is_aborted;
    dsp_status m_status;

    friend class PriorityQueue;
};

class PriorityQueueSingleton : public PriorityQueue {
   public:
    static std::unique_ptr<PriorityQueueSingleton> &get_instance()
    {
        static std::unique_ptr<PriorityQueueSingleton> instance;
        static std::mutex instance_mutex;

        if (!instance) {
            std::lock_guard<std::mutex> lock(instance_mutex);
            if (!instance) {
                instance = std::make_unique<PriorityQueueSingleton>();
            }
        }
        assert(instance != nullptr);
        return instance;
    }
    PriorityQueueSingleton();
    virtual ~PriorityQueueSingleton();

   private:
    std::thread m_thread;
    bool m_is_running;
};