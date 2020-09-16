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
            LOG_ERROR(0, format, ##args); \
            return rv; \
        } \
        }

#define LSERROR_CHECK_AND_PRINT(ret, lsError) \
    do { \
        if (!ret) { \
            LSErrorPrintAndFree(&lsError); \
            return false; \
        } \
    } while (0)

/// From main.cpp.
extern const char *lunaServiceId;
std::mutex IndexerService::mutex_;
std::mutex IndexerService::scanMutex_;
constexpr int SCAN_TIMEOUT = 10;

LSMethod IndexerService::serviceMethods_[] = {
    { "runDetect", IndexerService::onRun, LUNA_METHOD_FLAGS_NONE },
    { "stopDetect", IndexerService::onStop, LUNA_METHOD_FLAGS_NONE },
    { "getPlugin", IndexerService::onPluginGet, LUNA_METHOD_FLAGS_NONE },
    { "putPlugin", IndexerService::onPluginPut, LUNA_METHOD_FLAGS_NONE },
    { "getPluginList", IndexerService::onPluginListGet, LUNA_METHOD_FLAGS_NONE },
    { "getMediaDbPermission", IndexerService::onMediaDbPermissionGet, LUNA_METHOD_FLAGS_NONE },
    { "getDeviceList", IndexerService::onDeviceListGet, LUNA_METHOD_FLAGS_NONE },
    { "getAudioList", IndexerService::onAudioListGet, LUNA_METHOD_FLAGS_NONE },
    { "getAudioMetadata", IndexerService::onGetAudioMetadata, LUNA_METHOD_FLAGS_NONE },
    { "getVideoList", IndexerService::onGetVideoList, LUNA_METHOD_FLAGS_NONE },
    { "getVideoMetadata", IndexerService::onGetVideoMetadata, LUNA_METHOD_FLAGS_NONE },
    { "getImageList", IndexerService::onGetImageList, LUNA_METHOD_FLAGS_NONE },
    { "getImageMetadata", IndexerService::onGetImageMetadata, LUNA_METHOD_FLAGS_NONE },
    { "requestDelete", IndexerService::onRequestDelete, LUNA_METHOD_FLAGS_NONE },
    { "requestMediaScan", IndexerService::onRequestMediaScan, LUNA_METHOD_FLAGS_NONE },
    {NULL, NULL}
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
        "  },"
        "  \"required\": [ \"uri\", \"subscribe\" ]"
        "}"));

IndexerService::IndexerService(MediaIndexer *indexer) :
    indexer_(indexer)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::IndexerService");
    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSRegister(lunaServiceId, &lsHandle_, &lsError)) {
        LOG_CRITICAL(0, "Unable to register at luna-bus");
        return;
    }

    if (!LSRegisterCategory(lsHandle_, "/", serviceMethods_, NULL, NULL,
            &lsError)) {
        LOG_CRITICAL(0, "Unable to register top level category");
        return;
    }

    if (!LSCategorySetData(lsHandle_, "/", this, &lsError)) {
        LOG_CRITICAL(0, "Unable to set data on top level category");
        return;
    }

    if (!LSGmainAttach(lsHandle_, indexer_->mainLoop_, &lsError)) {
        LOG_CRITICAL(0, "Unable to attach service");
        return;
    }

    if (!LSSubscriptionSetCancelFunction(lsHandle_, 
                                         &IndexerService::callbackSubscriptionCancel,
                                         this, &lsError)) {
        LOG_CRITICAL(0, "Unable to set subscription cancel");
        return;
    }

    /// @todo Implement bus disconnect handler.

    PdmListener::init(lsHandle_);
    DbConnector::init(lsHandle_);
    auto dbInitialized = [&] () -> void {
        MediaDb::instance();
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
    LOG_INFO(0, "[OYJ_DBG] IndexerService::~IndexerService");
    if (!lsHandle_)
        return;

    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSUnregister(lsHandle_, &lsError)) {
        LOG_ERROR(0, "Service unregister failed");
    }

    if (dbObserver_)
        delete dbObserver_;

    //if (localeObserver_)
    //  delete localeObserver_;
    
    clientMgr_.reset();
}

