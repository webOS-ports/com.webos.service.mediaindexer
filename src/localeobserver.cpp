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

#include "localeobserver.h"

LocaleObserver::LocaleObserver(LSHandle *hdl, notify_callback_t && notify_callback)
    : handle_(hdl), notifyCallback_(notify_callback)
{
    pbnjson::JValue payload = pbnjson::Object();
    pbnjson::JValue array = pbnjson::Array();
    array.append(std::string("localeInfo"));
    payload.put("keys", array);
    payload.put("subscribe", true);
    sendMessage(url_, payload.stringify(), locateSettingsCallback, this);
}

bool LocaleObserver::locateSettingsCallback(LSHandle * hdl, LSMessage * msg, void * ctx)
{
    LocaleObserver * self = static_cast<LocaleObserver*>(ctx);
    pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());
    const char *payload = LSMessageGetPayload(msg);
    LOG_DEBUG(MEDIA_INDEXER_LOCALEOBSERVER, "payload : %s",payload);
    if (!parser.parse(payload)) {
            LOG_ERROR(MEDIA_INDEXER_LOCALEOBSERVER, 0, "Invalid JSON message: %s", payload);
            return false;
    }

    pbnjson::JValue parsed = parser.getDom();
    if (parsed.hasKey("returnValue") && parsed["returnValue"].asBool()) {
        if (parsed.hasKey("settings")) {
            pbnjson::JValue settings = parsed["settings"];
            if (settings.hasKey("localeInfo")) {
                pbnjson::JValue localeInfo = settings["localeInfo"];
                if (localeInfo.hasKey("locales")) {
                    if (localeInfo["locales"].hasKey("UI")) {
                        self->locale = localeInfo["locales"]["UI"].asString();
                        LOG_INFO(MEDIA_INDEXER_LOCALEOBSERVER, 0, "Locale info : %s", self->locale.c_str());
                        if (self->notifyCallback_)
                            self->notifyCallback_(self->locale);
                    }
                }
            }
        }
    }
    
    return true;
}

bool LocaleObserver::sendMessage(const std::string &uri, const std::string &payload, LocaleObserverCallback cb, void *ctx)
{
    lunaError_t lunaErr;
    if (!LSCall(handle_, uri.c_str(), payload.c_str(), cb, ctx, NULL, &lunaErr)) {
        LOG_ERROR(MEDIA_INDEXER_LOCALEOBSERVER, 0, "Failed to send message, uri : %s, payload : %s",uri.c_str(), payload.c_str());
        return false;
    }
    return true;
}

