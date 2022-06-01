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

#include "plugin.h"
#include "ideviceobserver.h"
#include "configurator.h"
#include "cachemanager.h"
#include <algorithm>
#include <filesystem>
#include <cinttypes>

#include <gio/gio.h>

namespace fs = std::filesystem;
bool Plugin::matchUri(const std::string &refUri, const std::string &testUri)
{
    return !testUri.compare(0, refUri.size(), refUri);
}

Plugin::~Plugin()
{
    // Nothing to be done here.
}

void Plugin::lock()
{
    lock_.lock_shared();
}

void Plugin::unlock()
{
    lock_.unlock_shared();
}

void Plugin::setDeviceNotifications(IDeviceObserver *observer, bool on)
{
    LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "setDeviceNotifications Start");
    if (on) {
        bool firstObserver = addObserver(observer);

        {
            std::shared_lock lock(lock_);

            // now tell the new observer about the current state of
            // devices
            for (auto const dev :  devices_)
                notifyObserversStateChange(dev.second, observer);
        }

        // If this is the first observer we need to start device
        // detection.
        if (firstObserver) {
            LOG_INFO(MEDIA_INDEXER_PLUGIN, 0, "Enable device detection for: '%s'", uri_.c_str());
            runDeviceDetection(true);
        }
        else {
            LOG_INFO(MEDIA_INDEXER_PLUGIN, 0, "Not firstObserver, skip runDeviceDetection for: '%s'", uri_.c_str());
        }
    } else {
        LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "removeObserver");
        removeObserver(observer);
    }
    LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "setDeviceNotifications Done");
}

const std::string &Plugin::uri() const
{
    // no lock required, this is set only once from the constructor
    return uri_;
}

bool Plugin::injectDevice(std::shared_ptr<Device> device)
{
    bool isNew = false;

    if (!hasDevice(device->uri())) {
        std::unique_lock lock(lock_);
        devices_.emplace(device->uri(), device);
        isNew = true;
    }

    if (isNew)
        notifyObserversStateChange(device);

    return isNew;
}

bool Plugin::injectDevice(const std::string &uri, int alive, bool avail, std::string uuid)
{
    LOG_INFO(MEDIA_INDEXER_PLUGIN, 0, "uri = [%s], uuid[%s]", uri.c_str(), uuid.c_str());
    bool isNew = false;

    if (!hasDevice(uri)) {
        std::unique_lock lock(lock_);
        LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "Make new device for uri : %s, uuid : %s", uri.c_str(), uuid.c_str());
        devices_[uri] = std::make_shared<Device>(uri, alive, avail, uuid);
        isNew = true;
    } else {
        auto dev = device(uri);
        dev->setNewMountedDevice(isNew);
    }

    if (isNew)
        notifyObserversStateChange(devices_[uri]);

    return isNew;
}

bool Plugin::addDevice(const std::string &uri, int alive)
{
    bool isNew = false;
    std::shared_ptr<Device> dev = nullptr;

    {
        std::unique_lock lock(lock_);
        dev = deviceUnlocked(uri);

        if (!dev) {
            dev = std::make_shared<Device>(uri, alive);
            dev->init();
            LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "Make new device for uri : %s", uri.c_str());
            devices_[uri] = dev;
            isNew = true;
        }
    }

    if (isNew) {
        notifyObserversStateChange(dev);
    } else {
        dev = device(uri);
        dev->init();
        auto changed = dev->setAvailable(true);
        // now tell the observers if availability changed
        if (changed)
            notifyObserversStateChange(dev);
    }

    return isNew;
}

bool Plugin::addDevice(const std::string &uri, const std::string &mp, std::string uuid, int alive)
{
    bool isNew = false;
    std::shared_ptr<Device> dev = nullptr;

    {
        std::unique_lock lock(lock_);
        dev = deviceUnlocked(uri);

        if (!dev) {
            LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "Make new device for uri : %s, uuid : %s", uri.c_str(), uuid.c_str());
            dev = std::make_shared<Device>(uri, alive, true, uuid);
            dev->init();
            dev->setMountpoint(mp);
            devices_[uri] = dev;
            isNew = true;
        }
    }

    if (isNew) {
        notifyObserversStateChange(dev);
    } else {
        dev = device(uri);
        dev->init();
        auto changed = dev->setAvailable(true);
        dev->setMountpoint(mp);
        dev->setUuid(uuid);
        dev->setNewMountedDevice(isNew);
        // now tell the observers if availability changed
        if (changed)
            notifyObserversStateChange(dev);
    }

    return isNew;
}

