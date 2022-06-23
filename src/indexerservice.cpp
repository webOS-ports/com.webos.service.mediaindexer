// Copyright (c) 2019-2021 LG Electronics, Inc.
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

#include "mediaindexer.h"
#include "mediaparser.h"
#include "plugins/pluginfactory.h"
#include "pdmlistener/pdmlistener.h"
#include "dbconnector/dbconnector.h"
#include "mediaitem.h"
#include "mediaparser.h"
#include "dbconnector/settingsdb.h"
#include "dbconnector/devicedb.h"
#include "dbconnector/mediadb.h"
#include "indexerserviceclientsmgrimpl.h"

#include <glib.h>

#include <algorithm>
#include <chrono>
#include <thread>

#define RETURN_IF(exp,rv,format,args...) \
        { if(exp) { \
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, format, ##args); \
            return rv; \
        } \
        }

#define LSERROR_CHECK_AND_PRINT(ret, lsError) \
    do { \
        if (!ret) { \
            LSErrorPrint(&lsError, stderr); \
            LSErrorFree(&lsError); \
            return false; \
        } \
    } while (0)

/// From main.cpp.
extern const char *lunaServiceId;
std::mutex IndexerService::mutex_;
std::mutex IndexerService::scanMutex_;
constexpr int SCAN_TIMEOUT = 10;
constexpr int MAXIMUM_DB_COUNT = 500;

LSMethod IndexerService::serviceMethods_[] = {
    { "runDetect", IndexerService::onRun, LUNA_METHOD_FLAGS_NONE },
    { "stopDetect", IndexerService::onStop, LUNA_METHOD_FLAGS_NONE },
    { "getPlugin", IndexerService::onPluginGet, LUNA_METHOD_FLAGS_NONE },
    { "putPlugin", IndexerService::onPluginPut, LUNA_METHOD_FLAGS_NONE },
    { "getPluginList", IndexerService::onPluginListGet, LUNA_METHOD_FLAGS_NONE },
    { "getMediaDbPermission", IndexerService::onMediaDbPermissionGet, LUNA_METHOD_FLAGS_NONE },
    { "getDeviceList", IndexerService::onDeviceListGet, LUNA_METHOD_FLAGS_NONE },
    { "getAudioList", IndexerService::onAudioListGet, LUNA_METHOD_FLAGS_NONE },
    { "getAudioMetadata", IndexerService::onAudioMetadataGet, LUNA_METHOD_FLAGS_NONE },
    { "getVideoList", IndexerService::onVideoListGet, LUNA_METHOD_FLAGS_NONE },
    { "getVideoMetadata", IndexerService::onVideoMetadataGet, LUNA_METHOD_FLAGS_NONE },
    { "getImageList", IndexerService::onImageListGet, LUNA_METHOD_FLAGS_NONE },
    { "getImageMetadata", IndexerService::onImageMetadataGet, LUNA_METHOD_FLAGS_NONE },
    { "requestDelete", IndexerService::onRequestDelete, LUNA_METHOD_FLAGS_NONE },
    { "requestMediaScan", IndexerService::onRequestMediaScan, LUNA_METHOD_FLAGS_NONE },
    { nullptr, nullptr}
};

pbnjson::JSchema IndexerService::pluginGetSchema_(pbnjson::JSchema::fromString(
        "{ \"type\": \"object\","
        "  \"properties\": {"
        "    \"uri\": {"
        "      \"type\": \"string\" }"
        "  }"
        "}"));

pbnjson::JSchema IndexerService::pluginPutSchema_(pbnjson::JSchema::fromString(
        "{ \"type\": \"object\","
        "  \"properties\": {"
        "    \"uri\": {"
        "      \"type\": \"string\" }"
        "  },"
        "  \"required\": [ \"uri\" ]"
        "}"));

pbnjson::JSchema IndexerService::deviceListGetSchema_(
    pbnjson::JSchema::fromString(
        "{ \"type\": \"object\","
        "  \"properties\": {"
        "    \"subscribe\": {"
        "      \"type\": \"boolean\" }"
        "  },"
        "  \"required\": [ \"subscribe\" ]"
        "}"));

