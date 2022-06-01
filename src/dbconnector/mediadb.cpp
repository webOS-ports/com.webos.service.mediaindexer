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

#include "mediadb.h"
#include "mediaitem.h"
#include "imediaitemobserver.h"
#include "device.h"
#include "plugins/pluginfactory.h"
#include "plugins/plugin.h"
#include "mediaindexer.h"
#include "mediaparser.h"
#include "performancechecker.h"

#include <cstdio>
#include <gio/gio.h>
#include <cstdint>
#include <cstring>
#include <unistd.h>
std::unique_ptr<MediaDb> MediaDb::instance_;

typedef struct RespData
{
    Device *dev;
    size_t cnt;
} RespData;

MediaDb *MediaDb::instance()
{
    if (!instance_.get()) {
        instance_.reset(new MediaDb);
        //instance_->ensureKind();
        instance_->ensureKind(AUDIO_KIND);
        instance_->ensureKind(VIDEO_KIND);
        instance_->ensureKind(IMAGE_KIND);
    }
    return instance_.get();
}

MediaDb::~MediaDb()
{
    mediaItemMap_.clear();
}

bool MediaDb::handleLunaResponse(LSMessage *msg)
{
    struct SessionData sd;
    LSMessageToken token = LSMessageGetResponseToken(msg);

    if (!sessionDataFromToken(token, &sd, HDL_LUNA_CONN)) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Failed to find session data from message token %ld", (long)token);
        return false;
    }

    auto method = sd.dbServiceMethod;
    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "Received response com.webos.mediadb for: '%s'", method.c_str());

    // handle the media data exists case
    if (method == std::string("find") ||
        method == std::string("putPermissions")) {
        if (!sd.object) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid object in session data");
            return false;
        }
        // we do not need to check, the service implementation should do that
        pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());
        const char *payload = LSMessageGetPayload(msg);

        if (!parser.parse(payload)) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid JSON message: %s", payload);
            return false;
        }
        LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "payload : %s",payload);

        pbnjson::JValue domTree(parser.getDom());

        // response message
        auto reply = static_cast<pbnjson::JValue *>(sd.object);
        *reply = domTree;
    } else if (method == std::string("search")) {
        if (!sd.object) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Search should include SessionData");
            return false;
        }
        // we do not need to check, the service implementation should do that
        pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());
        const char *payload = LSMessageGetPayload(msg);

        if (!parser.parse(payload)) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid JSON message: %s", payload);
            return false;
        }

        pbnjson::JValue domTree(parser.getDom());
        // response message
        auto reply = static_cast<pbnjson::JValue *>(sd.object);
        pbnjson::JValue array = pbnjson::Array();
        if (domTree.hasKey("results")){
            auto matches = domTree["results"];
            if (matches.isArray() && matches.isValid() && !matches.isNull())
                reply->put("results", matches);
            else
                reply->put("results", array);
        } else
            reply->put("results", array);

        LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "search response payload : %s",payload);
    } else if (method == std::string("mergePut")) {
        LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "method : %s", method.c_str());
        if (sd.object) {
            MediaItemPtr mi(static_cast<MediaItem *>(sd.object));
            DevicePtr device = mi->device();
            if (device) {
                device->incrementProcessedItemCount(mi->type());
                if (device->processingDone()) {
                    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "Activate cleanup task");
                    device->activateCleanUpTask();
                }
            }
        }
    } else if (method == std::string("put") || method == std::string("unflagDirty")) {
        LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "method : %s", method.c_str());
        if (!sd.object) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Search should include SessionData");
            return false;
        }

        RespData *resp = static_cast<RespData *>(sd.object);
        if (resp) {
            Device *device = resp->dev;
            if (device) {
                device->incrementTotalProcessedItemCount(resp->cnt);
                if (device->processingDone()) {
                    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "Activate cleanup task");
                    device->activateCleanUpTask();
                }
            }
            delete resp;
        }
    } else if (method == std::string("del")) {
        if (!sd.object) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Search should include SessionData");
            return false;
        }

        pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());
        const char *payload = LSMessageGetPayload(msg);

        if (!parser.parse(payload)) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid JSON message: %s", payload);
            return false;
        }
        LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "del response payload : %s",payload);

        pbnjson::JValue domTree(parser.getDom());
        // response message
        auto reply = static_cast<pbnjson::JValue *>(sd.object);
        *reply = domTree;
    } else if (method == std::string("flushDeleteItems")) {
        LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "method : %s", method.c_str());
        if (!sd.object) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Search should include SessionData");
            return false;
        }

        RespData *resp = static_cast<RespData *>(sd.object);
        if (resp) {
            Device *device = resp->dev;
            if (device) {
                device->incrementTotalRemovedItemCount(resp->cnt);
                if (device->processingDone()) {
                    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "Activate cleanup task");
                    device->activateCleanUpTask();
                }
            }
            delete resp;
        }
    }
    return true;
}