bool Plugin::removeDevice(const std::string &uri)
{
    auto dev = device(uri);

    if (!dev)
        return false;

    auto changed = dev->setAvailable(false);

    // now tell the observers if availability changed
    if (changed)
        notifyObserversStateChange(dev);

    return changed;
}

void Plugin::removeAll(void)
{
    std::shared_lock lock(lock_);

    // mark the device as not available and notify observers if the
    // available state really changed
    for (auto const dev :  devices_) {
        if (dev.second->setAvailable(false))
            notifyObserversStateChange(dev.second);
    }
}

void Plugin::modifyDevice(const std::string &uri, Device::Meta type,
    const std::string &value)
{
    auto dev = device(uri);

    // notify observers if meta data has changed
    if (dev && dev->setMeta(type, value))
        notifyObserversModify(dev);
}

bool Plugin::hasDevice(const std::string &uri) const
{
    auto dev = device(uri);
    return !!dev;
}

std::shared_ptr<Device> Plugin::device(const std::string &uri) const
{
    std::shared_lock lock(lock_);

    return deviceUnlocked(uri);
}

void Plugin::checkDevices(void)
{
    std::shared_lock lock(lock_);

    for (auto const dev : devices_) {
        // if device is still alive step to next one
        auto alive = dev.second->available();
        if (dev.second->available(true) == alive)
            continue;
        notifyObserversStateChange(dev.second);
    }
}

const std::map<std::string, std::shared_ptr<Device>> &Plugin::devices() const
{
    return devices_;
}

bool Plugin::active() const
{
    // protect the request
    std::shared_lock lock(lock_);

    return !deviceObservers_.empty();
}

void Plugin::scan(const std::string &uri)
{
    LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "Scan start! uri : %s", uri.c_str());
    // first get the device from the uri
    auto dev = device(uri);
    if (!dev)
        return;

    auto obs = dev->observer();
    if (!obs) {
        LOG_ERROR(MEDIA_INDEXER_PLUGIN, 0, "device %s has no observer, observer is manadatory", dev->uri().c_str());
        return;
    }

    // get the device mountpoint
    auto mp = dev->mountpoint();
    if (mp.empty()) {
        LOG_ERROR(MEDIA_INDEXER_PLUGIN, 0, "Device '%s' has no mountpoint", dev->uri().c_str());
        return;
    }
    LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "file scan start for mountpoint : %s!", mp.c_str());

    bool newMountedDevice = dev->isNewMountedDevice();
    bool ret = false;
    if (newMountedDevice) {
        LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "Device %s is new mounted device!", dev->uri().c_str());
        ret = doFileTreeWalk(dev, obs, mp);
    } else {
        LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "Device %s is not new mounted device, cache is used!", dev->uri().c_str());
        ret = doFileTreeWalkWithCache(dev, obs, mp);
    }

    if (!ret) {
        LOG_ERROR(MEDIA_INDEXER_PLUGIN, 0, "Failed file-tree-walk for '%s'", dev->uri().c_str());
        return;
    }

    LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "Scan has been completed for uri : %s!", uri.c_str());
}

