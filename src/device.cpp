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

#include "device.h"
#include "plugins/pluginfactory.h"
#include "plugins/plugin.h"
#include "dbconnector/mediadb.h"
#include <filesystem>

// Not part of Device class, this is defined at the bottom of device.h
Device::Meta &operator++(Device::Meta &meta)
{
    if (meta == Device::Meta::EOL)
        return meta;
    meta = static_cast<Device::Meta>(static_cast<int>(meta) + 1);
    return meta;
}

std::string Device::metaTypeToString(Device::Meta meta)
{
    switch (meta) {
    case Device::Meta::Name:
        return std::string("name");
    case Device::Meta::Description:
        return std::string("description");
    case Device::Meta::Icon:
        return std::string("icon");
    case Device::Meta::EOL:
        return std::string("eol");
    }

    return "";
}

std::string Device::stateToString(Device::State state)
{
    switch (state) {
    case Device::State::Idle:
        return std::string("idle");
    case Device::State::Scanning:
        return std::string("scanning");
    case Device::State::Parsing:
        return std::string("parsing");
    case Device::State::Inactive:
        return std::string("inactive");
    }

    return "";
}

std::shared_ptr<Device> Device::device(const std::string &uri)
{
    PluginFactory factory;
    auto plg = factory.plugin(uri);
    if (!plg)
        return nullptr;

    if (!plg->hasDevice(uri))
        return nullptr;

    return plg->device(uri);
}

Device::Device(const std::string &uri, int alive, bool avail, std::string uuid) :
    uri_(uri),
    mountpoint_(""),
    uuid_(uuid),
    lastSeen_(),
    state_(Device::State::Inactive),
    available_(avail),
    alive_(alive),
    maxAlive_(alive),
    observer_(nullptr)
{
    lastSeen_ = std::chrono::system_clock::now();
    LOG_DEBUG(MEDIA_INDEXER_DEVICE, "Device Ctor, URI : %s UUID : %s, object : %p", uri_.c_str(), uuid_.c_str(), this);
}

Device::~Device()
{
    // nothing to be done here, the only allocated object is the
    // scanner thread which runs in detached mode and thus does not
    // need any cleanup
    LOG_DEBUG(MEDIA_INDEXER_DEVICE, "Device Dtor, URI : %s UUID : %s OBJECT : %p", uri_.c_str(), uuid_.c_str(), this);
    exit_ = true;
    queue_.push_back("");
    cv_.notify_one();
    if (task_.joinable())
        task_.join();
    cleanUpTask_.destroy();
}

void Device::init()
{
    if (!createThumbnailDirectory()) {
        LOG_ERROR(MEDIA_INDEXER_DEVICE, 0, "Failed to create corresponding thumbnail directory for device UUID %s", uuid_.c_str());
    }
    task_ = std::thread(&Device::scanLoop, this);
    task_.detach();

    auto cleanTaskFunc = [] (void *ctx, void *data) -> void {
        Device* dev = static_cast<Device *>(ctx);
        if (dev) {
            LOG_DEBUG(MEDIA_INDEXER_DEVICE, "Clean Up Task start for device '%s'", dev->uri().c_str());
            auto obs = dev->observer();
            if (obs)
                obs->cleanupDevice(dev);
        }
    };

    cleanUpTask_.create(cleanTaskFunc);
}

bool Device::available(bool check)
{
    if (check) {
        std::unique_lock lock(lock_);
        available_ = checkAlive();
    }

    // if the device becomes unavailable it is likely that the icon
    // uri becomes unavailable as well so better delete the uri and we
    // also need to reset the media item count
    if (!available_) {
        meta_[Device::Meta::Icon] = "";
        resetMediaItemCount();
        setState(State::Inactive, true);
    }

    return available_;
}

bool Device::setAvailable(bool avail)
{
    std::unique_lock lock(lock_);

    auto before = available_;

    available_ = avail;
    if (!available_)
        alive_ = 0;
    else
        resetAlive();

    // if the device becomes unavailable it is likely that the icon
    // uri becomes unavailable as well so better delete the uri and we
    // also need to reset the media item count
    if (!available_) {
        meta_[Device::Meta::Icon] = "";
        resetMediaItemCount();
        setState(State::Inactive, true);
    }

    auto changed = (before != avail);
    if (avail && changed) {
        lastSeen_ = std::chrono::system_clock::now();
        setState(State::Idle, true);
    }

    return changed;
}

const std::string &Device::uri() const
{
    return uri_;
}

const std::string &Device::uuid() const
{
    return uuid_;
}

void Device::setUuid(const std::string &uuid)
{
    std::unique_lock lock(lock_);
    uuid_ = uuid;
}


int Device::alive() const
{
    std::shared_lock lock(lock_);
    return maxAlive_;
}

const std::string &Device::meta(Device::Meta type) const
{
    std::shared_lock lock(lock_);
    return meta_[type];
}

