// Copyright (c) 2018-2021 LG Electronics, Inc.
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

#include <string>
#include <memory>
#include <mutex>
#include "mediadbconnector.h"
#include "indexerconnector.h"
#include "mediaindexer-common.h"
#include "logging.h"

class MediaIndexerClient {
public:
    using MediaIndexerCallback = std::function<void(MediaIndexerClientEvent event, void* clientData, void* userData)>;
    MediaIndexerClient(MediaIndexerCallback cb = nullptr, void* userData = nullptr);
    ~MediaIndexerClient();

    void initialize() const;

    // Media Indexer Client API
    std::string getDeviceList() const;
    void getMediaDBPermission() const;

    // Media Indexer Client Database API
    std::string getAudioList(const std::string& uri) const;
    std::string getVideoList(const std::string& uri) const;
    std::string getImageList(const std::string& uri) const;
    std::string getAudioMetaData(const std::string& uri) const;
    std::string getVideoMetaData(const std::string& uri) const;
    std::string getImageMetaData(const std::string& uri) const;
    std::string requestDelete(const std::string& uri) const;
    std::string requestMediaScan(const std::string& path) const;

private:
    std::string getKindID(const std::string &uri) const;
    std::string typeFromMime(const std::string &mime) const;

    pbnjson::JValue generateLunaPayload(MediaIndexerClientAPI api,
                                        const std::string& uri) const;

    bool prepareWhere(const std::string &key,
                                 const std::string &value,
                                 bool precise,
                                 pbnjson::JValue &whereClause) const;

    bool prepareWhere(const std::string &key,
                                 bool value,
                                 bool precise,
                                 pbnjson::JValue &whereClause) const;

    pbnjson::JValue prepareQuery(const std::string& kindId,
                                 pbnjson::JValue where) const;

    pbnjson::JValue prepareQuery(pbnjson::JValue selectArray,
                                 const std::string& kindId,
                                 pbnjson::JValue where) const;

    static constexpr const char *mediaKind_ = "com.webos.service.mediaindexer.media:1";
    static constexpr const char *audioKind_ = "com.webos.service.mediaindexer.audio:1";
    static constexpr const char *videoKind_ = "com.webos.service.mediaindexer.video:1";
    static constexpr const char *imageKind_ = "com.webos.service.mediaindexer.image:1";

    MediaIndexerCallback callback_;
    void* userData_;

    std::thread task_;

    // indexer service connector.
    std::unique_ptr<IndexerConnector> indexerConnector_;
    // mediadb connector.
    std::unique_ptr<MediaDBConnector> mediaDBConnector_;

    // prevent copy & assign operation
    MediaIndexerClient(MediaIndexerClient const&) = delete;
    MediaIndexerClient(MediaIndexerClient &&) = delete;
    MediaIndexerClient& operator =(MediaIndexerClient const&) = delete;
    MediaIndexerClient& operator =(MediaIndexerClient &&) = delete;
};
