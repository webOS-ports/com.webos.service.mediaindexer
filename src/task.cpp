// Copyright (c) 2019-2020 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "task.h"

bool Task::create(Task::taskfunc_t func)
{
    if (!func) {
        LOG_ERROR(MEDIA_INDEXER_TASK, 0, "Invalid task function");
        return false;
    }

    taskFunc = func;
    task_ = std::thread(&Task::loop, this);
    task_.detach();
    return true;
}

bool Task::wakeUp()
{
    cv_.notify_one();
    return true;
}

bool Task::destroy()
{
    exit_ = true;
    cv_.notify_one();
    if (task_.joinable())
        task_.join();

    return true;
}

bool Task::sendMessage(void *ctx, void *data)
{
    std::lock_guard<std::mutex> lk(mutex_);
    taskData *tdata = new taskData;
    tdata->ctx = ctx;
    tdata->data = data;
    queue_.push_back((void *)tdata);
    cv_.notify_one();
    return true;
}

void Task::loop()
{
    while (!exit_)
    {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return !queue_.empty(); });
        taskData *data = static_cast<taskData *>(queue_.front());
        if (!data)
        {
            LOG_ERROR(MEDIA_INDEXER_TASK, 0, "Deque data is invalid!");
            continue;
        }
        if (taskFunc) {
            LOG_INFO(MEDIA_INDEXER_TASK, 0, "Task Function Start");
            taskFunc(data->ctx, data->data);
        }
        if (data)
            free(data);
        queue_.pop_front();
    }
}