bool IndexerService::pushDeviceList(LSMessage *msg)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::pushDeviceList");
    if (msg) {
        // parse incoming message
        const char *payload = LSMessageGetPayload(msg);
        pbnjson::JDomParser parser;

        if (!parser.parse(payload, deviceListGetSchema_)) {
            LOG_ERROR(0, "Invalid getDeviceList request: %s", payload);
            return false;
        }
        LOG_DEBUG("Valid getDeviceList request");

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

            // now get the meta data
            for (auto type = Device::Meta::Name;
                 type < Device::Meta::EOL; ++type) {
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
            LOG_ERROR(0, "Message reply error");
            return false;
        }
    } else {
        if (!LSSubscriptionReply(lsHandle_, "getDeviceList",
                reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(0, "Subscription reply error");
            return false;
        }
    }
    return true;
}

bool IndexerService::onPluginGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onPluginGet");
    IndexerService *is = static_cast<IndexerService *>(ctx);
    return is->pluginPutGet(msg, true);
}

bool IndexerService::onPluginPut(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onPluginPut");
    IndexerService *is = static_cast<IndexerService *>(ctx);
    return is->pluginPutGet(msg, false);
}

bool IndexerService::onPluginListGet(LSHandle *lsHandle, LSMessage *msg,
    void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onPluginListGet");
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
        LOG_ERROR(0, "Message reply error");
        return false;
    }

    return true;
}

bool IndexerService::onDeviceListGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onDeviceListGet");
    IndexerService *is = static_cast<IndexerService *>(ctx);
    // TODO
    return is->pushDeviceList(msg);
}

bool IndexerService::onRun(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onRun");
    IndexerService *is = static_cast<IndexerService *>(ctx);
    return is->detectRunStop(msg, true);
}

bool IndexerService::onStop(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onStop");
    IndexerService *is = static_cast<IndexerService *>(ctx);
    return is->detectRunStop(msg, false);
}

bool IndexerService::onMediaDbPermissionGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onMediaDbPermissionGet");
    LOG_DEBUG("call onMediaDbPermissionGet");
    std::string uri;
    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(0, "Invalid %s request: %s", method.c_str(),
            payload);
        return false;
    }

    auto domTree(parser.getDom());

    MediaDb *mdb = MediaDb::instance();
    auto reply = pbnjson::Object();
    std::lock_guard<std::mutex> lk(mutex_);
    if (mdb) {
        if (!domTree.hasKey("serviceName")) {
            LOG_ERROR(0, "serviceName field is mandatory input");
            mdb->putRespObject(false, reply, -1, "serviceName field is mandatory input");
            mdb->sendResponse(lsHandle, msg, reply.stringify());
            return false;
        }
        std::string serviceName = domTree["serviceName"].asString();
        if (serviceName.empty()) {
            LOG_ERROR(0, "empty string input");
            mdb->putRespObject(false, reply, -1, "empty string input");
            mdb->sendResponse(lsHandle, msg, reply.stringify());
            return false;
        }
        mdb->grantAccessAll(serviceName, true, reply);
        mdb->sendResponse(lsHandle, msg, reply.stringify());
    } else {
        LOG_ERROR(0, "Failed to get instance of Media Db");
        reply.put("returnValue", false);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid MediaDb Object");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(0, "Message reply error");
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
                                         const std::string &metaData)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::notifyMediaMetaData()");
    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSSubscriptionReply(lsHandle_, method.c_str(), metaData.c_str(), &lsError)) {
        LOG_ERROR(0, "subscription reply error!");
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
    LOG_INFO(0, "[OYJ_DBG] IndexerService::callbackSubscriptionCancel!");
    IndexerService* is = static_cast<IndexerService *>(ctx);

    if (is == NULL) {
        LOG_ERROR(0, "Subscription cancel callback context is invalid %p", ctx);
        return false;
    }

    LSMessageToken token = LSMessageGetToken(msg);
    std::string method = LSMessageGetMethod(msg);
    std::string sender = LSMessageGetSender(msg);
    bool ret = is->removeClient(sender, method, token);
    return ret;
}