bool Plugin::doFileTreeWalkWithCache(const std::shared_ptr<Device>& device,
                                     IMediaItemObserver* observer,
                                     const std::string& mountPoint)
{
    auto configurator = Configurator::instance();
    auto cacheMgr = CacheManager::instance();
    auto cache = cacheMgr->readCache(device->uri(), device->uuid());
    if (!cache) {
        LOG_WARNING(MEDIA_INDEXER_PLUGIN, 0, "Failed to get the cache for '%s'. let's try full scanning instead!",
                device->uri().c_str());
        return doFileTreeWalk(device, observer, mountPoint);
    }

    try {
        for (const auto &file : fs::recursive_directory_iterator(mountPoint)) {
            std::error_code err;
            if (!file.is_regular_file(err)) {
                LOG_WARNING(MEDIA_INDEXER_PLUGIN, 0, "'%s' is not regular file. error message : '%s'",
                        file.path().c_str(), err.message().c_str());
                continue;
            }

            std::string path = file.path();
            std::string mimeType;
            if (isHiddenfolder(path))
                continue;

            std::string ext = path.substr(path.find_last_of('.') + 1);
            auto typeInfo = configurator->getTypeInfo(ext);
            if (typeInfo.first == MediaItem::Type::EOL) {
                LOG_WARNING(MEDIA_INDEXER_PLUGIN, 0, "'%s' is NOT supported!", ext.c_str());
                continue;
            }

            auto type = typeInfo.first;
            auto extractorType = typeInfo.second;
            auto fileSize = file.file_size();
            auto lastWrite = file.last_write_time();
            auto hash = static_cast<unsigned long>(lastWrite.time_since_epoch().count());

            // check the cache whether exist or not
            bool exist = cache->isExist(path, hash);
            if (exist) {
                LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "not needed extraction for path '%s' and hash '%" PRIu64 "'",
                        ext.c_str(), hash);
                device->incrementMediaItemCount(type);
                device->incrementProcessedItemCount(type);
                continue;
            }

            MediaItemPtr mi = std::make_unique<MediaItem>(device, path, mimeType, hash,
                    fileSize, ext, type, extractorType);
            auto thumbnail = mi->getThumbnailFileName();
            cache->insertItem(path, hash, type, thumbnail);
            observer->newMediaItem(std::move(mi));
        }
    } catch (const std::exception &ex) {
        LOG_ERROR(MEDIA_INDEXER_PLUGIN, 0, "Exception caught while traversing through '%s', exception : %s",
            mountPoint.c_str(), ex.what());
    }
    LOG_INFO(MEDIA_INDEXER_PLUGIN, 0, "File-tree-walk(with cache) on device '%s' has been completed",
        device->uri().c_str());

    // we have to remove remaining cache with mediaDB.
    auto remainingCache = cache->getRemainingCache();
    for (auto& item : remainingCache) {
        auto uri = item.first;
        auto hash = std::get<0>(item.second);
        auto type = std::get<1>(item.second);
        auto thumb = std::get<2>(item.second);
        std::string ext = uri.substr(uri.find_last_of('.') + 1);

        // we don't have to increase media item count.
        MediaItemPtr mi = std::make_unique<MediaItem>(device, uri, hash, type);

        // let's first remove thumbnail.
        std::string filePath = THUMBNAIL_DIRECTORY + device->uuid() + "/" + thumb;
        std::filesystem::remove(filePath);
        // now, we have to remove database for syncronization
        observer->removeMediaItem(std::move(mi));
    }
    bool ret = cacheMgr->generateCacheFile(device->uri(), cache);
    if (!ret)
        LOG_WARNING(MEDIA_INDEXER_PLUGIN, 0, "Cache file generation fail for '%s'", device->uri().c_str());
    sync();
    return true;
}

bool Plugin::doFileTreeWalk(const std::shared_ptr<Device> &device,
                            IMediaItemObserver* observer,
                            const std::string& mountPoint)
{
    auto configurator = Configurator::instance();
    auto cacheMgr = CacheManager::instance();
    auto cache = cacheMgr->createCache(device->uri(), device->uuid());

    try {
        for (auto &file : fs::recursive_directory_iterator(mountPoint)) {
            std::error_code err;
            if (!file.is_regular_file(err)) {
                LOG_WARNING(MEDIA_INDEXER_PLUGIN, 0, "'%s' is not regular file. error message : '%s'",
                        file.path().c_str(), err.message().c_str());
                continue;
            }

            std::string path = file.path();
            std::string mimeType;
            if (isHiddenfolder(path))
                continue;

            std::string ext = path.substr(path.find_last_of('.') + 1);
            auto typeInfo = configurator->getTypeInfo(ext);
            if (typeInfo.first == MediaItem::Type::EOL) {
                LOG_WARNING(MEDIA_INDEXER_PLUGIN, 0, "'%s' is NOT supported!", ext.c_str());
                continue;
            }

            auto type = typeInfo.first;
            auto extractorType = typeInfo.second;
            auto lastWrite = file.last_write_time();
            auto fileSize = file.file_size();
            auto hash = static_cast<unsigned long>(lastWrite.time_since_epoch().count());
            MediaItemPtr mi = std::make_unique<MediaItem>(device, path, mimeType, hash,
                    fileSize, ext, type, extractorType);
            auto thumbnail = mi->getThumbnailFileName();
            cache->insertItem(path, hash, type, thumbnail);
            observer->newMediaItem(std::move(mi));

            /*
            if (MediaItem::mediaItemSupported(path, mimeType)) {
                auto lastWrite = file.last_write_time();
                auto fileSize = file.file_size();
                auto hash = lastWrite.time_since_epoch().count();
                MediaItemPtr mi(new MediaItem(device,
                        path, mimeType, hash, fileSize));
                obsserver->newMediaItem(std::move(mi));
            } else {
                LOG_WARNING(MEDIA_INDEXER_PLUGIN, 0, "mediaItem : %s is not supported", path.c_str());
            }
            */
        }
    } catch (const std::exception &ex) {
        LOG_ERROR(MEDIA_INDEXER_PLUGIN, 0, "Exception caught while traversing through '%s', exception : %s",
            mountPoint.c_str(), ex.what());
        return false;
    }
    LOG_INFO(MEDIA_INDEXER_PLUGIN, 0, "File-tree-walk on device '%s' has been completed",
        device->uri().c_str());

    bool ret = cacheMgr->generateCacheFile(device->uri(), cache);
    if (!ret)
        LOG_WARNING(MEDIA_INDEXER_PLUGIN, 0, "Cache file generation fail for '%s'", device->uri().c_str());

    return true;
}
/*
bool Plugin::checkFileInfomation(const fs::directory_entry& file)
{
    std::error_code err;
    if (!file.is_regular_file(err)) {
        LOG_WARNING(MEDIA_INDEXER_PLUGIN, 0, "'%s' is not regular file. error message : '%s'",
                file.path().c_str(), err.message().c_str());
        return false;
    }

    std::string path = file.path();
    std::string mimeType;
    if (isHiddenfolder(path))
        return false;

    std::string ext = path.substr(path.find_last_of('.') + 1);
    auto configurator = Configurator::instance();
    if (!configurator->isSupportedExtension(ext)) {
        LOG_WARNING(MEDIA_INDEXER_PLUGIN, 0, "'%s' is NOT supported!", ext.c_str());
        return false;
    }
    return true;
}
*/
bool Plugin::isHiddenfolder(std::string &filepath)
{
    if(filepath.find("/.") != -1)
        return true;
    return false;
}

