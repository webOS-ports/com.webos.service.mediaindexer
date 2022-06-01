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
#include "dbconnector/lunaconnector.h"
#include <pbnjson.hpp>
#include <luna-service2/lunaservice.h>
#include <functional>
#include <string>
#include <memory>
typedef bool (*DbObserverCallback) (LSHandle *hdl, LSMessage *msg, void *ctx);

class DbObserver
{
public:
    typedef std::function<void()> db_initialized_callback_t;
    DbObserver(LSHandle *hdl, db_initialized_callback_t && db_inital_callback);
    ~DbObserver() {};

    static bool registerServerStatusCallback(LSHandle *hdl, LSMessage *msg, void *ctx);

    bool sendMessage(const std::string &uri, const std::string &payload, DbObserverCallback cb, void *ctx);
    
private:
    const std::string serverStatusUrl_ = "luna://com.webos.service.bus/signal/registerServerStatus";
    const std::string serviceName_ = "com.webos.mediadb";
    LSHandle *handle_;
    db_initialized_callback_t dbInitialCallback_;
};