pbnjson::JSchema IndexerService::detectRunStopSchema_(
    pbnjson::JSchema::fromString(
        "{ \"type\": \"object\","
        "  \"properties\": {"
        "    \"uri\": {"
        "      \"type\": \"string\" }"
        "  }"
        "}"));

pbnjson::JSchema IndexerService::metadataGetSchema_(
    pbnjson::JSchema::fromString(
        "{ \"type\": \"object\","
        "  \"properties\": {"
        "    \"uri\": {"
        "      \"type\": \"string\" }"
        "  },"
        "  \"required\": [ \"uri\" ]"
        "}"));

pbnjson::JSchema IndexerService::listGetSchema_(
    pbnjson::JSchema::fromString(
        "{ \"type\": \"object\","
        "  \"properties\": {"
        "    \"uri\": {"
        "      \"type\": \"string\" },"
        "    \"count\": {"
        "      \"type\": \"number\" },"
        "    \"subscribe\": {"
        "      \"type\": \"boolean\" }"
        "  }"
        "}"));

IndexerService::IndexerService(MediaIndexer *indexer) :
    dbObserver_(nullptr),
    localeObserver_(nullptr),
    indexer_(indexer)
{
    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSRegister(lunaServiceId, &lsHandle_, &lsError)) {
        LOG_CRITICAL(MEDIA_INDEXER_INDEXERSERVICE, 0, "Unable to register at luna-bus");
        return;
    }

    if (!LSRegisterCategory(lsHandle_, "/", serviceMethods_, NULL, NULL,
            &lsError)) {
        LOG_CRITICAL(MEDIA_INDEXER_INDEXERSERVICE, 0, "Unable to register top level category");
        return;
    }

    if (!LSCategorySetData(lsHandle_, "/", this, &lsError)) {
        LOG_CRITICAL(MEDIA_INDEXER_INDEXERSERVICE, 0, "Unable to set data on top level category");
        return;
    }

    if (!LSGmainAttach(lsHandle_, indexer_->mainLoop_, &lsError)) {
        LOG_CRITICAL(MEDIA_INDEXER_INDEXERSERVICE, 0, "Unable to attach service");
        return;
    }

    if (!LSSubscriptionSetCancelFunction(lsHandle_,
                                         &IndexerService::callbackSubscriptionCancel,
                                         this, &lsError)) {
        LOG_CRITICAL(MEDIA_INDEXER_INDEXERSERVICE, 0, "Unable to set subscription cancel");
        return;
    }

    /// @todo Implement bus disconnect handler.

    PdmListener::init(lsHandle_);
    DbConnector::init(lsHandle_);
    auto dbInitialized = [&] () -> void {
        MediaDb *mdb = MediaDb::instance();
        // get the permission for the com.webos.service.mediaindexer
        // TODO: reply doesn't used in grantAccessAll function.
        auto reply = pbnjson::Object();
        mdb->grantAccessAll(std::string(lunaServiceId), false, reply, "putPermissions-async");
        SettingsDb::instance();
        DeviceDb::instance();
        MediaParser::instance();
        if(indexer_) {
            indexer_->addPlugin("msc");
            indexer_->addPlugin("storage");
            indexer_->setDetect(true);
        }
    };

    dbObserver_ = new DbObserver(lsHandle_, dbInitialized);
    //localeObserver_ = new LocaleObserver(lsHandle_, nullptr);

    clientMgr_ = std::make_unique<IndexerServiceClientsMgrImpl>();
}

IndexerService::~IndexerService()
{
    if (!lsHandle_)
        return;

    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSUnregister(lsHandle_, &lsError)) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Service unregister failed");
    }

    if (dbObserver_)
        delete dbObserver_;

    //if (localeObserver_)
    //  delete localeObserver_;

    clientMgr_.reset();
}