void Plugin::extractMeta(MediaItem &mediaItem, bool expand)
{
    LOG_ERROR(MEDIA_INDEXER_PLUGIN, 0, "No meta data extraction for '%s'", mediaItem.uri().c_str());
    // nothing to be done here, this should be implemented in the
    // derived plubin classes if needed
}

std::optional<std::string> Plugin::getPlaybackUri(const std::string &uri)
{
    auto dev = device(uri);
    if (!dev)
        return std::nullopt;

    if (!dev->available())
        return std::nullopt;

    auto path = std::string("file://");
    path.append(uri);
    // remove the device uri from the path
    path.erase(7, dev->uri().length());

    LOG_DEBUG(MEDIA_INDEXER_PLUGIN, "Playback uri for '%s' is '%s'", uri.c_str(), path.c_str());
    return path;
}

Plugin::Plugin(const std::string &uri) :
    uri_(uri),
    devices_(),
    deviceObservers_()
{
    // Nothing to be done here.
}

std::shared_ptr<Device> Plugin::deviceUnlocked(const std::string &uri) const
{
    for (auto &dev : devices_) {
        if (matchUri(dev.first, uri))
            return dev.second;
    }

    return nullptr;
}

bool Plugin::addObserver(IDeviceObserver *observer)
{
    std::unique_lock lock(lock_);

    if (!observer)
        return (deviceObservers_.size() == 1);

    // Check if observer is already registered.
    auto it = std::find_if(deviceObservers_.begin(), deviceObservers_.end(),
        [observer](IDeviceObserver *obs) { return (obs == observer); });
    if (it != deviceObservers_.end())
        return false;

    // Remember the observer for notifications.
    deviceObservers_.push_back(observer);

    return (deviceObservers_.size() == 1);
}

void Plugin::removeObserver(IDeviceObserver *observer)
{
    if (addObserver(nullptr)) {
        LOG_INFO(MEDIA_INDEXER_PLUGIN, 0, "Disable device detection for: '%s'", uri_.c_str());
        runDeviceDetection(false);
        removeAll();
    }

    std::unique_lock lock(lock_);
    deviceObservers_.remove(observer);
}

void Plugin::notifyObserversStateChange(const std::shared_ptr<Device> &device,
    IDeviceObserver *observer) const
{
    if (observer) {
        observer->deviceStateChanged(device);
    } else {
        std::shared_lock lock(lock_);
        for (auto const obs : deviceObservers_)
            obs->deviceStateChanged(device);
    }
}

void Plugin::notifyObserversModify(const std::shared_ptr<Device> &device) const
{
    std::shared_lock lock(lock_);
    for (auto const obs : deviceObservers_)
        obs->deviceModified(device);
}