bool Device::setMeta(Device::Meta type, const std::string value)
{
    std::unique_lock lock(lock_);

    // check if something needs to be done
    if (meta_[type] == value)
        return false;

    LOG_DEBUG(MEDIA_INDEXER_DEVICE, "Updating meta %i to '%s' for '%s'", (int) type, value.c_str(),
        uri().c_str());
    meta_[type] = value;

    return true;
}

Device::State Device::state() const
{
    std::shared_lock lock(lock_);
    return state_;
}

void Device::setState(Device::State state)
{
    std::unique_lock lock(lock_);
    setState(state, false);
}

const std::chrono::system_clock::time_point &Device::lastSeen() const
{
    std::shared_lock lock(lock_);
    return lastSeen_;
}

void Device::scanLoop()
{
    while (!exit_)
    {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return !queue_.empty(); });
        std::string uri = static_cast<std::string>(queue_.front());
        if (uri.empty())
        {
            LOG_ERROR(MEDIA_INDEXER_DEVICE, 0, "Deque data is invalid!");
            continue;
        }
        LOG_DEBUG(MEDIA_INDEXER_DEVICE, "scanLoop start for uri : %s",uri_.c_str());
        // let the plugin scan the device for media items
        auto plg = plugin();
        if (plg == nullptr)
        {
            LOG_ERROR(MEDIA_INDEXER_DEVICE, 0, "plugin for %s is not invalid",uri.c_str());
            break;
        }
#if PERFCHECK_ENABLE
        std::string perfuri = "SCAN-" + uuid();
        PERF_START(perfuri.c_str());
#endif
        setState(Device::State::Scanning);
        plg->scan(uri);
        setState(Device::State::Parsing);
        auto obs = observer();
        if (obs) {
            obs->notifyDeviceList();
        }
#if PERFCHECK_ENABLE
        PERF_END(perfuri.c_str());
#endif
        if (processingDone())
            activateCleanUpTask();
        queue_.pop_front();
    }
}

bool Device::scan(IMediaItemObserver *observer)
{
    {
        std::shared_lock lock(lock_);
        if (!available_) {
            LOG_ERROR(MEDIA_INDEXER_DEVICE, 0, "Device '%s' is not available", uri().c_str());
            return false;
        }
    }

    if (observer)
    {
        std::unique_lock lock(lock_);
        observer_ = observer;
    }

    LOG_INFO(MEDIA_INDEXER_DEVICE, 0, "Plugin will scan '%s' for us", uri_.c_str());
#if PERFCHECK_ENABLE
    PERF_START("TOTAL");
#endif
    resetMediaItemCount();
    queue_.push_back(uri_);
    cv_.notify_one();
    return true;
}

IMediaItemObserver *Device::observer() const
{
    std::shared_lock lock(lock_);
    return observer_;
}

std::string Device::mountpoint() const
{
    std::shared_lock lock(lock_);
    return mountpoint_;
}

void Device::setMountpoint(const std::string &mp)
{
    std::unique_lock lock(lock_);
    mountpoint_ = mp;
}

std::shared_ptr<Plugin> Device::plugin() const
{
    PluginFactory fac;
    return fac.plugin(uri_);
}

void Device::incrementMediaItemCount(MediaItem::Type type)
{
    if (type == MediaItem::Type::EOL)
        return;

    std::unique_lock lock(lock_);

    auto cntIter = mediaItemCount_.find(type);
    if (cntIter == mediaItemCount_.end())
        mediaItemCount_[type] = 1;
    else
        mediaItemCount_[type]++;
    totalItemCount_++;
}

void Device::incrementProcessedItemCount(MediaItem::Type type, int count)
{
    if (type == MediaItem::Type::EOL)
        return;

    std::unique_lock lock(lock_);

    auto cntIter = processedCount_.find(type);
    if (cntIter == processedCount_.end())
        processedCount_[type] = count;
    else
        processedCount_[type] += count;
    totalProcessedCount_ += count;
}

void Device::incrementRemovedItemCount(MediaItem::Type type, int count)
{
    if (type == MediaItem::Type::EOL)
        return;

    std::unique_lock lock(lock_);

    auto cntIter = removedCount_.find(type);
    if (cntIter == removedCount_.end())
        removedCount_[type] = count;
    else
        removedCount_[type] += count;
    totalRemovedCount_ += count;

}

void Device::incrementTotalProcessedItemCount(int count)
{
    std::unique_lock lock(lock_);
    totalProcessedCount_ += count;
}

void Device::incrementTotalRemovedItemCount(int count)
{
    std::unique_lock lock(lock_);
    totalRemovedCount_ += count;
}

void Device::incrementPutItemCount(int count)
{
    std::unique_lock lock(lock_);
    putCount_ += count;
}

void Device::incrementDirtyItemCount(int count)
{
    std::unique_lock lock(lock_);
    dirtyCount_ += count;
}

void Device::incrementRemoveItemCount(int count)
{
    std::unique_lock lock(lock_);
    removeCount_ += count;
}

