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

#include "priority_queue.hpp"

void TaskHandle::execute()
{
    auto status = m_func();
    std::unique_lock<std::mutex> lock(m_mutex);
    m_status = status;
    m_was_executed = true;
    m_cv.notify_one();
}

dsp_status TaskHandle::wait(std::chrono::milliseconds timeout, dsp_status &task_status)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    bool was_successful = m_cv.wait_for(lock, timeout, [this] { return m_was_executed || m_is_aborted; });
    if (m_is_aborted) {
        return DSP_ABORTED;
    }
    if (!was_successful) {
        LOGGER__ERROR("Timed out waiting for task to complete");
        return DSP_TIMEOUT;
    }

    task_status = m_status;
    return DSP_SUCCESS;
}

void TaskHandle::abort()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_is_aborted = true;
    m_cv.notify_one();
}

PriorityQueue::PriorityQueue() : m_is_aborted(false) {}

dsp_status PriorityQueue::enqueue(std::function<dsp_status()> func,
                                  priority_t priority,
                                  std::shared_ptr<TaskHandle> &out_handle)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_is_aborted) {
        return DSP_ABORTED;
    }

    auto handle = std::make_shared<TaskHandle>(func);
    if (!handle) {
        LOGGER__ERROR("Failed to allocate memory for task handle");
        return DSP_OUT_OF_HOST_MEMORY;
    }

    Task task = {handle, priority};
    m_queue.push(task);
    m_cv.notify_one();

    out_handle = handle;
    return DSP_SUCCESS;
}

dsp_status PriorityQueue::execute_next()
{
    std::shared_ptr<TaskHandle> handle;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        bool was_successful =
            m_cv.wait_for(lock, INFINITE_TIMEOUT, [this] { return (m_queue.size() > 0) || m_is_aborted; });
        if (m_is_aborted) {
            return DSP_ABORTED;
        }
        if (!was_successful) {
            return DSP_TIMEOUT;
        }

        auto &task = m_queue.top();
        handle = task.task_handle;
        m_queue.pop();
    }

    handle->execute();
    return DSP_SUCCESS;
}

void PriorityQueue::abort()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_queue.size() > 0) {
        auto &task = m_queue.top();
        task.task_handle->abort();
        m_queue.pop();
    }
    m_is_aborted = true;
    m_cv.notify_one();
}

PriorityQueueSingleton::PriorityQueueSingleton() : PriorityQueue(), m_is_running(true)
{
    m_thread = std::thread([this] {
        while (m_is_running) {
            dsp_status status = execute_next();
            if (DSP_ABORTED == status) {
                return;
            }
            if (status != DSP_SUCCESS) {
                LOGGER__ERROR("Failed to dequeue item from priority queue, status = {}", status);
                return;
            }
        }
    });
}

PriorityQueueSingleton::~PriorityQueueSingleton()
{
    abort();
    m_is_running = false;

    if (m_thread.joinable()) {
        m_thread.join();
    }
}