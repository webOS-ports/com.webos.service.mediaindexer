// Copyright (c) 2018-2020 LG Electronics, Inc.
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

#include "mediaindexerclient.h"
#include <iostream>
#include <list>

MediaIndexerClient::MediaIndexerClient(MediaIndexerCallback cb, void* userData)
    : callback_(cb),
      userData_(userData)
{
    std::cout << std::string("MediaIndexerClient ctor!") << std::endl;

    dbConnector_ = std::unique_ptr<LunaConnector>(new LunaConnector(std::string(dbClientService), true));

    if (!dbConnector_)
        std::cout << "Failed to create dbConnector object" << std::endl;

    indexerConnector_ = std::unique_ptr<LunaConnector>(new LunaConnector(std::string(indexerClientService), true));

    if (!indexerConnector_)
        std::cout << "Failed to create indexerConnector object" << std::endl;
}

MediaIndexerClient::~MediaIndexerClient()
{
    dbConnector_.reset();
    indexerConnector_.reset();
}

std::string MediaIndexerClient::getAudioList(const std::string& uri)
{
    if (!dbConnector_) {
        std::cout << "LunaConnector is NULL!" << std::endl;
        return std::string();
    }

    std::cout << "I'm getAudioList" << std::endl;
    std::cout << "thread id[" << std::this_thread::get_id() << "]" << std::endl;

    pbnjson::JValue request = generateLunaPayload(MediaIndexerClientAPI::GetAudioListAPI, uri);
    std::string url = dbUrl_ + std::string("search");
    bool async = false;
    LSMessageToken sessionToken;

    if (!dbConnector_->sendMessage(url.c_str(), request.stringify().c_str(), MediaIndexerClient::onLunaResponse, this, async, &sessionToken)) {
        std::cout << "sendMessage ERROR!" << std::endl;
        return std::string();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "Return value_ in getAudioList : " << returnValue_ << std::endl;
    std::cout << "I'm getAudioList END!!!!!!!!!!!!" << std::endl;
    return returnValue_;
}

std::string MediaIndexerClient::getVideoList(const std::string& uri)
{
    if (!dbConnector_) {
        std::cout << "LunaConnector is NULL!" << std::endl;
        return std::string();
    }

    std::cout << "I'm getVideoList" << std::endl;
    std::cout << "thread id[" << std::this_thread::get_id() << "]" << std::endl;

    pbnjson::JValue request = generateLunaPayload(MediaIndexerClientAPI::GetVideoListAPI, uri);
    std::string url = dbUrl_ + std::string("search");
    bool async = false;
    LSMessageToken sessionToken;

    if (!dbConnector_->sendMessage(url.c_str(), request.stringify().c_str(), MediaIndexerClient::onLunaResponse, this, async, &sessionToken)) {
        std::cout << "sendMessage ERROR!" << std::endl;
        return std::string();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "Return value_ in getVideoList : " << returnValue_ << std::endl;
    std::cout << "I'm getVideoList END!!!!!!!!!!!!" << std::endl;
    return returnValue_;
}

std::string MediaIndexerClient::getImageList(const std::string& uri)
{
    if (!dbConnector_) {
        std::cout << "LunaConnector is NULL!" << std::endl;
        return std::string();
    }

    pbnjson::JValue request = generateLunaPayload(MediaIndexerClientAPI::GetImageListAPI, uri);
    std::string url = dbUrl_ + std::string("search");
    bool async = false;
    LSMessageToken sessionToken;

    if (!dbConnector_->sendMessage(url.c_str(), request.stringify().c_str(), MediaIndexerClient::onLunaResponse, this, async, &sessionToken)) {
        std::cout << "sendMessage ERROR!" << std::endl;
        return std::string();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "Return value_ in getImageList : " << returnValue_ << std::endl;
    std::cout << "I'm getImageList END!!!!!!!!!!!!" << std::endl;
    return returnValue_;

}

std::string MediaIndexerClient::getAudioMetaData(const std::string& uri)
{
    if (!dbConnector_) {
        std::cout << "LunaConnector is NULL!" << std::endl;
        return std::string();
    }

    pbnjson::JValue request = generateLunaPayload(MediaIndexerClientAPI::GetAudioMetaDataAPI, uri);
    std::string url = dbUrl_ + std::string("search");
    bool async = false;
    LSMessageToken sessionToken;

    if (!dbConnector_->sendMessage(url.c_str(), request.stringify().c_str(), MediaIndexerClient::onLunaResponse, this, async, &sessionToken)) {
        std::cout << "sendMessage ERROR!" << std::endl;
        return std::string();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "Return value_ in getAudioMetaData : " << returnValue_ << std::endl;
    std::cout << "I'm getAudioMetaData END!!!!!!!!!!!!" << std::endl;
    return returnValue_;
}

std::string MediaIndexerClient::getVideoMetaData(const std::string& uri)
{
    if (!dbConnector_) {
        std::cout << "LunaConnector is NULL!" << std::endl;
        return std::string();
    }

    pbnjson::JValue request = generateLunaPayload(MediaIndexerClientAPI::GetVideoMetaDataAPI, uri);
    std::string url = dbUrl_ + std::string("search");
    bool async = false;
    LSMessageToken sessionToken;

    if (!dbConnector_->sendMessage(url.c_str(), request.stringify().c_str(), MediaIndexerClient::onLunaResponse, this, async, &sessionToken)) {
        std::cout << "sendMessage ERROR!" << std::endl;
        return std::string();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "Return value_ in getVideoMetaData : " << returnValue_ << std::endl;
    std::cout << "I'm getVideoMetaData END!!!!!!!!!!!!" << std::endl;
    return returnValue_;
}

std::string MediaIndexerClient::getImageMetaData(const std::string& uri)
{
    if (!dbConnector_) {
        std::cout << "LunaConnector is NULL!" << std::endl;
        return std::string();
    }

    pbnjson::JValue request = generateLunaPayload(MediaIndexerClientAPI::GetImageMetaDataAPI, uri);
    std::string url = dbUrl_ + std::string("search");
    bool async = false;
    LSMessageToken sessionToken;

    if (!dbConnector_->sendMessage(url.c_str(), request.stringify().c_str(), MediaIndexerClient::onLunaResponse, this, async, &sessionToken)) {
        std::cout << "sendMessage ERROR!" << std::endl;
        return std::string();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "Return value_ in getImageMetaData : " << returnValue_ << std::endl;
    std::cout << "I'm getImageMetaData END!!!!!!!!!!!!" << std::endl;
    return returnValue_;    
}

bool MediaIndexerClient::onLunaResponse(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    MediaIndexerClient *client = static_cast<MediaIndexerClient*>(ctx);
    std::cout << "I'm onLunaResponse" << std::endl;
    return client->handleLunaResponse(msg);
}

bool MediaIndexerClient::handleLunaResponse(LSMessage *msg)
{
    std::cout << "I'm handleLunaResponse" << std::endl;
    std::cout << "thread id[" << std::this_thread::get_id() << "]" << std::endl;

    pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());
    const char *payload = LSMessageGetPayload(msg);

    std::cout << "payload : " << payload << std::endl;

    if (!parser.parse(payload)) {
        std::cout << "Invalid JSON message: " << payload << std::endl;
        return false;
    }

    pbnjson::JValue domTree(parser.getDom());
    std::cout << "Message : " << domTree.stringify() << std::endl;

    std::lock_guard<std::mutex> lock(mutex_);
    returnValue_ = domTree.stringify();
    return true;
}

// TODO: remove duplicated append for each requested function.
pbnjson::JValue MediaIndexerClient::generateLunaPayload(MediaIndexerClientAPI api,
                                                        const std::string& uri)
{
    auto request = pbnjson::Object();
    switch(api) {
        case MediaIndexerClientAPI::GetAudioListAPI: {
            auto selectArray = pbnjson::Array();
            selectArray.append(std::string("uri"));
            selectArray.append(std::string("type"));
            selectArray.append(std::string("last_modified_date"));
            selectArray.append(std::string("file_size"));
            selectArray.append(std::string("file_path"));
            selectArray.append(std::string("title"));
            selectArray.append(std::string("duration"));
            selectArray.append(std::string("thumbnail"));
            std::string val = "storage"; // uri;

            auto query = pbnjson::Object();
            query.put("select", selectArray);
            query.put("from", audioKind_);
            auto where = pbnjson::Array();
            auto cond = pbnjson::Object();
            if (uri.empty()) {
                cond.put("prop", "dirty");
                cond.put("op", "=");
                cond.put("val", false);
            } else {
                cond.put("prop", "uri");
                cond.put("op", "%");
                cond.put("val", uri);
            }
            where << cond;
            query.put("where", where);

            request.put("query", query);
            break;
        }
        case MediaIndexerClientAPI::GetVideoListAPI: {
            auto selectArray = pbnjson::Array();
            selectArray.append(std::string("uri"));
            selectArray.append(std::string("type"));
            selectArray.append(std::string("last_modified_date"));
            selectArray.append(std::string("file_size"));
            selectArray.append(std::string("file_path"));
            selectArray.append(std::string("duration"));
            selectArray.append(std::string("title"));
            selectArray.append(std::string("thumbnail"));
            std::string val = "storage"; // uri;

            auto query = pbnjson::Object();
            query.put("select", selectArray);
            query.put("from", videoKind_);
            auto where = pbnjson::Array();
            auto cond = pbnjson::Object();
            if (val.empty()) {
                cond.put("prop", "dirty");
                cond.put("op", "=");
                cond.put("val", false);
            } else {
                cond.put("prop", "uri");
                cond.put("op", "%");
                cond.put("val", val);
            }
            where << cond;
            query.put("where", where);

            request.put("query", query);
            break;
        }
        case MediaIndexerClientAPI::GetImageListAPI: {
            auto selectArray = pbnjson::Array();
            selectArray.append(std::string("uri"));
            selectArray.append(std::string("type"));
            selectArray.append(std::string("last_modified_date"));
            selectArray.append(std::string("file_size"));
            selectArray.append(std::string("file_path"));
            selectArray.append(std::string("title"));
            selectArray.append(std::string("width"));
            selectArray.append(std::string("height"));
            std::string val = "storage"; // uri;

            auto query = pbnjson::Object();
            query.put("select", selectArray);
            query.put("from", imageKind_);
            auto where = pbnjson::Array();
            auto cond = pbnjson::Object();
            if (val.empty()) {
                cond.put("prop", "dirty");
                cond.put("op", "=");
                cond.put("val", false);
            } else {
                cond.put("prop", "uri");
                cond.put("op", "%");
                cond.put("val", val);
            }
            where << cond;
            query.put("where", where);

            request.put("query", query);
            break;
        }
        case MediaIndexerClientAPI::GetAudioMetaDataAPI: {
            auto selectArray = pbnjson::Array();
            selectArray.append(std::string("uri"));
            selectArray.append(std::string("mime"));
            selectArray.append(std::string("type"));
            selectArray.append(std::string("date_of_creation"));
            selectArray.append(std::string("last_modified_date"));
            selectArray.append(std::string("file_size"));
            selectArray.append(std::string("file_path"));
            selectArray.append(std::string("title"));
            selectArray.append(std::string("genre"));
            selectArray.append(std::string("album"));
            selectArray.append(std::string("artist"));
            selectArray.append(std::string("album_artist"));
            selectArray.append(std::string("track"));
            selectArray.append(std::string("total_tracks"));
            selectArray.append(std::string("duration"));
            selectArray.append(std::string("thumbnail"));
            selectArray.append(std::string("sample_rate"));
            selectArray.append(std::string("bit_per_sample"));
            selectArray.append(std::string("bit_rate"));
            selectArray.append(std::string("channels"));
            selectArray.append(std::string("lyric"));
            std::string val = "storage"; // uri;

            auto query = pbnjson::Object();
            query.put("select", selectArray);
            query.put("from", audioKind_);
            auto where = pbnjson::Array();
            auto cond = pbnjson::Object();
            cond.put("prop", "uri");
            cond.put("op", "=");
            cond.put("val", val);
            where << cond;
            query.put("where", where);

            request.put("query", query);
            break;
        }
        case MediaIndexerClientAPI::GetVideoMetaDataAPI: {
            auto selectArray = pbnjson::Array();
            selectArray.append(std::string("uri"));
            selectArray.append(std::string("title"));
            selectArray.append(std::string("mime"));
            selectArray.append(std::string("type"));
            selectArray.append(std::string("date_of_creation"));
            selectArray.append(std::string("last_modified_date"));
            selectArray.append(std::string("file_size"));
            selectArray.append(std::string("file_path"));
            selectArray.append(std::string("duration"));
            selectArray.append(std::string("width"));
            selectArray.append(std::string("height"));
            selectArray.append(std::string("thumbnail"));
            selectArray.append(std::string("frame_rate"));
            std::string val = "storage"; // uri;

            auto query = pbnjson::Object();
            query.put("select", selectArray);
            query.put("from", videoKind_);
            auto where = pbnjson::Array();
            auto cond = pbnjson::Object();
            cond.put("prop", "uri");
            cond.put("op", "=");
            cond.put("val", val);
            where << cond;
            query.put("where", where);

            request.put("query", query);
            break;
        }
        case MediaIndexerClientAPI::GetImageMetaDataAPI: {
            auto selectArray = pbnjson::Array();
            selectArray.append(std::string("uri"));
            selectArray.append(std::string("mime"));
            selectArray.append(std::string("title"));
            selectArray.append(std::string("type"));
            selectArray.append(std::string("date_of_creation"));
            selectArray.append(std::string("last_modified_date"));
            selectArray.append(std::string("file_size"));
            selectArray.append(std::string("file_path"));
            selectArray.append(std::string("width"));
            selectArray.append(std::string("height"));
            selectArray.append(std::string("geo_location_city"));
            selectArray.append(std::string("geo_location_country"));
            selectArray.append(std::string("geo_location_latitude"));
            selectArray.append(std::string("geo_location_longitude"));
            std::string val = "storage"; // uri;

            auto query = pbnjson::Object();
            query.put("select", selectArray);
            query.put("from", imageKind_);
            auto where = pbnjson::Array();
            auto cond = pbnjson::Object();
            cond.put("prop", "uri");
            cond.put("op", "=");
            cond.put("val", val);
            where << cond;
            query.put("where", where);

            request.put("query", query);
            break;
        }
        default:
            break;
    }

    return request;
}
bool MediaIndexerClient::onGetDeviceList(LSHandle* lsHandle, LSMessage* msg, void* ctx)
{
    MediaIndexerClient *client = static_cast<MediaIndexerClient*>(ctx);
    std::cout << "I'm onGetDeviceList" << std::endl;
    return client->handleResponseFromIndexer(msg);
}

bool MediaIndexerClient::handleResponseFromIndexer(LSMessage* msg)
{
    std::cout << "I'm handleResponseFromIndexer" << std::endl;
    std::cout << "thread id[" << std::this_thread::get_id() << "]" << std::endl;

    pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());
    const char *payload = LSMessageGetPayload(msg);

    std::cout << "payload : " << payload << std::endl;

    if (!parser.parse(payload)) {
        std::cout << "Invalid JSON message: " << payload << std::endl;
        return false;
    }

    pbnjson::JValue domTree(parser.getDom());
    std::cout << "Message : " << domTree.stringify() << std::endl;
/*
    if (callback_) {
        callback_(MediaIndexerClientEvent::NotifyGetDeviceList, domTree.stringify(), userData_); 
    }
*/
    std::lock_guard<std::mutex> lock(mutex_);
    returnValue_ = domTree.stringify();
    return true;

}

std::string MediaIndexerClient::getDeviceList()
{
    std::cout << "I'm getDeviceList" << std::endl;
    std::cout << "thread id[" << std::this_thread::get_id() << "]" << std::endl;
    if (!indexerConnector_) {
        std::cout << "indexerConnector is NULL!" << std::endl;
        return std::string();
    }

    LSMessageToken sessionToken;
    auto subscription = pbnjson::Object();
    subscription.put("subscribe", true);

    if (!indexerConnector_->sendMessage("luna://com.webos.service.mediaindexer/getDeviceList", subscription.stringify().c_str(), MediaIndexerClient::onGetDeviceList, this, false, &sessionToken)) {
        std::cout << "sendMessage ERROR!" << std::endl;
        return std::string();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "Return value_ in getDeviceList : " << returnValue_ << std::endl;
    std::cout << "I'm getDeviceList END!!!!!!!!!!!!" << std::endl;
    return returnValue_;
}
