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

#include "dbobserver.h"

DbObserver::DbObserver(LSHandle *hdl, db_initialized_callback_t && db_inital_callback)
    : handle_(hdl), dbInitialCallback_(db_inital_callback)
{
    pbnjson::JValue payload = pbnjson::Object();
    payload.put("serviceName", serviceName_);
    payload.put("subscribe", true);
    sendMessage(serverStatusUrl_, payload.stringify(), registerServerStatusCallback, this);
}

bool DbObserver::registerServerStatusCallback(LSHandle * hdl, LSMessage * msg, void * ctx)
{
    DbObserver * self = static_cast<DbObserver*>(ctx);
    pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());
    const char *payload = LSMessageGetPayload(msg);
    LOG_DEBUG(MEDIA_INDEXER_DBOBSERVER, "payload : %s",payload);
    if (!parser.parse(payload)) {
            LOG_ERROR(MEDIA_INDEXER_DBOBSERVER, 0, "Invalid JSON message: %s", payload);
            return false;
    }

    pbnjson::JValue parsed = parser.getDom();
    if (parsed.hasKey("serviceName") && parsed.hasKey("connected")) {
        std::string serviceName = parsed["serviceName"].asString();
        bool isconnected = parsed["connected"].asBool();
        if (serviceName == self->serviceName_ && isconnected) {
            self->dbInitialCallback_();
        }
    }
    return true;
}

bool DbObserver::sendMessage(const std::string &uri, const std::string &payload, DbObserverCallback cb, void *ctx)
{
    bool ret = true;
    lunaError_t lunaErr;
    if (!LSCall(handle_, uri.c_str(), payload.c_str(), cb, ctx, NULL, &lunaErr)) {
        LOG_ERROR(MEDIA_INDEXER_DBOBSERVER, 0, "Failed to send message, uri : %s, payload : %s",uri.c_str(), payload.c_str());
        return false;
    }
    return true;
}