bool IndexerService::pushDeviceList(LSMessage *msg)
{
    if (msg) {
        // parse incoming message
        const char *payload = LSMessageGetPayload(msg);
        pbnjson::JDomParser parser;

        if (!parser.parse(payload, deviceListGetSchema_)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid getDeviceList request: %s", payload);
            return false;
        }
        LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "Valid getDeviceList request");

        checkForDeviceListSubscriber(msg, parser);
    }

    // generate response
    auto reply = pbnjson::Object();
    auto pluginList = pbnjson::Array();
    for (auto const &[uri, plg] : indexer_->plugins_) {
        auto plugin = pbnjson::Object();
        plugin.put("active", plg->active());
        plugin.put("uri", uri);

        auto deviceList = pbnjson::Array();
        plg->lock();
        for (auto const &[uri, dev] : plg->devices()) {
            auto device = pbnjson::Object();
            device.put("available", dev->available());
            device.put("uri", uri);
            if (dev->state() == Device::State::Parsing)
                device.put("state", dev->stateToString(Device::State::Scanning));
            else
                device.put("state", dev->stateToString(dev->state()));

            // now get the meta data
            for (auto type = Device::Meta::Name;
                 type < Device::Meta::Icon; ++type) {
                auto meta = dev->meta(type);
                device.put(Device::metaTypeToString(type), meta);
            }

            // now push the media item count for this device for every
            // given media item type
            for (auto type = MediaItem::Type::Audio;
                 type < MediaItem::Type::EOL; ++type) {
                auto cnt = dev->mediaItemCount(type);
                auto typeStr = MediaItem::mediaTypeToString(type);
                typeStr.append("Count");
                device.put(typeStr, cnt);
            }

            deviceList << device;
        }
        plg->unlock();
        plugin.put("deviceList", deviceList);

        pluginList << plugin;
    }
    reply.put("pluginList", pluginList);
    reply.put("returnValue", true);
    LSError lsError;
    LSErrorInit(&lsError);
    std::lock_guard<std::mutex> lk(mutex_);
    if (msg) {
        if (!LSMessageReply(lsHandle_, msg, reply.stringify().c_str(),
                &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
            return false;
        }
    } else {
        if (!LSSubscriptionReply(lsHandle_, "getDeviceList",
                reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Subscription reply error");
            return false;
        }
    }
    return true;
}

bool IndexerService::onPluginGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    IndexerService *is = static_cast<IndexerService *>(ctx);
    return is->pluginPutGet(msg, true);
}

bool IndexerService::onPluginPut(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    IndexerService *is = static_cast<IndexerService *>(ctx);
    return is->pluginPutGet(msg, false);
}

bool IndexerService::onPluginListGet(LSHandle *lsHandle, LSMessage *msg,
    void *ctx)
{
    // no schema check needed as we do not expect any objects/properties

    // generate response
    auto reply = pbnjson::Object();
    auto pluginList = pbnjson::Array();

    PluginFactory factory;
    const std::list<std::string> &list = factory.plugins();

    for (auto const plg : list) {
        auto plugin = pbnjson::Object();
        plugin.put("uri", plg);
        pluginList << plugin;
    }

    reply.put("pluginList", pluginList);
    reply.put("returnValue", true);

    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
        return false;
    }

    return true;
}

bool IndexerService::onDeviceListGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    IndexerService *is = static_cast<IndexerService *>(ctx);
    // TODO
    return is->pushDeviceList(msg);
}

bool IndexerService::onRun(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    IndexerService *is = static_cast<IndexerService *>(ctx);
    return is->detectRunStop(msg, true);
}

bool IndexerService::onStop(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    IndexerService *is = static_cast<IndexerService *>(ctx);
    return is->detectRunStop(msg, false);
}

bool IndexerService::onMediaDbPermissionGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "call onMediaDbPermissionGet");
    std::string uri;
    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid %s request: %s", method.c_str(),
            payload);
        return false;
    }

    auto domTree(parser.getDom());

    MediaDb *mdb = MediaDb::instance();
    auto reply = pbnjson::Object();
    std::lock_guard<std::mutex> lk(mutex_);
    if (mdb) {
        if (!domTree.hasKey("serviceName")) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "serviceName field is mandatory input");
            mdb->putRespObject(false, reply, -1, "serviceName field is mandatory input");
            mdb->sendResponse(lsHandle, msg, reply.stringify());
            return false;
        }
        std::string serviceName = domTree["serviceName"].asString();
        if (serviceName.empty()) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "empty string input");
            mdb->putRespObject(false, reply, -1, "empty string input");
            mdb->sendResponse(lsHandle, msg, reply.stringify());
            return false;
        }
        mdb->grantAccessAll(serviceName, true, reply);
        mdb->sendResponse(lsHandle, msg, reply.stringify());
    } else {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Failed to get instance of Media Db");
        reply.put("returnValue", false);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid MediaDb Object");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
        }
        return false;
    }
    return true;
}