//TODO : should be merged including above handler.
bool MediaDb::handleLunaResponseMetaData(LSMessage *msg)
{
    struct SessionData sd;
    LSMessageToken token = LSMessageGetResponseToken(msg);

    if (!sessionDataFromToken(token, &sd)) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Failed to find session data from message token %ld", (long)token);
        return false;
    }

    pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());
    const char *payload = LSMessageGetPayload(msg);

    if (!parser.parse(payload)) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid JSON message: %s", payload);
        return false;
    }

    pbnjson::JValue domTree(parser.getDom());
    pbnjson::JValue results;
    if (domTree.hasKey("results"))
        results = domTree["results"];


    auto dbServiceMethod = sd.dbServiceMethod;
    auto dbMethod = sd.dbMethod;
    auto dbQuery = sd.query;
    auto object = sd.object;
    LOG_INFO(MEDIA_INDEXER_MEDIADB, 0, "Received response com.webos.mediadb for: \
            dbServiceMethod[%s], dbMethod[%s]",
            dbServiceMethod.c_str(), dbMethod.c_str());

    const auto &it = dbMethodMap_.find(dbMethod);
    if (it == dbMethodMap_.end()) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Failed to find media db method[%s]", dbMethod.c_str());
        return false;
    }

    const auto method = it->second;

    bool ret = false;

    // getAudioList, getVideoList, getImageList are same in switch statement.
    // but it could be changed in the future. so remain it eventhough same thing.
    switch(method) {
    case MediaDbMethod::GetAudioList: {
        auto response = pbnjson::Object();
        auto result = pbnjson::Object();
        result.put("results", results);
        result.put("count", results.arraySize());
        response.put("audioList", result);
        putRespObject(true, response);
        MediaIndexer *indexer = MediaIndexer::instance();
        ret = indexer->sendMediaMetaDataNotification(dbMethod, response.stringify(),
                static_cast<LSMessage*>(object));

        if (!ret) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Notification error in GetAudioList!");
            break;
        }

        // again send search command if payload has "next" key.
        // object null means subscription
        if (domTree.hasKey("next") && object == nullptr) {
            std::string page = domTree["next"].asString();
            dbQuery.put("page", page);

            ret = search(dbQuery, dbMethod, object);
            if (!ret) {
                LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Search error!");
            }
        }
        break;
    }
    case MediaDbMethod::GetVideoList: {
        auto response = pbnjson::Object();
        auto result = pbnjson::Object();
        result.put("results", results);
        result.put("count", results.arraySize());
        response.put("videoList", result);
        putRespObject(true, response);

        MediaIndexer *indexer = MediaIndexer::instance();
        ret = indexer->sendMediaMetaDataNotification(dbMethod, response.stringify(),
                static_cast<LSMessage*>(object));

        if (!ret) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Notification error in GetVideoList!");
            break;
        }

        // again send search command if payload has "next" key.
        // object null means subscription
        if (domTree.hasKey("next") && object == nullptr) {
            std::string page = domTree["next"].asString();
            dbQuery.put("page", page);

            ret = search(dbQuery, dbMethod, object);
            if (!ret) {
                LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Search error!");
            }
        }
        break;
    }
    case MediaDbMethod::GetImageList: {
        auto response = pbnjson::Object();
        auto result = pbnjson::Object();
        result.put("results", results);
        result.put("count", results.arraySize());
        response.put("imageList", result);
        putRespObject(true, response);

        MediaIndexer *indexer = MediaIndexer::instance();
        ret = indexer->sendMediaMetaDataNotification(dbMethod, response.stringify(),
                static_cast<LSMessage*>(object));

        if (!ret) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Notification error in GetImageList!");
            break;
        }

        // again send search command if payload has "next" key.
        // object null means subscription
        if (domTree.hasKey("next") && object == nullptr) {
            std::string page = domTree["next"].asString();
            dbQuery.put("page", page);

            ret = search(dbQuery, dbMethod, object);
            if (!ret) {
                LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Search error!");
            }
        }
        break;
    }
    case MediaDbMethod::GetAudioMetaData:
    case MediaDbMethod::GetVideoMetaData:
    case MediaDbMethod::GetImageMetaData: {
        auto response = pbnjson::Object();
        auto metadata = results[0];
        MediaIndexer *indexer = MediaIndexer::instance();

        if (!metadata.hasKey("uri")) {
            putRespObject(false, response, -1, "Invalid uri");
            ret = indexer->sendMediaMetaDataNotification(dbMethod, response.stringify(),
                    static_cast<LSMessage*>(object));
            if (!ret) {
                LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Notification error in extra meta data extraction!");
            }
            break;
        }

        auto uri = metadata["uri"].asString();
        auto mparser = MediaParser::instance();
        if (mparser) {
            bool rv = false;
            rv = mparser->setMediaItem(uri);
            rv = mparser->extractExtraMeta(metadata);
            response.put("metadata", metadata);
            if (!rv)
                putRespObject(rv, response, -1, "Metadata extraction failure");
            else
                putRespObject(rv, response);
        } else {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Failed to get instance of Media parser Object");
            putRespObject(false, response, -1, "Invalid media parser object");
        }
        ret = indexer->sendMediaMetaDataNotification(dbMethod, response.stringify(),
                static_cast<LSMessage*>(object));
        if (!ret) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Notification error in extra meta data extraction!");
        }
        break;
    }
    case MediaDbMethod::RequestDelete: {
        MediaIndexer *indexer = MediaIndexer::instance();
        ret = indexer->sendMediaMetaDataNotification(dbMethod, domTree.stringify(),
                static_cast<LSMessage*>(object));
        if (!ret) {
            LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Notification error in RequestDelete!");
        }
        break;
    }
    case MediaDbMethod::RemoveDirty: {
        if (results.isArray() && results.isValid() && !results.isNull()) {
            for (auto item : results.items()) {
                auto uri = item["uri"].asString();
                auto thumbnail = item["thumbnail"].asString();
                auto kind = item["_kind"].asString();

                if (!uri.empty()) {
                    auto where = pbnjson::Array();
                    prepareWhere(URI, uri, true, where);
                    auto query = pbnjson::Object();
                    query.put("from", kind);
                    query.put("where", where);
                    ret = del(query, dbMethod);

                    if (!ret)
                        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "ERROR deleting mediaDB uri : [%s]",
                                uri.c_str());

                }

                if (!thumbnail.empty()) {
                    int remove_ret = std::remove(thumbnail.c_str());

                    if (remove_ret != 0)
                        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Error deleting thumbnail file : [%s]",
                                thumbnail.c_str());

                    sync();
                }

            }
        }
        ret = true;
        break;
    }
    default: {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Unknown db method[%s]", dbMethod.c_str());
        ret = false;
        break;
    }
    } // end of switch

    return ret;

}