bool IndexerService::onAudioListGet(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onAudioListGet()");
    // parse incoming message
    std::string senderName = LSMessageGetSenderServiceName(msg);
    const char *payload = LSMessageGetPayload(msg);

    pbnjson::JDomParser parser;
    // TODO: apply listSchema
    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(0, "Invalid request: payload[%s] sender[%s]",
                payload, senderName.c_str());
        return false;
    }

    // initial reply to prevent application blocking
    auto reply = pbnjson::Object();
    bool subscribe = LSMessageIsSubscription(msg);
    reply.put("subscribed", subscribe);
    reply.put("returnValue", true);

    LSError lsError;
    LSErrorInit(&lsError);
    if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
        LOG_ERROR(0, "Message reply error");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        return false;
    }

    if (subscribe) {
        LOG_INFO(0, "[OYJ_DBG] Adding getAudioList subscriber '%s'",
                senderName.c_str());

        IndexerService *is = static_cast<IndexerService *>(ctx);
        std::string sender = LSMessageGetSender(msg);
        std::string method = LSMessageGetMethod(msg);
        LSMessageToken token = LSMessageGetToken(msg);

        if (!LSSubscriptionAdd(lsHandle, method.c_str(), msg, &lsError)) {
            LOG_ERROR(0, "Add subscription error");
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
            return false;
        }
//        LSSubscriptionSetCancelFunction(lshandle, &IndexerService::callbackSubscriptionCancel, (void*)this, &lserror);

        is->addClient(sender, method, token);

        // parse uri and count from application payload
        std::string uri;
        int count = 0;
        auto domTree(parser.getDom());

        if (domTree.hasKey("uri"))
            uri = domTree["uri"].asString();

        if (domTree.hasKey("count"))
            count = domTree["count"].asNumber<int32_t>();


        LOG_INFO(0, "[OYJ_DBG] getAudioList start()");
        bool ret = is->getAudioList(uri, count);
        LOG_INFO(0, "[OYJ_DBG] getAudioList end()");
        return ret;
    }

    return true;
}

bool IndexerService::getAudioList(const std::string &uri, int count)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::getAudioList()");
    MediaDb *mdb = MediaDb::instance();
    return mdb->getAudioList(uri, count);
}

bool IndexerService::onGetAudioMetadata(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onGetAudioMetadata");
    LOG_DEBUG("call onGetAudioMetadata");

    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    RETURN_IF(!domTree.hasKey("uri"), false, "client must specify uri");
    // get the playback uri for the given media item uri
    auto uri = domTree["uri"].asString();
    LOG_DEBUG("Valid %s request for uri: %s", LSMessageGetMethod(msg),
        uri.c_str());
    bool rv = false;
    auto mdb = MediaDb::instance();
    auto mparser = MediaParser::instance();
    auto reply = pbnjson::Object();
    std::lock_guard<std::mutex> lk(mutex_);
    if (mdb && mparser) {
        pbnjson::JValue resp = pbnjson::Object();
        pbnjson::JValue metadata = pbnjson::Object(); 
//        rv = mdb->getAudioList(uri, resp);
        metadata << resp["results"];
        rv = mparser->setMediaItem(uri);
        rv = mparser->extractMetaDirect(metadata);
        reply.put("metadata", metadata);
        mdb->putRespObject(rv, reply);
        mdb->sendResponse(lsHandle, msg, reply.stringify());
    } else {
        LOG_ERROR(0, "Failed to get instance of Media Db");
        reply.put("returnValue", false);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid MediaDb Object");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(0, "Message reply error");
        }
    }
    return rv;

}


bool IndexerService::onGetVideoList(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onGetVideoList");
    LOG_DEBUG("call onGetVideoList");
    std::string uri;
    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());

    if (domTree.hasKey("uri"))
        uri = domTree["uri"].asString();

    bool rv = true;
    auto mdb = MediaDb::instance();
    auto reply = pbnjson::Object();
    std::lock_guard<std::mutex> lk(mutex_);
    if (mdb) {
        pbnjson::JValue resp = pbnjson::Object();
        pbnjson::JValue respArray = pbnjson::Array();
        pbnjson::JValue list = pbnjson::Object();

        rv &= mdb->getVideoList(uri, list);
        if (!uri.empty())
            list.put("uri", uri.c_str());
        list.put("count", list["results"].arraySize());
        respArray.append(list);

        resp.put("videoList", respArray);
        mdb->putRespObject(rv, resp);
        mdb->sendResponse(lsHandle, msg, resp.stringify());
    } else {
        LOG_ERROR(0, "Failed to get instance of Media Db");
        rv = false;
        reply.put("returnValue", rv);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid MediaDb Object");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(0, "Message reply error");
        }
    }

    return rv;
}