bool IndexerService::notifySubscriber(const std::string& method, pbnjson::JValue& response)
{
    return true;
}

bool IndexerService::notifyMediaMetaData(const std::string &method,
                                         const std::string &metaData,
                                         LSMessage *msg)
{
    LSError lsError;
    LSErrorInit(&lsError);

    if (msg != nullptr) {
        if (!LSMessageRespond(msg, metaData.c_str(), &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message respond error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            LSMessageUnref(msg);
            return false;
        }
        LSMessageUnref(msg);
        return true;
    }

    // reply for subscription
    if (!LSSubscriptionReply(lsHandle_, method.c_str(), metaData.c_str(), &lsError)) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "subscription reply error!");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        return false;
    }
    return true;
}

bool IndexerService::callbackSubscriptionCancel(LSHandle *lshandle,
                                               LSMessage *msg,
                                               void *ctx)
{
    IndexerService* indexerService = static_cast<IndexerService *>(ctx);

    if (indexerService == NULL) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Subscription cancel callback context is invalid %p", ctx);
        return false;
    }

    LSMessageToken token = LSMessageGetToken(msg);
    std::string method = LSMessageGetMethod(msg);
    std::string sender = LSMessageGetSender(msg);
    bool ret = indexerService->removeClient(sender, method, token);
    return ret;
}


bool IndexerService::onAudioListGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    IndexerService *indexerService = static_cast<IndexerService *>(ctx);

    // parse incoming message
    std::string senderName = LSMessageGetSenderServiceName(msg);
    const char *payload = LSMessageGetPayload(msg);

    pbnjson::JDomParser parser;
    // TODO: apply listSchema
    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid request: payload[%s] sender[%s]",
                payload, senderName.c_str());
        return false;
    }

    // parse uri and count from application payload
    std::string uri;
    int count = 0;
    auto domTree(parser.getDom());

    if (domTree.hasKey("uri"))
        uri = domTree["uri"].asString();
    if (domTree.hasKey("count"))
        count = domTree["count"].asNumber<int32_t>();

    if (count < 0 || count > MAXIMUM_DB_COUNT) {
        auto reply = pbnjson::Object();
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid request count : %d", count);
        reply.put("returnValue", false);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid request count");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        return false;
    }

    bool subscribe = LSMessageIsSubscription(msg);
    bool ret = false;

    if (subscribe) {
        LSError lsError;
        LSErrorInit(&lsError);
        LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "Adding getAudioList subscription for '%s'", senderName.c_str());

        std::string sender = LSMessageGetSender(msg);
        std::string method = LSMessageGetMethod(msg);
        LSMessageToken token = LSMessageGetToken(msg);

        if (!LSSubscriptionAdd(lsHandle, method.c_str(), msg, &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Add subscription error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        if (!indexerService->addClient(sender, method, token)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Failed to add client: '%s'", sender.c_str());
        }

        auto reply = pbnjson::Object();
        reply.put("subscribed", subscribe);
        reply.put("returnValue", true);

        // initial reply to prevent application blocking
        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        ret = indexerService->getAudioList(uri, count);
    } else {
        // increase reference count for LSMessage.
        // this reference count will be decrease in notification callback.
        LSMessageRef(msg);
        ret = indexerService->getAudioList(uri, count, msg);
    }

    return ret;
}

bool IndexerService::getAudioList(const std::string &uri, int count, LSMessage *msg, bool expand)
{
    MediaDb *mdb = MediaDb::instance();
    return mdb->getAudioList(uri, count, msg, expand);
}

bool IndexerService::onAudioMetadataGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "call onGetAudioMetadata");
    IndexerService *indexerService = static_cast<IndexerService *>(ctx);
    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    RETURN_IF(!domTree.hasKey("uri"), false, "client must specify uri");
    // get the playback uri for the given media item uri
    auto uri = domTree["uri"].asString();
    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "Valid %s request for uri: %s", LSMessageGetMethod(msg),
        uri.c_str());
    LSMessageRef(msg);
    return indexerService->getAudioList(uri, 0, msg, true);
}