void MediaDb::checkForChange(MediaItemPtr mediaItem)
{
    auto mi = mediaItem.get();
    auto uri = mediaItem->uri();
    auto hash = mediaItem->hash();
    if ((mediaItemMap_.find(uri) == mediaItemMap_.end()) ||
        ((mediaItemMap_.find(uri) != mediaItemMap_.end()) && (mediaItemMap_[uri] != hash))) {
        mediaItemMap_[uri] = hash;
        find(mediaItem->uri(), true, mi, "", false);
    }

    mediaItem.reset();
}

bool MediaDb::needUpdate(MediaItem *mediaItem)
{
    bool ret = false;
    if (!mediaItem) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid input");
        return false;
    }
    pbnjson::JValue resp = pbnjson::Object();
    std::string kind = "";
    if (mediaItem->type() != MediaItem::Type::EOL)
        kind = kindMap_[mediaItem->type()];
    do {
        ret = find(mediaItem->uri(), true, &resp, kind, true);
    } while(!ret);

    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "find result for %s : %s",mediaItem->uri().c_str(), resp.stringify().c_str());

    if (!resp.hasKey("results")) {
        LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "New media item '%s' needs meta data", mediaItem->uri().c_str());
        return true;
    }

    auto matches = resp["results"];

    // sanity check
    if (!matches.isArray() || matches.arraySize() == 0) {
        return true;
    }

    // gotcha
    auto match = matches[0];

    if (!match.hasKey("uri") || !match.hasKey("hash")) {
        LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "Current db data is insufficient, need update");
        return true;
    }

    auto uri = match["uri"].asString();
    auto hashStr = match["hash"].asString();
    auto hash = std::stoul(hashStr);

    // check if media item has changed since last visited
    if (mediaItem->hash() != hash) {
        LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "Media item '%s' hash changed, request meta data update",
            mediaItem->uri().c_str());
        return true;
    }

    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "Media item '%s' doesn't need to be changed", mediaItem->uri().c_str());
    return false;
}

