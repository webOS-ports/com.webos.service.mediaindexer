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

#pragma once

#include "ideviceobserver.h"
#include "imediaitemobserver.h"
#include "plugins/plugin.h"
#include "logging.h"
#include "configurator.h"
#if defined HAS_LUNA
#include "indexerservice.h"
#endif

#include <glib.h>

#include <map>
#include <memory>

/// Media indexer class.
class MediaIndexer : public IDeviceObserver, public IMediaItemObserver
{
#if defined HAS_LUNA
    /// Allow IndexerService to access us directly.
    friend IndexerService;
#endif

public:
    /**
     * \brief Initialize with mainloop.
     */
    static void init(GMainLoop *mainLoop);

    /**
     * \brief Get singleton object of MediaIndexer.
     *
     * \return The singleton.
     */
    static MediaIndexer *instance();

    virtual ~MediaIndexer();

    /**
     * \brief Create plugin if not yet exists.
     *
     * If uri is empty create all available plugins.
     *
     * \param[in] uri Base uri.
     */
    bool get(const std::string &uri);

    /**
     * \brief Add plugin if not yet exists.
     *
     *
     * \param[in] uri Base uri.
     */
    bool addPlugin(const std::string &uri);

    /**
     * \brief Decrease plugin refcount and release if unused.
     *
     * \param[in] uri Base uri.
     */
    bool put(const std::string &uri);

    /**
     * \brief Set device detection for all available plugins.
     *
     * \param[in] on Turn detection on or off.
     */
    bool setDetect(bool on);

    /**
     * \brief Set device detection for given base uri.
     *
     * \param[in] on Turn detection on or off.
     * \param[in] uri Base uri for plugin identification.
     */
    bool setDetect(bool on, const std::string &uri);

    /**
     * \brief Generate device list object and push either as reply to
     * \p msg or to all registered subscribers.
     *
     * This internally calls pushDeviceList of indexerservice object.
     * \param[in] msg The Luna message.
     */
    bool sendDeviceNotification(LSMessage *msg = nullptr);

    /**
     * \brief Activate plugins to detect
     *
     * This internally calls get and setDetect.
     */
    bool activate();

    bool sendMediaMetaDataNotification(const std::string &method,
                                       const std::string &metaData,
                                       LSMessage *msg);
    /**
     * \brief find registered device.
     * \param[in] uri The uri of corresponding device.
     */
    Device *findDevice(const std::string &uri);
protected:
    /// Activate plugins and detection
    static gboolean _activate(gpointer data);
    /// DeviceObserver interface.
    void deviceStateChanged(DevicePtr device);

    /// DeviceObserver interface.
    void deviceModified(DevicePtr device);

    /// MediaItemObserver interface.
    void newMediaItem(MediaItemPtr mediaItem);

    /// MediaItemObserver interface.
    void metaDataUpdateRequired(MediaItemPtr mediaItem);

    /// MediaItemObserver interface.
    void cleanupDevice(Device* device);

    /// MediaItemObserver interface.
    void flushUnflagDirty(Device* device);

    /// MediaItemObserver interface.
    void flushDeleteItems(Device* device);

    /// MediaItemObserver interface.
    void notifyDeviceScanned();

    /// MediaItemObserver interface.
    void notifyDeviceList();

    /// MediaItemObserver interface.
    void removeMediaItem(MediaItemPtr mediaItem);

private:
    /// Singleton.
    MediaIndexer();

    /**
     * \brief Check if plugin is already known.
     *
     * \param[in] uri The plugin uri.
     * \return True if know, else false.
     */
    bool hasPlugin(const std::string &uri) const;

    /// Singleton instance object.
    static std::unique_ptr<MediaIndexer> instance_;
    /// Glib main loop.
    static GMainLoop *mainLoop_;

#if defined HAS_LUNA
    /// The Luna service implementation.
    std::unique_ptr<IndexerService> indexerService_;
#endif

    /// Plugins currently in use.
    std::map<std::string, std::shared_ptr<Plugin>> plugins_;

    /// For locking internal structures.
    mutable std::shared_mutex lock_;

    /// media indexer configurator from json configuration file
    std::unique_ptr<Configurator> configurator_;
};