bool IndexerService::onVideoListGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    IndexerService *indexerService = static_cast<IndexerService *>(ctx);

    // parse incoming message
    std::string senderName = LSMessageGetSenderServiceName(msg);
    const char *payload = LSMessageGetPayload(msg);

    pbnjson::JDomParser parser;
    // TODO: apply listSchema
    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid request: payload[%s] sender[%s]",
                payload, senderName.c_str());
        return false;
    }

    // parse uri and count from application payload
    std::string uri;
    int count = 0;
    auto domTree(parser.getDom());

    if (domTree.hasKey("uri"))
        uri = domTree["uri"].asString();
    if (domTree.hasKey("count"))
        count = domTree["count"].asNumber<int32_t>();

    if (count < 0 || count > MAXIMUM_DB_COUNT) {
        auto reply = pbnjson::Object();
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid request count : %d", count);
        reply.put("returnValue", false);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid request count");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        return false;
    }

    bool subscribe = LSMessageIsSubscription(msg);
    bool ret = false;

    if (subscribe) {
        LSError lsError;
        LSErrorInit(&lsError);
        LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "Adding getVideoList subscription for '%s'", senderName.c_str());

        std::string sender = LSMessageGetSender(msg);
        std::string method = LSMessageGetMethod(msg);
        LSMessageToken token = LSMessageGetToken(msg);

        if (!LSSubscriptionAdd(lsHandle, method.c_str(), msg, &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Add subscription error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        if (!indexerService->addClient(sender, method, token)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Failed to add client: '%s'", sender.c_str());
        }

        auto reply = pbnjson::Object();
        reply.put("subscribed", subscribe);
        reply.put("returnValue", true);

        // initial reply to prevent application blocking
        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        ret = indexerService->getVideoList(uri, count);
    } else {
        // increase reference count for message.
        // this reference count will be decrease in notification callback.
        LSMessageRef(msg);
        ret = indexerService->getVideoList(uri, count, msg);
    }

    return ret;
}

bool IndexerService::getVideoList(const std::string &uri, int count, LSMessage *msg, bool expand)
{
    MediaDb *mdb = MediaDb::instance();
    return mdb->getVideoList(uri, count, msg, expand);
}

bool IndexerService::onVideoMetadataGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "call onGetVideoMetadata");
    IndexerService *indexerService = static_cast<IndexerService *>(ctx);
    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    RETURN_IF(!domTree.hasKey("uri"), false, "client must specify uri");
    // get the playback uri for the given media item uri
    auto uri = domTree["uri"].asString();
    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "Valid %s request for uri: %s", LSMessageGetMethod(msg),
        uri.c_str());
    LSMessageRef(msg);
    return indexerService->getVideoList(uri, 0, msg, true);

}

bool IndexerService::onImageListGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    IndexerService *indexerService = static_cast<IndexerService *>(ctx);

    // parse incoming message
    std::string senderName = LSMessageGetSenderServiceName(msg);
    const char *payload = LSMessageGetPayload(msg);

    pbnjson::JDomParser parser;
    // TODO: apply listSchema
    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid request: payload[%s] sender[%s]",
                payload, senderName.c_str());
        return false;
    }

    // parse uri and count from application payload
    std::string uri;
    int count = 0;
    auto domTree(parser.getDom());

    if (domTree.hasKey("uri"))
        uri = domTree["uri"].asString();
    if (domTree.hasKey("count"))
        count = domTree["count"].asNumber<int32_t>();

    if (count < 0 || count > MAXIMUM_DB_COUNT) {
        auto reply = pbnjson::Object();
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid request count : %d", count);
        reply.put("returnValue", false);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid request count");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        return false;
    }

    bool subscribe = LSMessageIsSubscription(msg);
    bool ret = false;

    if (subscribe) {
        LSError lsError;
        LSErrorInit(&lsError);
        LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "Adding getImageList subscription for '%s'", senderName.c_str());

        std::string sender = LSMessageGetSender(msg);
        std::string method = LSMessageGetMethod(msg);
        LSMessageToken token = LSMessageGetToken(msg);

        if (!LSSubscriptionAdd(lsHandle, method.c_str(), msg, &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Add subscription error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        if (!indexerService->addClient(sender, method, token)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Failed to add client: '%s'", sender.c_str());
        }

        auto reply = pbnjson::Object();
        reply.put("subscribed", subscribe);
        reply.put("returnValue", true);

        // initial reply to prevent application blocking
        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        ret = indexerService->getImageList(uri, count);
    } else {
        // increase reference count for message.
        // this reference count will be decrease in notification callback.
        LSMessageRef(msg);
        ret = indexerService->getImageList(uri, count, msg);
    }

    return ret;
}

