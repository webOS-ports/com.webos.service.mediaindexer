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
#include "cache/cachemanager.h"
#if defined HAS_LUNA
#include "dbconnector/devicedb.h"
#include "dbconnector/mediadb.h"
#include "dbconnector/settingsdb.h"
#endif

#include <iostream>
std::unique_ptr<MediaIndexer> MediaIndexer::instance_;
GMainLoop *MediaIndexer::mainLoop_ = nullptr;

void MediaIndexer::init(GMainLoop *mainLoop)
{
    mainLoop_ = mainLoop;
}

MediaIndexer *MediaIndexer::instance()
{
    if (!instance_.get())
        instance_.reset(new MediaIndexer);
    return instance_.get();
}

MediaIndexer::MediaIndexer() :
    IDeviceObserver(),
    IMediaItemObserver(),
#if defined HAS_LUNA
    indexerService_(new IndexerService(this)),
#endif
    plugins_()
{
    Configurator::instance();
    CacheManager::instance();
}

MediaIndexer::~MediaIndexer()
{
    // nothing to be done here
}

bool MediaIndexer::get(const std::string &uri)
{
    PluginFactory factory;

    // check if we need to create all available plugins
    if (uri.empty()) {
        const std::list<std::string> &list = factory.plugins();
        if (list.empty())
            return false;

        for (auto const plgUri : list)
            get(plgUri);

        return true;
    }

    // is the plugin already there?
    if (!hasPlugin(uri)) {
        auto plg = factory.plugin(uri);
        if (!plg)
            return false;

        std::shared_lock lock(lock_);
        LOG_DEBUG(MEDIA_INDEXER_MEDIAINDEXER, "add plugin uri : %s to plugins",plg->uri().c_str());
        plugins_[plg->uri()] = plg;

#if defined HAS_LUNA
        // update settings for this plugin
        SettingsDb *settingsDb = SettingsDb::instance();
        settingsDb->applySettings(uri);
#endif
    }
#if defined HAS_LUNA
    // notify subscribers
    indexerService_->pushDeviceList();
#endif

    return true;
}

bool MediaIndexer::addPlugin(const std::string &uri)
{
    PluginFactory factory;
    if (!uri.empty()) {
        if (!hasPlugin(uri)) {
            auto plg = factory.plugin(uri);
            if (!plg)
                return false;

            std::shared_lock lock(lock_);
            plugins_[plg->uri()] = plg;
            LOG_DEBUG(MEDIA_INDEXER_MEDIAINDEXER, "add plugin uri : %s to plugins",plg->uri().c_str());
        }
    } else {
        LOG_ERROR(MEDIA_INDEXER_MEDIAINDEXER, 0, "Invalid Input parameter");
        return false;
    }
    return true;
}

bool MediaIndexer::put(const std::string &uri)
{
    if (!hasPlugin(uri))
        return false;

    {
        std::shared_lock lock(lock_);

        const auto & [plgUri, plg] = *plugins_.find(uri);;
        plg->setDeviceNotifications(this, false);
#if defined HAS_LUNA
        // database shall no longer care about these devices
        plg->setDeviceNotifications(DeviceDb::instance(), false);
#endif
    }

    {
        std::unique_lock lock(lock_);
        plugins_.erase(uri);
    }

#if defined HAS_LUNA
    // notify subscribers
    indexerService_->pushDeviceList();
#endif

    return true;
}

bool MediaIndexer::setDetect(bool on)
{
    std::shared_lock lock(lock_);
    LOG_DEBUG(MEDIA_INDEXER_MEDIAINDEXER, "setDetect Start");
    for (auto const & [uri, plg] : plugins_) {
        LOG_DEBUG(MEDIA_INDEXER_MEDIAINDEXER, "uri : %s",uri.c_str());
        setDetect(on, uri);
    }

    return true;
}

bool MediaIndexer::setDetect(bool on, const std::string &uri)
{
    if (!hasPlugin(uri)) {
        LOG_DEBUG(MEDIA_INDEXER_MEDIAINDEXER, "%s is not included in plugin list of mediaindexer service", uri.c_str());
        return false;
    }
    LOG_DEBUG(MEDIA_INDEXER_MEDIAINDEXER, "Plugin Found");
    {
        std::shared_lock lock(lock_);

        const auto & [plgUri, plg] = *plugins_.find(uri);
#if defined HAS_LUNA
        // enable/disable database device notify listener
        plg->setDeviceNotifications(DeviceDb::instance(), on);

        // check database for already known devices for this plugin
        auto dbConn = DeviceDb::instance();
        if (on)
            dbConn->injectKnownDevices(plg->uri());

        // now save the new state to settings database
        SettingsDb *settingsDb = SettingsDb::instance();
        settingsDb->setEnable(plgUri, on);
#endif
        plg->setDeviceNotifications(this, on);
    }
#if defined HAS_LUNA
    // notify subscribers
    indexerService_->pushDeviceList();
#endif

    return true;
}

