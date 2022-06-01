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

#include "lunaconnector.h"

bool LunaConnector::isTaskStarted_ = false;
std::mutex LunaConnector::syncCallbackLock_;
std::mutex LunaConnector::CallbackLock_;

LunaConnector::LunaConnector(const std::string& name, bool async)
    : serviceName_(name),
      mainLoop_(nullptr),
      mainContext_(nullptr),
      handle_(nullptr),
      token_(LSMESSAGE_TOKEN_INVALID),
      userData_(nullptr),
      stopped_(false),
      async_(async)
{
    lunaError_t lunaErr;
    LOG_DEBUG(MEDIA_INDEXER_LUNACONNECTOR, "[LunaConnector] Ctor for service name : %s", name.c_str());
    if (serviceName_.empty()) {
        LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "[ERROR] Ctor of LunaConnector : Invalid service name");
        return;
    }

    if (!async) {
        mainContext_ = g_main_context_ref_thread_default();
    } else {
        mainContext_ = g_main_context_new();
        mainLoop_ = g_main_loop_new(mainContext_, FALSE);
        g_main_context_ref(mainContext_);
    }

    auto handleFailure = [this] (const std::string & log) {
        LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "[ERROR] Ctor of LunaConnector : %s", log.c_str());
        g_main_context_unref(mainContext_);
        if( mainLoop_ )
            g_main_loop_unref(mainLoop_);
    };

    if (LSRegister(serviceName_.c_str(), &handle_, &lunaErr))
    {
        if (!LSRegisterCategory(handle_,"/",NULL,NULL,NULL,&lunaErr))
            handleFailure("Failed occurred LSRegisterCategory");

        if (!LSCategorySetData(handle_, "/", (void *)(this), &lunaErr))
            handleFailure("Failed occurred LSCategorySetData with this");

        if (!LSGmainContextAttach(handle_, mainContext_, &lunaErr))
            handleFailure("Failed occurred LSGmainContextAttach");
    } else {
        handleFailure("Fail occurred in LSRegister");
        return;
    }

    if (async_)
        loopAsync();
    g_main_context_unref(mainContext_);
    LOG_DEBUG(MEDIA_INDEXER_LUNACONNECTOR, "[LunaConnector] Ctor Done");
}

LunaConnector::~LunaConnector()
{
    lunaError_t lunaErr;
    stop();

    if (task_.joinable())
        task_.join();
    isTaskStarted_ = false;
    if (!handle_) {
        LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "[ERROR] Dtor of LunaConnector : LSHandle is invalid");
        return;
    }
    g_main_loop_quit(mainLoop_);
    g_main_context_unref(mainContext_);

    if (!LSUnregister(handle_, &lunaErr)) {
        LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "[ERROR] Dtor of LunaConnector : Fail occurred in LSUnregister");
        return;
    }

    if (mainLoop_)
        g_main_loop_unref(mainLoop_);
}

bool LunaConnector::run()
{
    if (!stopped_) {
        LOG_DEBUG(MEDIA_INDEXER_LUNACONNECTOR, "LunaConnector loop started");
        isTaskStarted_ = true;
        cv_.notify_one();
        if (mainLoop_)
            g_main_loop_run(mainLoop_);
    }

    return true;
}

bool LunaConnector::stop()
{
    if (stopped_)
        return true;

    stopped_ = true;

    if (mainLoop_) {
        GSource * gSrc = g_timeout_source_new(0);
        g_source_set_callback(
            gSrc ,
            [] (void * l) { g_main_loop_quit((GMainLoop*)l); return 0; },
            mainLoop_,
            NULL
        );
        g_source_attach(gSrc, g_main_loop_get_context(mainLoop_));
        g_source_unref(gSrc);
    }
    return true;
}

bool LunaConnector::_syncCallback(LSHandle *hdl, LSMessage *msg, void *ctx)
{
    bool ret = true;
    std::lock_guard<std::mutex> syncLock_(syncCallbackLock_);
    LOG_DEBUG(MEDIA_INDEXER_LUNACONNECTOR, "Get response from sender");
    LOG_DEBUG(MEDIA_INDEXER_LUNACONNECTOR, "Sender Service Name : %s", LSMessageGetSenderServiceName(msg));
    LOG_DEBUG(MEDIA_INDEXER_LUNACONNECTOR, "Message : %s", LSMessageGetPayload(msg));

    CallbackWrapper *wrapper = static_cast<CallbackWrapper *>(ctx);
    if (!wrapper) {
        LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Fatal Error : sync callback wrapper broken");
        wrapper->wakeUp();
        return false;
    }
    LSMessageRef(msg);
    if (!wrapper->callback(hdl, msg)) {
        LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Fail occurred in sync callback function");
        ret = false;
    }
    LSMessageUnref(msg);
    wrapper->wakeUp();
    return ret;
}

