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
typedef bool (*LocaleObserverCallback) (LSHandle *hdl, LSMessage *msg, void *ctx);

class LocaleObserver
{
public:
    typedef std::function<void(std::string &)> notify_callback_t;
    LocaleObserver(LSHandle *hdl, notify_callback_t && notify_callback);
    LocaleObserver() {};

    static bool locateSettingsCallback(LSHandle *hdl, LSMessage *msg, void *ctx);

    bool sendMessage(const std::string &uri, const std::string &payload, LocaleObserverCallback cb, void *ctx);

private:
    LOG_MSGID;
    const std::string url_ = "luna://com.webos.settingsservice/getSystemSettings";
    std::string locale;
    LSHandle *handle_ = nullptr;
    notify_callback_t notifyCallback_ = nullptr;
};