void MediaDb::updateMediaItem(MediaItemPtr mediaItem)
{
    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "%s Start for mediaItem uri : %s",__FUNCTION__, mediaItem->uri().c_str());
    // update or create the device in the database
    if (mediaItem->type() == MediaItem::Type::EOL) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid media type");
        return;
    }
    auto props = pbnjson::Object();
    props.put(URI, mediaItem->uri());
    props.put(HASH, std::to_string(mediaItem->hash()));
    props.put(DIRTY, false);
    //typeProps.put(TYPE, mediaItem->mediaTypeToString(mediaItem->type()));
    //typeProps.put(MIME, mediaItem->mime());
    auto filepath = getFilePath(mediaItem->uri());
    props.put(FILE_PATH, filepath ? filepath.value() : "");

    std::string kind_type = kindMap_[mediaItem->type()];

    for (auto meta = MediaItem::Meta::Title; meta < MediaItem::Meta::Track; ++meta) {
        auto metaStr = mediaItem->metaToString(meta);
        auto data = mediaItem->meta(meta);

        //Todo - Only media kind columns should be stored.
        //if(mediaItem->isMediaMeta(meta))
        //mediaItem->putProperties(metaStr, data, props);

        if ((mediaItem->type() == MediaItem::Type::Audio && mediaItem->isAudioMeta(meta))
            ||(mediaItem->type() == MediaItem::Type::Video && mediaItem->isVideoMeta(meta))
            ||(mediaItem->type() == MediaItem::Type::Image && mediaItem->isImageMeta(meta))) {
            mediaItem->putProperties(metaStr, data, props);
        }
    }
    //mergePut(mediaItem->uri(), true, props, nullptr, MEDIA_KIND);
    auto dev = mediaItem->device();
    if (dev->isNewMountedDevice()) {
        props.put("_kind", kind_type);
        putMeta(props, dev);
    } else {
        auto uri = mediaItem->uri();
        // release ownership for this mediaItem.
        auto mi = mediaItem.release();
        mergePut(uri, true, props, mi, kind_type);
    }
}

std::optional<std::string> MediaDb::getFilePath(
    const std::string &uri) const
{
    // check if the plugin is available and get it
    auto plg = PluginFactory().plugin(uri);
    if (!plg)
        return std::nullopt;

    return plg->getPlaybackUri(uri);
}

