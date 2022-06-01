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

#include "mediaparser.h"
#include "plugins/pluginfactory.h"
#include "plugins/plugin.h"
#include "metadataextractors/imetadataextractor.h"
#include "dbconnector/mediadb.h"
#include <thread>
#include <chrono>
#include <condition_variable>


std::queue<std::unique_ptr<MediaParser>> MediaParser::tasks_;
std::map<MediaItem::ExtractorType, std::shared_ptr<IMetaDataExtractor>> MediaParser::extractor_;
int MediaParser::runningThreads_ = 0;
std::mutex MediaParser::lock_;
std::unique_ptr<MediaParser> MediaParser::instance_;
std::mutex MediaParser::ctorLock_;


void MediaParser::enqueueTask(MediaItemPtr mediaItem)
{
    std::lock_guard<std::mutex> lock(lock_);
    auto type = mediaItem->extractorType();
    MediaParser* mParser = MediaParser::instance();
    mParser->mediaItemQueue_.push(std::move(mediaItem));
    GError *error = nullptr;
    if (!g_thread_pool_push(mParser->pool, static_cast<void*>(&type), &error)) {
        LOG_ERROR(MEDIA_INDEXER_MEDIAPARSER, 0, "Fail occurred in g_thread_pool_push");
        if (error) {
            LOG_ERROR(MEDIA_INDEXER_MEDIAPARSER, 0, "Error Message : %s", error->message);
            g_error_free(error);
        }
    }
}

MediaParser *MediaParser::instance()
{
    std::lock_guard<std::mutex> lk(ctorLock_);
    if (!instance_.get()) {
        instance_.reset(new MediaParser());
    }
    return instance_.get();
}

MediaParser::~MediaParser()
{
    // all threads are running in detached mode so no need to cleanup
    // anything here
    LOG_INFO(MEDIA_INDEXER_MEDIAPARSER, 0, "MediaParser Dtor!!!");
    g_thread_pool_free(pool, TRUE, TRUE);
    //mediaItem_.reset();
}

MediaParser::MediaParser()
{
    pool = g_thread_pool_new((GFunc) &MediaParser::extractMeta, this, PARALLEL_META_EXTRACTION, TRUE, NULL);
    g_thread_pool_set_max_unused_threads(PARALLEL_META_EXTRACTION);

    // create each extractors
    for (auto type = MediaItem::ExtractorType::TagLibExtractor;
            type < MediaItem::ExtractorType::EOL; ++type)
        extractor_[type] = IMetaDataExtractor::extractor(type);
}

MediaItem::ExtractorType MediaParser::getType(MediaItem::Type type, const std::string &ext)
{
    MediaItem::ExtractorType ret = MediaItem::ExtractorType::EOL;
    switch(type) {
        case MediaItem::Type::Audio:
            if (ext.compare(EXT_MP3) == 0 || ext.compare(EXT_OGG) == 0)
                ret = MediaItem::ExtractorType::TagLibExtractor;
            else
                ret = MediaItem::ExtractorType::GStreamerExtractor;
            break;
        case MediaItem::Type::Video:
            ret = MediaItem::ExtractorType::GStreamerExtractor;
            break;
        case MediaItem::Type::Image:
            ret = MediaItem::ExtractorType::ImageExtractor;
            break;
        default:
            break;
    }
    return ret;
}


bool MediaParser::setMediaItem(std::string & uri)
{
    std::lock_guard<std::mutex> lock(mediaItemLock_);
    if (mediaItem_.get() != nullptr)
        mediaItem_.reset();
    mediaItem_ = std::make_unique<MediaItem>(uri);
    if (mediaItem_.get() == nullptr) {
        LOG_ERROR(MEDIA_INDEXER_MEDIAPARSER, 0, "Failed to get mediaitem!");
        return false;
    }
    return true;
}