bool IndexerService::getImageList(const std::string &uri, int count, LSMessage *msg, bool expand)
{
    MediaDb *mdb = MediaDb::instance();
    return mdb->getImageList(uri, count, msg, expand);
}

bool IndexerService::onImageMetadataGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "call onGetImageMetadata");
    IndexerService *indexerService = static_cast<IndexerService *>(ctx);
    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    RETURN_IF(!domTree.hasKey("uri"), false, "client must specify uri");
    // get the playback uri for the given media item uri
    auto uri = domTree["uri"].asString();
    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "Valid %s request for uri: %s", LSMessageGetMethod(msg),
        uri.c_str());
    LSMessageRef(msg);
    return indexerService->getImageList(uri, 0, msg, true);
}

bool IndexerService::onRequestDelete(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(MEDIA_INDEXER_INDEXERSERVICE, 0, "start onRequestDelete");
    IndexerService *indexerService = static_cast<IndexerService *>(ctx);

    // parse incoming message
    std::string senderName = LSMessageGetSenderServiceName(msg);
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    RETURN_IF(!domTree.hasKey("uri"), false, "client must specify uri");

    bool subscribe = LSMessageIsSubscription(msg);

    // get the playback uri for the given media item uri
    auto uri = domTree["uri"].asString();
    bool ret = false;
    if (subscribe) {
        LSError lsError;
        LSErrorInit(&lsError);
        LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "Adding requestDelete subscription for '%s'", senderName.c_str());

        std::string sender = LSMessageGetSender(msg);
        std::string method = LSMessageGetMethod(msg);
        LSMessageToken token = LSMessageGetToken(msg);

        if (!LSSubscriptionAdd(lsHandle, method.c_str(), msg, &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Add subscription error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        if (!indexerService->addClient(sender, method, token)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Failed to add client: '%s'", sender.c_str());
        }

        auto reply = pbnjson::Object();
        reply.put("subscribed", subscribe);
        reply.put("returnValue", true);

        // initial reply to prevent application blocking
        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }

        ret = indexerService->requestDelete(uri);
    } else {
        // increase reference count for message.
        // this reference count will be decrease in notification callback.
        LSMessageRef(msg);
        ret = indexerService->requestDelete(uri, msg);
    }

    return ret;
}

bool IndexerService::requestDelete(const std::string &uri, LSMessage *msg)
{
    MediaDb *mdb = MediaDb::instance();
    return mdb->requestDelete(uri, msg);
}

bool IndexerService::onRequestMediaScan(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(MEDIA_INDEXER_INDEXERSERVICE, 0, "start onRequestMediaScan");

    IndexerService *is = static_cast<IndexerService *>(ctx);
    return is->requestMediaScan(msg);
}

bool IndexerService::requestMediaScan(LSMessage *msg)
{
    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    RETURN_IF(!domTree.hasKey("path"), false, "client must specify path");

    // get the playback uri for the given media item uri
    auto path = domTree["path"].asString();

    LOG_INFO(MEDIA_INDEXER_INDEXERSERVICE, 0, "call IndexerService onRequestMediaScan");
    bool scanned = false;
    // generate response
    auto reply = pbnjson::Object();
    for (auto const &[uri, plg] : indexer_->plugins_) {
        plg->lock();
        for (auto const &[uri, dev] : plg->devices()) {
            if (dev->available() && (!dev->mountpoint().compare(0, path.size(), path))) {
                LOG_INFO(MEDIA_INDEXER_INDEXERSERVICE, 0, "Media Scan start for device %s", dev->uri().c_str());
                dev->scan();
                scanned = true;
                break;
            }
        }
        plg->unlock();
    }

    if (scanned && waitForScan()) {
        reply.put("returnValue", true);
        reply.put("errorCode", 0);
        reply.put("errorText", "No Error");
    } else {
        reply.put("returnValue", false);
        reply.put("errorCode", -1);
        reply.put("errorText", "Scan Failed");
    }
    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSMessageReply(lsHandle_, msg, reply.stringify().c_str(), &lsError)) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
        return false;
    }
    return true;
}