bool MediaDb::putMeta(pbnjson::JValue &params, DevicePtr device)
{
    auto uri = device->uri();
    std::unique_lock<std::mutex> lk(mutex_);
    if (firstScanTempBuf_.find(uri) == firstScanTempBuf_.end()) {
        firstScanTempBuf_.emplace(uri, pbnjson::Array());
    }
    firstScanTempBuf_[uri] << params;
    device->incrementPutItemCount();
    //LOG_PERF("array size : %d", firstScanTempBuf_[uri].arraySize());
    if(firstScanTempBuf_[uri].arraySize() >= FLUSH_COUNT || device->needFlushed())
        flushPut(device.get());
    return true;
}

bool MediaDb::flushPut(Device* device)
{
    if (device) {
        auto uri = device->uri();
        if (firstScanTempBuf_.find(uri) != firstScanTempBuf_.end()) {
            if (firstScanTempBuf_[uri].arraySize() > 0) {
                RespData *obj = new RespData {device, static_cast<size_t>(firstScanTempBuf_[uri].arraySize())};
                put(firstScanTempBuf_[uri], (void *)obj);
                while (firstScanTempBuf_[uri].arraySize() > 0)
                    firstScanTempBuf_[uri].remove(ssize_t(0));
            }
        }
    } else {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid input device");
        return false;
    }
    return true;
}

void MediaDb::markDirty(std::shared_ptr<Device> device, MediaItem::Type type)
{
    // update or create the device in the database
    auto props = pbnjson::Object();
    props.put(DIRTY, true);

    //mergePut(device->uri(), false, props);
    if (type == MediaItem::Type::EOL) {
        merge(AUDIO_KIND, props, URI, device->uri(), false);
        merge(VIDEO_KIND, props, URI, device->uri(), false);
        merge(IMAGE_KIND, props, URI, device->uri(), false);
    } else
        merge(kindMap_[type], props, URI, device->uri(), false);
}

void MediaDb::unmarkAllDirty(std::shared_ptr<Device> device, MediaItem::Type type)
{
    // update or create the device in the database
    auto props = pbnjson::Object();
    props.put(DIRTY, false);

    //mergePut(device->uri(), false, props);
    if (type == MediaItem::Type::EOL) {
        merge(AUDIO_KIND, props, URI, device->uri(), false, nullptr, true);
        merge(VIDEO_KIND, props, URI, device->uri(), false, nullptr, true);
        merge(IMAGE_KIND, props, URI, device->uri(), false, nullptr, true);
    } else
        merge(kindMap_[type], props, URI, device->uri(), false, nullptr, true);
}

void MediaDb::unflagDirty(MediaItemPtr mediaItem)
{
    std::string uri = mediaItem->uri();
    MediaItem::Type type = mediaItem->type();
    if (type == MediaItem::Type::EOL) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "ERROR : Media Item type for uri %s should not be EOL", uri.c_str());
        return;
    }

    auto query = pbnjson::Object();
    query.put("from", kindMap_[type]);

    auto wheres = pbnjson::Array();
    prepareWhere(URI, uri, true, wheres);
    query.put("where", wheres);

    auto props = pbnjson::Object();
    props.put(DIRTY, false);

    auto param = pbnjson::Object();
    param.put("query", query);
    param.put("props", props);

    auto device = mediaItem->device();
    auto duri = device->uri();
    if (reScanTempBuf_.find(duri) == reScanTempBuf_.end()) {
        reScanTempBuf_.emplace(duri, pbnjson::Array());
    }
    prepareOperation("merge", param, reScanTempBuf_[duri]);
    device->incrementDirtyItemCount();
    if (reScanTempBuf_[duri].arraySize() >= FLUSH_COUNT) {
        flushUnflagDirty(device.get());
    }
}

