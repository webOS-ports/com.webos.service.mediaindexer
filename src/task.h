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

#pragma once

#include "logging.h"
#include <iostream>
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <functional>

class Task
{
public:
    typedef std::function<void(void *ctx, void *data)> taskfunc_t;
    struct taskData
    {
        void *ctx;
        void *data;
    };

    Task() {}
    ~Task() {}

    bool create(taskfunc_t func);
    bool wakeUp();
    bool destroy();

    bool sendMessage(void *ctx, void *data);

    void loop();

private:
    std::thread task_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<void *> queue_;
    bool exit_ = false;
    taskfunc_t taskFunc = nullptr;

};