/*
 Response should be displayed like below:
{code}
{
    "errorCode": 0,
    "returnValue": true,
    "errorText": "No Error",
    "audioList": [
        {
            "results": [
                {
                    "last_modified_date": "Wed Jul 12 21:07:26 2017 GMT",
                    "duration": 226,
                    "dirty": false,
                    "file_path": "file:///tmp/usb/sdg/sdg1/mediaIndexerContents/scan2/Miss_A.mp3",
                    "album": "A Class",
                    "genre": "Dance/Pop",
                    "artist": "Miss A",
                    "uri": "msc://4013-0934/tmp/usb/sdg/sdg1/mediaIndexerContents/scan2/Miss_A.mp3",
                    "title": "Good Bye Baby",
                    "file_size": 5453302,
                    "thumbnail": "/media/.thumbnail/4013-0934/1737769378748857.jpg"
                },
                {
                    "last_modified_date": "Sat Aug 20 22:49:30 2011 GMT",
                    "duration": 205,
                    "dirty": false,
                    "file_path": "file:///tmp/usb/sdg/sdg1/mediaIndexerContents/scan3/MP3_None_MPEG[44.1KHz@2ch].mp3",
                    "album": "무한도전 서해안 고속도로 가요제",
                    "genre": "",
                    "artist": "GG",
                    "uri": "msc://4013-0934/tmp/usb/sdg/sdg1/mediaIndexerContents/scan3/MP3_None_MPEG[44.1KHz@2ch].mp3",
                    "title": "바람났어 (Feat. 박봄)",
                    "file_size": 8296654,
                    "thumbnail": "/media/.thumbnail/4013-0934/1261326074966182.jpg"
                },
                {
                    "last_modified_date": "Tue Jul 28 02:07:20 2020 GMT",
                    "duration": 195,
                    "dirty": false,
                    "file_path": "file:///tmp/usb/sdg/sdg1/mediaIndexerContents/Rababa.mp3",
                    "album": "미스트롯 FINAL STAGE",
                    "genre": "성인가요",
                    "artist": "정미애",
                    "uri": "msc://4013-0934/tmp/usb/sdg/sdg1/mediaIndexerContents/Rababa.mp3",
                    "title": "라밤바",
                    "file_size": 8017226,
                    "thumbnail": "/media/.thumbnail/4013-0934/1164044871931923.jpg"
                },
                {
                    "last_modified_date": "Tue Nov 25 06:10:08 2014 GMT",
                    "duration": 260,
                    "dirty": false,
                    "file_path": "file:///tmp/usb/sdg/sdg1/mediaIndexerContents/scan1/[iSongs.info] 01 - Jalsa.mp3",
                    "album": "Jalsa - (2008)",
                    "genre": "Telugu",
                    "artist": "Baba Sehgal, Rita",
                    "uri": "msc://4013-0934/tmp/usb/sdg/sdg1/mediaIndexerContents/scan1/[iSongs.info] 01 - Jalsa.mp3",
                    "title": "[iSongs.info] 01 - Jalsa",
                    "file_size": 4243260,
                    "thumbnail": "/media/.thumbnail/4013-0934/1109780776648399.jpg"
                },
                {
                    "last_modified_date": "Wed Mar  4 00:25:00 2020 GMT",
                    "duration": 132,
                    "dirty": false,
                    "file_path": "file:///tmp/usb/sdg/sdg1/mediaIndexerContents/delete_insert/file_example_MP3_5MG.mp3",
                    "album": "YouTube Audio Library",
                    "genre": "Cinematic",
                    "artist": "Kevin MacLeod",
                    "uri": "msc://4013-0934/tmp/usb/sdg/sdg1/mediaIndexerContents/delete_insert/file_example_MP3_5MG.mp3",
                    "title": "Impact Moderato",
                    "file_size": 5289384,
                    "thumbnail": 0
                },
                {
                    "last_modified_date": "Fri Sep 11 05:06:36 2020 GMT",
                    "duration": 200,
                    "dirty": false,
                    "file_path": "file:///tmp/usb/sdg/sdg1/mediaIndexerContents/changeTitle/TitleChangeSample.mp3",
                    "album": "Camper",
                    "genre": "|\t8\\Ht > |\t8\\Ht, |\t8\\Ht > X0$ (House), |\t8\\Ht > t}/\u0004$ (Club/Dance)",
                    "artist": "Albert Kick",
                    "uri": "msc://4013-0934/tmp/usb/sdg/sdg1/mediaIndexerContents/changeTitle/TitleChangeSample.mp3",
                    "title": "Audio Sample2",
                    "file_size": 8036342,
                    "thumbnail": "/media/.thumbnail/4013-0934/3374671203630697.jpg"
                },
                {
                    "last_modified_date": "Fri Sep 11 04:57:14 2020 GMT",
                    "duration": 200,
                    "dirty": false,
                    "file_path": "file:///tmp/usb/sdg/sdg1/mediaIndexerContents/parsorTest/AlbertKick_Camper_feat _Jason_Rene.mp3",
                    "album": "Camper",
                    "genre": "|\t8\\Ht > |\t8\\Ht, |\t8\\Ht > X0$ (House), |\t8\\Ht > t}/\u0004$ (Club/Dance)",
                    "artist": "Albert Kick",
                    "uri": "msc://4013-0934/tmp/usb/sdg/sdg1/mediaIndexerContents/parsorTest/AlbertKick_Camper_feat _Jason_Rene.mp3",
                    "title": "Audio Sample2",
                    "file_size": 8036342,
                    "thumbnail": "/media/.thumbnail/4013-0934/9368499306337553.jpg"
                },
                {
                    "last_modified_date": "Tue Nov 25 06:10:08 2014 GMT",
                    "duration": 260,
                    "dirty": false,
                    "file_path": "file:///tmp/usb/sdg/sdg1/mediaIndexerContents/MetaThumbnail/01_Jalsa.mp3",
                    "album": "Jalsa - (2008)",
                    "genre": "Telugu",
                    "artist": "Baba Sehgal, Rita",
                    "uri": "msc://4013-0934/tmp/usb/sdg/sdg1/mediaIndexerContents/MetaThumbnail/01_Jalsa.mp3",
                    "title": "[iSongs.info] 01 - Jalsa",
                    "file_size": 4243260,
                    "thumbnail": "/media/.thumbnail/4013-0934/1413753745152321.jpg"
                }
            ],
            "count": 8
        }
    ]
}

{code}
 */