void MediaDb::flushUnflagDirty(Device *device)
{
    std::unique_lock<std::mutex> lk(mutex_);
    if (device) {
        auto uri = device->uri();
        if (reScanTempBuf_.find(uri) != reScanTempBuf_.end()) {
            if (reScanTempBuf_[uri].arraySize() > 0) {

                RespData *obj = new RespData {device, static_cast<size_t>(reScanTempBuf_[uri].arraySize())};
                batch(reScanTempBuf_[uri], "unflagDirty", (void *)obj);

                while (reScanTempBuf_[uri].arraySize() > 0)
                    reScanTempBuf_[uri].remove(ssize_t(0));
            }
        }
    } else {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid input device");
    }
}

void MediaDb::requestDeleteItem(MediaItemPtr mediaItem)
{
    auto uri = mediaItem->uri();
    auto type = mediaItem->type();
    if (type == MediaItem::Type::EOL) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid media item type for '%s'", uri.c_str());
        return;
    }

    auto query = pbnjson::Object();
    query.put("from", kindMap_[type]);

    auto wheres = pbnjson::Array();
    prepareWhere(URI, uri, true, wheres);
    query.put("where", wheres);

    auto param = pbnjson::Object();
    param.put("query", query);

    auto device = mediaItem->device();
    auto duri = device->uri();
    if (reScanTempBuf_.find(duri) == reScanTempBuf_.end()) {
        reScanTempBuf_.emplace(duri, pbnjson::Array());
    }
    prepareOperation("del", param, reScanTempBuf_[duri]);
    device->incrementRemoveItemCount();
    if(reScanTempBuf_[duri].arraySize() >= FLUSH_COUNT) {
        flushDeleteItems(device.get());
    }
}

void MediaDb::flushDeleteItems(Device *device)
{
    std::unique_lock<std::mutex> lk(mutex_);
    if (device == nullptr) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid input device");
        return;
    }

    auto uri = device->uri();
    auto iter = reScanTempBuf_.find(uri);
    if (iter != reScanTempBuf_.end()) {
        auto array = iter->second;
        auto arraySize = iter->second.arraySize();
        if (arraySize > 0) {
            RespData *obj = new RespData {device, static_cast<size_t>(arraySize)};
            batch(array, "flushDeleteItems", static_cast<void*>(obj));

            while (arraySize > 0) {
                array.remove(ssize_t(0));
                arraySize--;
            }
        }
    }
}


bool MediaDb::resetFirstScanTempBuf(const std::string &uri)
{
    if (uri.empty()) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid uri of device");
        return false;
    }

    if (firstScanTempBuf_.find(uri) != firstScanTempBuf_.end()) {
        while (firstScanTempBuf_[uri].arraySize() > 0)
            firstScanTempBuf_[uri].remove(ssize_t(0));
    }

    return true;
}

bool MediaDb::resetReScanTempBuf(const std::string &uri)
{
    if (uri.empty()) {
        LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "Invalid uri of device");
        return false;
    }

    if (reScanTempBuf_.find(uri) != reScanTempBuf_.end()) {
        while (reScanTempBuf_[uri].arraySize() > 0)
            reScanTempBuf_[uri].remove(ssize_t(0));
    }

    return true;
}

void MediaDb::removeDirty(Device* device)
{
    std::string uri = device->uri();

    auto selectArray = pbnjson::Array();
    selectArray.append(MediaItem::metaToString(MediaItem::CommonType::KIND));
    selectArray.append(MediaItem::metaToString(MediaItem::CommonType::URI));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Thumbnail));

    auto where = pbnjson::Array();
    auto filter = pbnjson::Array();
    prepareWhere(URI, uri, false, where);
    prepareWhere(DIRTY, true, true, filter);

    auto query = pbnjson::Object();
    query.put("select", selectArray);
    query.put("where", where);
    query.put("filter", filter);

    std::string dbMethod = std::string("removeDirty");
    for (auto const &[type, kind] : kindMap_) {
        query.put("from", kind);
        bool ret = search(query, dbMethod);
        if (!ret) LOG_ERROR(MEDIA_INDEXER_MEDIADB, 0, "search fail for removeDirty. uri[%s]", uri.c_str());
    }
}

