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

#include <pbnjson.hpp>
#include <luna-service2/lunaservice.h>
#include <glib.h>
#include <string.h>
#include <iostream>
#include <chrono>
#include <memory>
#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>
#define CONNECTOR_WAIT_TIMEOUT 5
typedef bool (*LunaConnectorCallback) (LSHandle *hdl, LSMessage *msg, void *ctx);
typedef std::function<void(LSMessageToken &token, const std::string &dbMethod,
                           const std::string &indexerMethod, void *obj)> tokenCallback_t;
typedef std::function<void(LSMessageToken & token, void *obj)> tokenCancelCallback_t;

typedef struct LunaConnectorLunaErr : LSError {
    LunaConnectorLunaErr() { LSErrorInit(this); }
    ~LunaConnectorLunaErr() { LSErrorFree(this); }
    LunaConnectorLunaErr * operator &() { LSErrorFree(this); return this; }
} lunaError_t;

class CallbackWrapper {
public:
    CallbackWrapper() : handler_(nullptr), timeout_(CONNECTOR_WAIT_TIMEOUT)         {}
    ~CallbackWrapper() {}
    void setHandler(LunaConnectorCallback cb, void *ctx) { handler_ = cb; ctx_ = ctx; }
    bool callback(LSHandle *hdl, LSMessage *msg) {
        if (handler_)
            return handler_(hdl, msg, ctx_);
        return false;
    }
    std::mutex & getMutex () { return mutex_; }
    bool wait(std::unique_lock<std::mutex> & lk)
        { return cv_.wait_for(lk, std::chrono::seconds(timeout_)) == std::cv_status::timeout; }
    void wakeUp() { cv_.notify_one(); }
private:
    LunaConnectorCallback handler_;
    void * ctx_ = nullptr;
    std::mutex mutex_;
    std::condition_variable cv_;
    uint32_t timeout_;
};

class LunaConnector
{
public:
    LunaConnector(const std::string& name, bool async = false);
    ~LunaConnector();
    void registerTokenCallback(tokenCallback_t cb) { tokenCallback_ = cb; }
    void registerTokenCancelCallback(tokenCancelCallback_t cb) { tokenCancelCallback_ = cb; }
    bool run();
    bool stop();
    bool sendMessage(const std::string &uri,
        const std::string &payload,
        LunaConnectorCallback cb, void *ctx, bool async = true, LSMessageToken *token = nullptr, void *obj = nullptr, std::string forcemethod = std::string(), std::string indexerMethod = std::string());
    /*
    bool sendMessage(const std::string &uri,
        const std::string &payload,
        LunaConnectorCallback cb, void *ctx, bool async = true, LSMessageToken *token = nullptr, void *obj = nullptr, std::string forcemethod = std::string());
    */
    bool sendResponse(LSHandle *sender, LSMessage* message,
        const std::string &object);

    static void* messageThread(void *ctx);

    void loopAsync();

    static bool _syncCallback(LSHandle *hdl, LSMessage *msg, void *ctx);
    static bool _Callback(LSHandle *hdl, LSMessage *msg, void *ctx);

private:
    std::string serviceName_;
    GMainLoop *mainLoop_;
    GMainContext *mainContext_;
    LSHandle *handle_;
    LSMessageToken token_;
    tokenCallback_t tokenCallback_;
    tokenCancelCallback_t tokenCancelCallback_;
    void * userData_;
    bool stopped_;

    std::thread task_;
    static bool isTaskStarted_;
    std::mutex mutex_;
    static std::mutex syncCallbackLock_;
    static std::mutex CallbackLock_;
    std::condition_variable cv_;
    bool async_;

    CallbackWrapper callbackWrapper;
};