bool IndexerService::getVideoList(const std::string &uri, int count)
{
    /*OYJ
    MediaDb *mdb = MediaDb::instance();
    // TODO: add define guard
    if (count == 0 || count > 500) // DB8 limit is 500
        return mdb->getVideoList(uri);

    return mdb->getVideoList(uri, count);
    */
    return true;
}

bool IndexerService::onGetVideoMetadata(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onGetVideoMetadata");
    LOG_DEBUG("call onGetVideoMetadata");

    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    RETURN_IF(!domTree.hasKey("uri"), false, "client must specify uri");
    // get the playback uri for the given media item uri
    auto uri = domTree["uri"].asString();
    LOG_DEBUG("Valid %s request for uri: %s", LSMessageGetMethod(msg),
        uri.c_str());
    bool rv = false;
    auto mdb = MediaDb::instance();
    auto mparser = MediaParser::instance();
    auto reply = pbnjson::Object();
    std::lock_guard<std::mutex> lk(mutex_);
    if (mdb && mparser) {
        pbnjson::JValue resp = pbnjson::Object();
        pbnjson::JValue metadata = pbnjson::Object();
        rv = mdb->getVideoList(uri, resp);
        metadata << resp["results"];
        rv = mparser->setMediaItem(uri);
        rv = mparser->extractMetaDirect(metadata);
        reply.put("metadata", metadata);
        mdb->putRespObject(rv, reply);
        mdb->sendResponse(lsHandle, msg, reply.stringify());
    } else {
        LOG_ERROR(0, "Failed to get instance of Media Db");
        reply.put("returnValue", false);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid MediaDb Object");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(0, "Message reply error");
        }
    }
    return rv;
}