void MediaDb::grantAccess(const std::string &serviceName)
{
    LOG_INFO(MEDIA_INDEXER_MEDIADB, 0, "Add read-only access to media db for '%s'",
        serviceName.c_str());
    dbClients_.push_back(serviceName);
    roAccess(dbClients_);
}

void MediaDb::grantAccessAll(const std::string &serviceName, bool atomic,
                                pbnjson::JValue &resp,  const std::string &methodName)
{
    LOG_INFO(MEDIA_INDEXER_MEDIADB, 0, "Add read-only access to media db for '%s'",
        serviceName.c_str());
    dbClients_.push_back(serviceName);
    std::list<std::string> kindList_ = {AUDIO_KIND, VIDEO_KIND, IMAGE_KIND};
    if (atomic)
        roAccess(dbClients_, kindList_, &resp, atomic, methodName);
    else
        roAccess(dbClients_, kindList_, nullptr, atomic, methodName);
}

bool MediaDb::getAudioList(const std::string &uri, int count, LSMessage *msg, bool expand)
{
    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "%s Start for uri : %s, count : %d", __func__, uri.c_str(), count);
    auto selectArray = pbnjson::Array();
    selectArray.append(MediaItem::metaToString(MediaItem::CommonType::URI));
    selectArray.append(MediaItem::metaToString(MediaItem::CommonType::FILEPATH));
    selectArray.append(MediaItem::metaToString(MediaItem::CommonType::DIRTY));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Genre));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Album));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Artist));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::LastModifiedDate));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::FileSize));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Title));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Duration));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Thumbnail));

    auto wheres = pbnjson::Array();
    if (uri.empty()) {
        prepareWhere(DIRTY, false, true, wheres);
    } else {
        prepareWhere(URI, uri, false, wheres);
        prepareWhere(DIRTY, false, true, wheres);
    }

    auto query = pbnjson::Object();
    query.put("select", selectArray);
    query.put("from", AUDIO_KIND);
    query.put("where", wheres);

    if (count != 0)
        query.put("limit", count);

    std::string dbMethod = expand ? std::string("getAudioMetaData") : std::string("getAudioList");
    return search(query, dbMethod, msg);
}

bool MediaDb::getVideoList(const std::string &uri, int count, LSMessage *msg, bool expand)
{
    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "%s Start for uri : %s, count : %d", __func__, uri.c_str(), count);
    auto selectArray = pbnjson::Array();
    selectArray.append(MediaItem::metaToString(MediaItem::CommonType::URI));
    selectArray.append(MediaItem::metaToString(MediaItem::CommonType::FILEPATH));
    selectArray.append(MediaItem::metaToString(MediaItem::CommonType::DIRTY));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::LastModifiedDate));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::FileSize));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Width));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Height));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Title));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Duration));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Thumbnail));

    auto wheres = pbnjson::Array();
    if (uri.empty()) {
        prepareWhere(DIRTY, false, true, wheres);
    } else {
        prepareWhere(URI, uri, false, wheres);
        prepareWhere(DIRTY, false, true, wheres);
    }

    auto query = pbnjson::Object();
    query.put("select", selectArray);
    query.put("from", VIDEO_KIND);
    query.put("where", wheres);

    if (count != 0)
        query.put("limit", count);

    std::string dbMethod = expand ? std::string("getVideoMetaData") : std::string("getVideoList");
    return search(query, dbMethod, msg);
}

bool MediaDb::getImageList(const std::string &uri, int count, LSMessage *msg, bool expand)
{
    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "%s Start for uri : %s, count : %d", __func__, uri.c_str(), count);
    auto selectArray = pbnjson::Array();
    selectArray.append(URI);
    selectArray.append(TYPE);
    selectArray.append(MediaItem::metaToString(MediaItem::CommonType::DIRTY));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::LastModifiedDate));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::FileSize));
    selectArray.append(FILE_PATH);
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Title));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Width));
    selectArray.append(MediaItem::metaToString(MediaItem::Meta::Height));

    auto wheres = pbnjson::Array();
    if (uri.empty()) {
        prepareWhere(DIRTY, false, true, wheres);
    } else {
        prepareWhere(URI, uri, false, wheres);
        prepareWhere(DIRTY, false, true, wheres);
    }

    auto query = pbnjson::Object();
    query.put("select", selectArray);
    query.put("from", IMAGE_KIND);
    query.put("where", wheres);

    if (count != 0)
        query.put("limit", count);

    std::string dbMethod = expand ? std::string("getImageMetaData") : std::string("getImageList");
    return search(query, dbMethod, msg);
}