bool LunaConnector::_Callback(LSHandle *hdl, LSMessage *msg, void *ctx)
{
    bool ret = true;
    std::lock_guard<std::mutex> Lock_(CallbackLock_);
    CallbackWrapper *wrapper = static_cast<CallbackWrapper *>(ctx);

    if (wrapper) {
        LSMessageRef(msg);
        if (!wrapper->callback(hdl, msg)) {
            LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Fail occurred in callback function");
            ret = false;
        }
        LSMessageUnref(msg);
    } else {
        LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Fatal Error : callback wrapper broken");
        ret = false;
    }
    return ret;
}


bool LunaConnector::sendMessage(const std::string &uri, const std::string &payload,
                                LunaConnectorCallback cb, void * ctx,
                                bool async, LSMessageToken *token, void *obj,
                                std::string forcemethod, std::string indexerMethod)
{
    lunaError_t lunaErr;
    LSMessageToken *msgToken = (token == nullptr) ? &token_ : token;
    std::string method = forcemethod.empty() ? uri.substr(uri.find_last_of('/') + 1) : forcemethod;
    LOG_DEBUG(MEDIA_INDEXER_LUNACONNECTOR, "uri : %s, payload : %s, async : %d, method : %s", 
            uri.c_str(), payload.c_str(), async, method.c_str());
    if (!async_) {
        if (!LSCallOneReply(handle_, uri.c_str(), payload.c_str(),
                            cb, ctx, msgToken, &lunaErr)) {
            LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Failed to send message %s", payload.c_str());
            LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Error Message : %s", lunaErr.message);
            return false;
        }

        if (tokenCallback_)
            tokenCallback_(*msgToken, method, indexerMethod, obj);
        return true;
    } else {
        if (!isTaskStarted_) {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk,[&]{return isTaskStarted_ == true;});
        }

        std::unique_lock<std::mutex> lock_(callbackWrapper.getMutex());
        callbackWrapper.setHandler(cb, ctx);

        if (async) {
            std::lock_guard<std::mutex> Lock_(CallbackLock_);
            if (!LSCallOneReply(handle_, uri.c_str(), payload.c_str(),
                                _Callback, &callbackWrapper,
                                msgToken, &lunaErr)) {
                LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Failed to send message %s", payload.c_str());
                LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Error Message : %s", lunaErr.message);
                return false;
            }
            if (tokenCallback_)
                tokenCallback_(*msgToken, method, indexerMethod, obj);
        } else {
            {
                std::lock_guard<std::mutex> syncLock_(syncCallbackLock_);
                if (!LSCallOneReply(handle_, uri.c_str(), payload.c_str(),
                                    _syncCallback, &callbackWrapper,
                                    msgToken, &lunaErr)) {
                    LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Failed to send message %s", payload.c_str());
                    LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Error Message : %s", lunaErr.message);
                    return false;
                }
                if (tokenCallback_)
                    tokenCallback_(*msgToken, method, indexerMethod, obj);
            }
            if (callbackWrapper.wait(lock_)) {
                LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Sync handler timeout!");
                if (tokenCancelCallback_)
                    tokenCancelCallback_(*msgToken, nullptr);
                return false;
            }

        }
    }
    return true;
}

bool LunaConnector::sendResponse(LSHandle * sender, LSMessage * message, const std :: string & object)
{
    lunaError_t lunaErr;
    if (!LSMessageReply(sender, message, object.c_str(), &lunaErr)) {
        LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Message reply error");
        LOG_ERROR(MEDIA_INDEXER_LUNACONNECTOR, 0, "Error Message : %s", lunaErr.message);
        return false;
    }
    return true;
}

void* LunaConnector::messageThread(void * ctx)
{
    LunaConnector* self = static_cast<LunaConnector *>(ctx);
    LOG_DEBUG(MEDIA_INDEXER_LUNACONNECTOR, "messageThread %d Start!", gettid());
    self->run();
    return NULL;
}

void LunaConnector::loopAsync()
{
    task_ = std::thread(&LunaConnector::messageThread, this);
    task_.detach();
}