bool IndexerService::onGetImageList(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onGetImageList");
    LOG_DEBUG("call onGetImageList");
    std::string uri;
    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());

    if (domTree.hasKey("uri"))
        uri = domTree["uri"].asString();

    bool rv = true;
    auto mdb = MediaDb::instance();
    auto reply = pbnjson::Object();
    std::lock_guard<std::mutex> lk(mutex_);
    if (mdb) {
        pbnjson::JValue resp = pbnjson::Object();
        pbnjson::JValue respArray = pbnjson::Array();
        pbnjson::JValue list = pbnjson::Object();

        rv &= mdb->getImageList(uri, list);
        if (!uri.empty())
            list.put("uri", uri.c_str());
        list.put("count", list["results"].arraySize());
        respArray.append(list);

        resp.put("imageList", respArray);
        mdb->putRespObject(rv, resp);
        mdb->sendResponse(lsHandle, msg, resp.stringify());
    } else {
        LOG_ERROR(0, "Failed to get instance of Media Db");
        rv = false;
        reply.put("returnValue", rv);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid MediaDb Object");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(0, "Message reply error");
        }
    }

    return rv;
}

bool IndexerService::getImageList(const std::string &uri, int count)
{
    /*OYJ
    MediaDb *mdb = MediaDb::instance();
    // TODO: add define guard
    if (count == 0 || count > 500) // DB8 limit is 500
        return mdb->getImageList(uri);

    return mdb->getImageList(uri, count);
    */
    return true;
}

bool IndexerService::onGetImageMetadata(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::onGetImageMetadata");
    //IndexerService *is = static_cast<IndexerService *>(ctx);
    LOG_DEBUG("call onGetImageMetadata");

    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    RETURN_IF(!domTree.hasKey("uri"), false, "client must specify uri");
    // get the playback uri for the given media item uri
    auto uri = domTree["uri"].asString();
    LOG_DEBUG("Valid %s request for uri: %s", LSMessageGetMethod(msg),
        uri.c_str());
    bool rv = false;
    auto mdb = MediaDb::instance();
    auto mparser = MediaParser::instance();
    auto reply = pbnjson::Object();
    std::lock_guard<std::mutex> lk(mutex_);
    if (mdb && mparser) {
        pbnjson::JValue resp = pbnjson::Object();
        pbnjson::JValue metadata = pbnjson::Object();
        rv = mdb->getVideoList(uri, resp);
        metadata << resp["results"];
        rv = mparser->setMediaItem(uri);
        rv = mparser->extractMetaDirect(metadata);
        reply.put("metadata", metadata);
        mdb->putRespObject(rv, reply);
        mdb->sendResponse(lsHandle, msg, reply.stringify());
    } else {
        LOG_ERROR(0, "Failed to get instance of Media Db");
        reply.put("returnValue", false);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid MediaDb Object");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(0, "Message reply error");
        }
    }
    return rv;

}