bool Device::needFlushed()
{
    if ((state_ == Device::State::Parsing) && (totalItemCount_ == putCount_))
        return true;
    return false;
}

bool Device::needDirtyFlushed()
{
    return totalItemCount_ == dirtyCount_;
}

bool Device::needFlushedForRemove()
{
    if (totalRemovedCount_ != removeCount_)
        return true;

    return false;
}

bool Device::processingDone()
{
    std::unique_lock<std::mutex> lock(pmtx_);
    if (state_ == Device::State::Parsing) {
        LOG_INFO(MEDIA_INDEXER_DEVICE, 0, "Item Count : %d, Proccessed Count : %d, Removed Count :%d", totalItemCount_, 
                totalProcessedCount_, totalRemovedCount_);
        if ((totalItemCount_ == totalProcessedCount_) && (removeCount_ == totalRemovedCount_)) {
            setState(Device::State::Idle);
            auto obs = observer();
            if (obs)
                obs->notifyDeviceScanned();
#if PERFCHECK_ENABLE
            PERF_END("TOTAL");
            LOG_PERF("Item Count : %d, Proccessed Count : %d, Removed Count : %d",
                    totalItemCount_, totalProcessedCount_, totalRemovedCount_);
#endif
            return true;
        } else if (needDirtyFlushed()) {
            auto obs = observer();
            if (obs)
                obs->flushUnflagDirty(this);
        } else if (needFlushedForRemove()) {
            auto obs = observer();
            if (obs)
                obs->flushDeleteItems(this);
        }
    }
    return false;
}

void Device::activateCleanUpTask()
{
    cleanUpTask_.sendMessage(this, nullptr);
}

void Device::resetMediaItemCount()
{
    mediaItemCount_.clear();
    processedCount_.clear();
    removedCount_.clear();
    totalItemCount_ = totalProcessedCount_ = totalRemovedCount_ = 0;
    putCount_ = 0;
    dirtyCount_ = 0;
    removeCount_ = 0;
    auto mdb = MediaDb::instance();
    mdb->resetFirstScanTempBuf(uri_);
    mdb->resetReScanTempBuf(uri_);
}

void Device::setState(Device::State state, bool force)
{
    if (state == state_)
        return;
    LOG_DEBUG(MEDIA_INDEXER_DEVICE, "Device state change: %s -> %s",
        stateToString(state_).c_str(), stateToString(state).c_str());
    state_ = state;
}

int Device::mediaItemCount(MediaItem::Type type)
{
    std::shared_lock lock(lock_);
    auto cntIter = mediaItemCount_.find(type);
    if (cntIter == mediaItemCount_.end())
        return 0;
    else
        return mediaItemCount_[type];
}

bool Device::createThumbnailDirectory()
{
    bool ret = true;
    std::error_code err;
    std::string thumbnailDir = THUMBNAIL_DIRECTORY + uuid_;
    if (!std::filesystem::is_directory(thumbnailDir))
    {
        if (!std::filesystem::create_directory(thumbnailDir, err))
        {
            LOG_ERROR(MEDIA_INDEXER_DEVICE, 0, "Failed to create directory %s, error : %s",thumbnailDir.c_str(), err.message().c_str());
            LOG_DEBUG(MEDIA_INDEXER_DEVICE, "Retry with create_directories");
            if (!std::filesystem::create_directories(thumbnailDir, err)) {
                LOG_ERROR(MEDIA_INDEXER_DEVICE, 0, "Retry Failed, error : %s", err.message().c_str());
                ret = false;
            }
        }
    }
    return ret;
}

bool Device::createCacheDirectory()
{
    bool ret = true;
    std::error_code err;
    std::string cacheDir = CACHE_DIRECTORY + uuid_;
    if (!std::filesystem::is_directory(cacheDir))
    {
        if (!std::filesystem::create_directory(cacheDir, err))
        {
            LOG_ERROR(MEDIA_INDEXER_DEVICE, 0, "Failed to create directory %s, error : %s",cacheDir.c_str(), err.message().c_str());
            LOG_DEBUG(MEDIA_INDEXER_DEVICE, "Retry with create_directories");
            if (!std::filesystem::create_directories(cacheDir, err)) {
                LOG_ERROR(MEDIA_INDEXER_DEVICE, 0, "Retry Failed, error : %s", err.message().c_str());
                ret = false;
            }
        }
    }
    return ret;
}

bool Device::checkAlive()
{
    if (alive_ <= 0)
        return (alive_ != 0);

    if (--alive_ < 0)
        alive_ = -1;

    return !!(alive_);
}

void Device::resetAlive()
{
    alive_ = maxAlive_;
}

bool Device::isNewMountedDevice() const
{
    std::shared_lock lock(lock_);
    return newMountedDevice_;
}

void Device::setNewMountedDevice(bool isNew)
{
    std::unique_lock lock(lock_);
    newMountedDevice_ = isNew;
}