bool MediaDb::requestDelete(const std::string &uri, LSMessage *msg)
{
    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "%s Start for uri : %s", __func__, uri.c_str());
    auto where = pbnjson::Array();
    prepareWhere(URI, uri, true, where);
    MediaItem::Type type =guessType(uri);
    auto query = pbnjson::Object();
    query.put("from", kindMap_[type]);
    query.put("where", where);
    std::string dbMethod = std::string("requestDelete");
    return del(query, dbMethod, msg);
}

MediaItem::Type MediaDb::guessType(const std::string &uri)
{
    LOG_DEBUG(MEDIA_INDEXER_MEDIADB, "%s Start for uri : %s", __FUNCTION__, uri.c_str());
    gchar *contentType = NULL;
    gboolean uncertain;
    bool mimeTypeSupported = false;

    contentType = g_content_type_guess(uri.c_str(), NULL, 0, &uncertain);
    if (!contentType) {
        LOG_INFO(MEDIA_INDEXER_MEDIADB, 0, "MIME type detection is failed for '%s'", uri.c_str());
        return MediaItem::Type::EOL;
    }

    std::string mimeType = contentType;
    g_free(contentType);

    mimeTypeSupported = MediaItem::mimeTypeSupported(mimeType);
    if (!mimeTypeSupported) {
        // get the file extension for the ts or ps or asf.
        std::string ext = uri.substr(uri.find_last_of('.') + 1);

        //TODO: switch
        if (!ext.compare("ts"))
            mimeType = std::string("video/MP2T");
        else if (!ext.compare("ps"))
            mimeType = std::string("video/MP2P");
        else if (!ext.compare("asf"))
            mimeType = std::string("video/x-asf");
        else {
            LOG_INFO(MEDIA_INDEXER_MEDIADB, 0, "it's NOT ts/ps/asf. need to check for '%s'", uri.c_str());
            return MediaItem::Type::EOL;
        }
    }

    MediaItem::Type type = MediaItem::typeFromMime(mimeType);
    return type;
}

bool MediaDb::prepareWhere(const std::string &key,
                           const std::string &value,
                           bool precise,
                           pbnjson::JValue &whereClause) const
{
    auto cond = pbnjson::Object();
    cond.put("prop", key);
    cond.put("op", precise ? "=" : "%");
    cond.put("val", value);
    whereClause << cond;
    return true;
}

bool MediaDb::prepareWhere(const std::string &key,
                           bool value,
                           bool precise,
                           pbnjson::JValue &whereClause) const
{
    auto cond = pbnjson::Object();
    cond.put("prop", key);
    cond.put("op", precise ? "=" : "%");
    cond.put("val", value);
    whereClause << cond;
    return true;
}

bool MediaDb::prepareOperation(const std::string &method,
                               pbnjson::JValue &param,
                               pbnjson::JValue &operationClause) const
{
    auto operation = pbnjson::Object();
    operation.put("method", method);
    operation.put("params", param);
    operationClause << operation;
    return true;
}

MediaDb::MediaDb() :
    DbConnector("com.webos.service.mediaindexer.media", true)
{
    std::list<std::list<std::string>> indexList = {
        {URI}, {DIRTY}, {DIRTY, URI}
    };

    int i = 1;
    for (std::list<std::string> list : indexList) {
        auto index = pbnjson::Object();
        index.put("name", "index"+ std::to_string(i));
        i++;

        auto props = pbnjson::Array();
        for (std::string idx : list) {
            auto prop = pbnjson::Object();
            prop.put("name", idx);
            props << prop;
        }
        index.put("props", props);
        kindIndexes_ << index;
    }
}