bool IndexerService::onRequestDelete(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "start onRequestDelete");

    IndexerService *is = static_cast<IndexerService *>(ctx);

    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    std::string method = LSMessageGetMethod(msg);
    pbnjson::JDomParser parser;

    if (!parser.parse(payload, pbnjson::JSchema::AllSchema())) {
        LOG_ERROR(0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    RETURN_IF(!domTree.hasKey("uri"), false, "client must specify uri");

    // get the playback uri for the given media item uri
    auto uri = domTree["uri"].asString();
    bool rv = true;
    auto mdb = MediaDb::instance();
    auto reply = pbnjson::Object();
    std::lock_guard<std::mutex> lk(mutex_);
    if (mdb) {
        rv = mdb->requestDelete(uri, reply);
        mdb->putRespObject(rv, reply);
        mdb->sendResponse(lsHandle, msg, reply.stringify());
    } else {
        LOG_ERROR(0, "Failed to get instance of Media Db");
        rv = false;
        reply.put("returnValue", rv);
        reply.put("errorCode", -1);
        reply.put("errorText", "Invalid MediaDb Object");

        LSError lsError;
        LSErrorInit(&lsError);

        if (!LSMessageReply(lsHandle, msg, reply.stringify().c_str(), &lsError)) {
            LOG_ERROR(0, "Message reply error");
        }
    }
    return rv;
}

bool IndexerService::onRequestMediaScan(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    LOG_INFO(0, "start onRequestMediaScan");

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
        LOG_ERROR(0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    RETURN_IF(!domTree.hasKey("path"), false, "client must specify path");

    // get the playback uri for the given media item uri
    auto path = domTree["path"].asString();

    LOG_INFO(0, "call IndexerService onRequestMediaScan");
    Device *device = nullptr;
    bool scanned = false;
    int errorCode = 0;
    // generate response
    auto reply = pbnjson::Object();
    for (auto const &[uri, plg] : indexer_->plugins_) {
        plg->lock();
        for (auto const &[uri, dev] : plg->devices()) {
            if (plg->matchUri(dev->mountpoint(), path)) {
                LOG_INFO(0, "Media Scan start for device %s", dev->uri().c_str());
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
        LOG_ERROR(0, "Message reply error");
        return false;
    }
    return true;
}

bool IndexerService::waitForScan()
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::waitForScan");
    std::unique_lock<std::mutex> lk(scanMutex_);
    return !(scanCv_.wait_for(lk, std::chrono::seconds(SCAN_TIMEOUT)) == std::cv_status::timeout);
}

bool IndexerService::notifyScanDone()
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::notifyScanDone");
    scanCv_.notify_one();
    return true;
}

bool IndexerService::pluginPutGet(LSMessage *msg, bool get)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::pluginPutGet");
    const char *payload = LSMessageGetPayload(msg);
    pbnjson::JDomParser parser;
    LOG_DEBUG("LSMessageGetMethod : %s", LSMessageGetMethod(msg));
    if (!parser.parse(payload, get ? pluginGetSchema_ : pluginPutSchema_)) {
        LOG_ERROR(0, "Invalid %s request: %s", LSMessageGetMethod(msg),
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
        LOG_DEBUG("Valid %s request for uri: %s", LSMessageGetMethod(msg),
            uri.c_str());

        if (get)
            reply.put("returnValue", indexer_->get(uri));
        else
            reply.put("returnValue", indexer_->put(uri));
    }

    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSMessageReply(lsHandle_, msg, reply.stringify().c_str(), &lsError)) {
        LOG_ERROR(0, "Message reply error");
        return false;
    }
    return true;
}

bool IndexerService::detectRunStop(LSMessage *msg, bool run)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::detectRunStop");
    // parse incoming message
    const char *payload = LSMessageGetPayload(msg);
    pbnjson::JDomParser parser;
    if (!parser.parse(payload, detectRunStopSchema_)) {
        LOG_ERROR(0, "Invalid %s request: %s", LSMessageGetMethod(msg),
            payload);
        return false;
    }

    auto domTree(parser.getDom());
    if (domTree.hasKey("uri")) {
        auto uri = domTree["uri"].asString();
        LOG_DEBUG("Valid %s request for uri: %s", LSMessageGetMethod(msg),
            uri.c_str());
        indexer_->setDetect(run, uri);
    } else {
        LOG_DEBUG("setDetect Start");
        indexer_->setDetect(run);
    }

    // generate response
    auto reply = pbnjson::Object();
    reply.put("returnValue", true);

    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSMessageReply(lsHandle_, msg, reply.stringify().c_str(), &lsError)) {
        LOG_ERROR(0, "Message reply error");
        return false;
    }
    LOG_DEBUG("detectRunStop Done");
    return true;
}

void IndexerService::checkForDeviceListSubscriber(LSMessage *msg,
    pbnjson::JDomParser &parser)
{
    LOG_INFO(0, "[OYJ_DBG] IndexerService::checkForDeviceListSubscriber");
    auto domTree(parser.getDom());
    auto subscribe = domTree["subscribe"].asBool();

    if (!subscribe)
        return;

    LOG_INFO(0, "Adding getDeviceList subscriber '%s'",
        LSMessageGetSenderServiceName(msg));
    LSSubscriptionAdd(lsHandle_, "getDeviceList", msg, NULL);

    auto mdb = MediaDb::instance();
    std::string sn(LSMessageGetSenderServiceName(msg));
    // we still have to strip the -<pid> trailer from the service
    // name
    auto pos = sn.find_last_of("-");
    if (pos != std::string::npos)
        sn.erase(pos);
    auto reply = pbnjson::Object();
    mdb->grantAccessAll(sn, false, reply);
}

bool IndexerService::addClient(const std::string &sender, 
                               const std::string &method,
                               const LSMessageToken& token)
{
    return clientMgr_->addClient(sender, method, token);
}

bool IndexerService::removeClient(const std::string &sender,
                                  const std::string &method,
                                  const LSMessageToken& token)
{
    return clientMgr_->removeClient(sender, method, token);
}

bool IndexerService::isClientExist(const std::string &sender,
                                   const std::string &method,
                                   const LSMessageToken& token)
{
    return clientMgr_->isClientExist(sender, method, token);
}