bool MediaIndexer::sendDeviceNotification(LSMessage * msg)
{
    if (indexerService_)
        return indexerService_->pushDeviceList(msg);
    else
        return false;
}

void MediaIndexer::deviceStateChanged(std::shared_ptr<Device> device)
{
    LOG_INFO(MEDIA_INDEXER_MEDIAINDEXER, 0, "Device '%s' has been %s", device->uri().c_str(),
        device->available() ? "added" : "removed");

#if defined HAS_LUNA
    // notify subscribers
    indexerService_->pushDeviceList();
#endif

    // start device media scan
    if (device->available()) {
        auto mdb = MediaDb::instance();
        mdb->unmarkAllDirty(device);
        device->scan(this);
#if defined HAS_LUNA
    } else {
        // mark all device media items dirty
        auto mdb = MediaDb::instance();
        mdb->markDirty(device);
#endif
    }
}

void MediaIndexer::deviceModified(std::shared_ptr<Device> device)
{
    LOG_INFO(MEDIA_INDEXER_MEDIAINDEXER, 0, "Device '%s' has been modified", device->uri().c_str());

#if defined HAS_LUNA
    // notify subscribers
    indexerService_->pushDeviceList();
#endif
}

void MediaIndexer::newMediaItem(MediaItemPtr mediaItem)
{
    // this helps us for logging
    auto dev = mediaItem->device();

    // if the media item has not yet been parsed we first check if
    // parsing is necessary at all
    if (!mediaItem->parsed()) {
        LOG_INFO(MEDIA_INDEXER_MEDIAINDEXER, 0, "New media item '%s' on '%s' found"
            " with hash '%lu' and type '%s'", mediaItem->uri().c_str(),
            dev->uri().c_str(), mediaItem->hash(),
            MediaItem::mediaTypeToString(mediaItem->type()).c_str());

#if defined HAS_LUNA
        auto mdb = MediaDb::instance();
        //mdb->checkForChange(std::move(mediaItem));
        if (dev->isNewMountedDevice()) {
            metaDataUpdateRequired(std::move(mediaItem));
        } else {
            if (mdb->needUpdate(mediaItem.get())) {
                metaDataUpdateRequired(std::move(mediaItem));
            } else
                mdb->unflagDirty(std::move(mediaItem));
        }
#else
        LOG_INFO(MEDIA_INDEXER_MEDIAINDEXER, 0, "Device '%s' media item count (audio/video/images): %i/%i/%i",
            dev->uri().c_str(), dev->mediaItemCount(MediaItem::Type::Audio),
            dev->mediaItemCount(MediaItem::Type::Video),
            dev->mediaItemCount(MediaItem::Type::Image));
        MediaParser::enqueueTask(std::move(mediaItem));
#endif
    } else {
        // if parsing has been completed update the media database
        LOG_INFO(MEDIA_INDEXER_MEDIAINDEXER, 0, "Media item '%s' has been parsed", mediaItem->uri().c_str());
#if defined HAS_LUNA
        auto mdb = MediaDb::instance();
        mdb->updateMediaItem(std::move(mediaItem));
#endif
    }
}

void MediaIndexer::removeMediaItem(MediaItemPtr mediaItem) 
{
    auto mdb = MediaDb::instance();
    mdb->requestDeleteItem(std::move(mediaItem));
}

void MediaIndexer::metaDataUpdateRequired(MediaItemPtr mediaItem)
{
    MediaParser::enqueueTask(std::move(mediaItem));
}

bool MediaIndexer::hasPlugin(const std::string &uri) const
{
    std::shared_lock lock(lock_);

    auto plg = plugins_.find(uri);
    return (plg != plugins_.end());
}

void MediaIndexer::cleanupDevice(Device* device)
{
    auto mdb = MediaDb::instance();
    mdb->removeDirty(device);
}

void MediaIndexer::flushUnflagDirty(Device* device)
{
    auto mdb = MediaDb::instance();
    mdb->flushUnflagDirty(device);
}

void MediaIndexer::flushDeleteItems(Device* device)
{
    auto mdb = MediaDb::instance();
    mdb->flushDeleteItems(device);
}

void MediaIndexer::notifyDeviceScanned()
{
    indexerService_->notifyScanDone();
    indexerService_->pushDeviceList();
}

void MediaIndexer::notifyDeviceList()
{
    indexerService_->pushDeviceList();
}

bool MediaIndexer::sendMediaMetaDataNotification(const std::string &method,
                                                 const std::string &metaData,
                                                 LSMessage *msg)
{
    LOG_INFO(MEDIA_INDEXER_MEDIAINDEXER, 0, "MediaIndexer::sendMediaListNotification");
    return indexerService_->notifyMediaMetaData(method, metaData, msg);
}

Device *MediaIndexer::findDevice(const std::string &uri)
{
    Device *ret = nullptr;
    for (auto const &[puri, plg] : plugins_) {
        for (auto const &[duri, dev] : plg->devices()) {
            if (duri == uri)
                return dev.get();
        }
    }
    return ret;
}