bool MediaParser::extractExtraMeta(pbnjson::JValue &meta)
{
    try {
        std::lock_guard<std::mutex> lock(mediaItemLock_);
        auto mi = mediaItem_.get();
        if (mi == nullptr) {
            LOG_ERROR(MEDIA_INDEXER_MEDIAPARSER, 0, "Media Item is invalid");
            return false;
        }
        LOG_DEBUG(MEDIA_INDEXER_MEDIAPARSER, "Media item to extract %p with parser %p", mi, this);
        auto path = mediaItem_->path();

        if (*path.begin() == '/') {
            MediaItem::ExtractorType p = getType(mediaItem_->type(), mi->ext());

            if (extractor_.find(p) != extractor_.end()) {
                extractor_[p]->extractMeta(*mi, true);
                mi->setParsed(true);
            } else {
                LOG_WARNING(MEDIA_INDEXER_MEDIAPARSER, 0, "Could not found valid extractor, type : %s, ext : %s", MediaItem::mediaTypeToString(mediaItem_->type()).c_str(), mi->ext().c_str());
                LOG_DEBUG(MEDIA_INDEXER_MEDIAPARSER, "Create new extractor");
                extractor_[p] = IMetaDataExtractor::extractor(p);
                extractor_[p]->extractMeta(*mi, true);
                mi->setParsed(true);
            }
        } else {
            auto plg = PluginFactory().plugin(mediaItem_->uri());
            plg->extractMeta(*mi, true);
            mi->setParsed(true);
        }
        if (!mediaItem_->putExtraMetaToJson(meta)) {
            LOG_ERROR(MEDIA_INDEXER_MEDIAPARSER, 0, "Failed to put meta to json");
            return false;
        }
        mediaItem_.reset();
    } catch (const std::exception & e) {
        LOG_ERROR(MEDIA_INDEXER_MEDIAPARSER, 0, "MediaParser::extractMeta failure: %s", e.what());
        return false;
    } catch (...) {
        LOG_ERROR(MEDIA_INDEXER_MEDIAPARSER, 0, "MediaParser::extractMeta failure by unexpected failure");
        return false;
    }
    return true;
}

void MediaParser::extractMeta(void *data, void *user_data)
{
    // make sure we are deleted when this method terminates
    try {
        if (!data || !user_data) {
            LOG_ERROR(MEDIA_INDEXER_MEDIAPARSER, 0, "Invalid Input parameters");
            return;
        }
        MediaParser *mp = static_cast<MediaParser *>(user_data);

        MediaItemPtr mip;
        {
            // mediaItemQueue is resource that task threads use it.
            std::lock_guard<std::mutex> lock(mp->mediaItemLock_);
            mip = std::move(mp->mediaItemQueue_.front());
            mp->mediaItemQueue_.pop();
        }

        LOG_DEBUG(MEDIA_INDEXER_MEDIAPARSER, "Media item to extract %p with parser %p", mip.get(), mp);

        auto path = mip->path();
        if (*path.begin() == '/') {
            MediaItem::ExtractorType p = mip->extractorType();
            if (!extractor_[p]->extractMeta(*mip)) {
                LOG_WARNING(MEDIA_INDEXER_MEDIAPARSER, 0, "%s meta data extraction failed!", mip->uri().c_str());
            }
        } else {
            auto plg = PluginFactory().plugin(mip->uri());
            plg->extractMeta(*mip);
        }

        mip->setParsed(true);
        LOG_DEBUG(MEDIA_INDEXER_MEDIAPARSER, "Pushing parsed mediaitem %p to mdb, updateMediaItem start", mip.get());
        auto mdb = MediaDb::instance();
        mdb->updateMediaItem(std::move(mip));
        LOG_DEBUG(MEDIA_INDEXER_MEDIAPARSER, "mdb->updateMediaItem Done");

    } catch (const std::exception & e) {
        LOG_ERROR(MEDIA_INDEXER_MEDIAPARSER, 0, "MediaParser::extractMeta failure: %s", e.what());
    } catch (...) {
        LOG_ERROR(MEDIA_INDEXER_MEDIAPARSER, 0, "MediaParser::extractMeta failure by unexpected failure");
    }
}
