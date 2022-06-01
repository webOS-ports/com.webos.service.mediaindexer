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

#include "task.h"
#include "mediaitem.h"
#include "metadataextractors/imetadataextractor.h"
#include <pbnjson.hpp>

#include <thread>
#include <memory>
#include <mutex>
#include <queue>
#include <list>
#include <atomic>
#include <glib.h>

/// Media parser class for meta data extraction.
class MediaParser
{
 public:
    /// Enqueue meta data extraction task.
    static void enqueueTask(MediaItemPtr mediaItem);

    static void extractMeta(void *data, void *user_data);

    /**
     * \brief Get media parser object.
     *
     * \return Singleton object.
     */
    static MediaParser *instance();

    static MediaItem::ExtractorType getType(MediaItem::Type type, const std::string &ext);

    bool setMediaItem(std::string & uri);

    /// Construction is only allowed with media item.
    MediaParser();

    /// For the purpose of direct meta extraction from indexer service api
    bool extractExtraMeta(pbnjson::JValue &meta);
    virtual ~MediaParser();

 private:
    /// Start new task, must be called with lock locked.
    static void runTask();

    static std::unique_ptr<MediaParser> instance_;
    /// Queue of meta data extraction tasks.
    static std::queue<std::unique_ptr<MediaParser>> tasks_;
    /// Number of currently running threads.
    static int runningThreads_;
    /// Make class static data thread safe.
    static std::mutex lock_;
    static std::mutex ctorLock_;
    /// Meta data extrator.
    static std::map<MediaItem::ExtractorType,
           std::shared_ptr<IMetaDataExtractor>> extractor_;
    GThreadPool *pool = nullptr;
    std::mutex mediaItemLock_;
    /// The media item this media parser works on - extractMeta will
    /// modify it internally though it is otherwise consideren to be
    /// const so better make this mutable
    mutable MediaItemPtr mediaItem_;
    /// Set if the default extractor shall be used.
    bool useDefaultExtractor_;

    /// The media item queue
    /// This media item queue will use task thread
    std::queue<MediaItemPtr> mediaItemQueue_;
};