bool IndexerService::waitForScan()
{
    std::unique_lock<std::mutex> lk(scanMutex_);
    return !(scanCv_.wait_for(lk, std::chrono::seconds(SCAN_TIMEOUT)) == std::cv_status::timeout);
}

bool IndexerService::notifyScanDone()
{
    scanCv_.notify_one();
    return true;
}

bool IndexerService::pluginPutGet(LSMessage *msg, bool get)
{
    const char *payload = LSMessageGetPayload(msg);
    pbnjson::JDomParser parser;
    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "LSMessageGetMethod : %s", LSMessageGetMethod(msg));
    if (!parser.parse(payload, get ? pluginGetSchema_ : pluginPutSchema_)) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());

    // response message
    auto reply = pbnjson::Object();

    // if no uri is given for getPlugin we activate all plugins
    if (get && !domTree.hasKey("uri")) {
        reply.put("returnValue", indexer_->get(""));
    } else {
        auto uri = domTree["uri"].asString();
        LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "Valid %s request for uri: %s", LSMessageGetMethod(msg),
            uri.c_str());

        if (get)
            reply.put("returnValue", indexer_->get(uri));
        else
            reply.put("returnValue", indexer_->put(uri));
    }

    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSMessageReply(lsHandle_, msg, reply.stringify().c_str(), &lsError)) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
        return false;
    }
    return true;
}

bool IndexerService::detectRunStop(LSMessage *msg, bool run)
{
    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    pbnjson::JDomParser parser;
    if (!parser.parse(payload, detectRunStopSchema_)) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    if (domTree.hasKey("uri")) {
        auto uri = domTree["uri"].asString();
        LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "Valid %s request for uri: %s", LSMessageGetMethod(msg),
            uri.c_str());
        indexer_->setDetect(run, uri);
    } else {
        LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "setDetect Start");
        indexer_->setDetect(run);
    }

    // generate response
    auto reply = pbnjson::Object();
    reply.put("returnValue", true);

    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSMessageReply(lsHandle_, msg, reply.stringify().c_str(), &lsError)) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Message reply error");
        return false;
    }
    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICE, "detectRunStop Done");
    return true;
}

void IndexerService::checkForDeviceListSubscriber(LSMessage *msg,
    pbnjson::JDomParser &parser)
{
    auto domTree(parser.getDom());
    auto subscribe = domTree["subscribe"].asBool();

    if (!subscribe)
        return;

    LOG_INFO(MEDIA_INDEXER_INDEXERSERVICE, 0, "Adding getDeviceList subscriber '%s'",
        LSMessageGetSenderServiceName(msg));

    if (!LSSubscriptionAdd(lsHandle_, "getDeviceList", msg, NULL)) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICE, 0, "Add subscription error");
        return;
    }

    auto mdb = MediaDb::instance();
    std::string sn(LSMessageGetSenderServiceName(msg));
    // we still have to strip the -<pid> trailer from the service
    // name
    auto pos = sn.find_last_of("-");
    if (pos != std::string::npos)
        sn.erase(pos);
    auto reply = pbnjson::Object();
    mdb->grantAccessAll(sn, true, reply);
}

bool IndexerService::addClient(const std::string &sender,
                               const std::string &method,
                               const LSMessageToken &token)
{
    return clientMgr_->addClient(sender, method, token);
}

bool IndexerService::removeClient(const std::string &sender,
                                  const std::string &method,
                                  const LSMessageToken &token)
{
    return clientMgr_->removeClient(sender, method, token);
}

bool IndexerService::isClientExist(const std::string &sender,
                                   const std::string &method,
                                   const LSMessageToken &token)
{
    return clientMgr_->isClientExist(sender, method, token);
}

void IndexerService::putRespResult(pbnjson::JValue &obj,
                                   const bool &returnValue,
                                   const int &errorCode,
                                   const std::string &errorText)
{
    obj.put("returnValue", returnValue);
    obj.put("errorCode", errorCode);
    obj.put("errorText", errorText);
}
