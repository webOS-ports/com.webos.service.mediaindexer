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

#include "logging.h"
#include "mediaitem.h"
#include "task.h"

#if PERFCHECK_ENABLE
#include "performancechecker.h"
#endif

#include <string>
#include <chrono>
#include <map>
#include <shared_mutex>
#include <memory>
#include <thread>
#include <condition_variable>
#include <deque>
#include <vector>

class Plugin;

/// Base class for devices like MTP or UPnP servers.
class Device
{
public:
    /// Meta types for devices.
    enum class Meta {
        Name, ///< A human-readable device name.
        Description, ///< A human-readable device description.
        Icon, ///< A device identification icon uri.
        EOL ///< End of list marker.
    };

    /// Device indexing status.
    enum class State {
        Idle, ///< Scan has been completed, not monitoring.
        Scanning, ///< Device is in initial scan mode.
        Parsing, ///< Device is in media file parsing mode.
        Inactive ///< Device is not available.
    };

    /**
     * \brief Convert meta type to string.
     *
     * \param[in] meta Meta data type.
     * \return The related string.
     */
    static std::string metaTypeToString(Device::Meta meta);

    /**
     * \brief Convert state to string.
     *
     * \param[in] state The state identifier.
     * \return The related string.
     */
    static std::string stateToString(Device::State state);

    /**
    * \brief System wide search for a device by uri.
    */
    static std::shared_ptr<Device> device(const std::string &uri);

    /**
     * \brief Construct device by uri.
     *
     * \param[in] uri Full device uri.
     * \param[in] alive If alive count should be used set this to value >1.
     * \param[in] avail Set initial available state.
     */
    Device(const std::string &uri, int alive = -1, bool avail = true, std::string uuid = "");

    virtual ~Device();

    /**
     * \brief If the device is available.
     *
     * \param[in] check If true ddecrement and check alive refcount.
     * \return Availability status.
     */
    virtual bool available(bool check = false);

    /**
     * \brief Set the device available state.
     *
     * Calling this function will reset the alive refcount if used.
     *
     * \param[in] avail Available state.
     * \return True if availability changed, else false.
     */
    virtual bool setAvailable(bool avail);

    /**
     * \brief Tell the device uri.
     *
     * \return Uri string.
     */
    virtual const std::string &uri() const;

    /**
     * \brief Tell the device alive count.
     *
     * \return Alive count reset value.
     */
    virtual int alive() const;

    /**
     * \brief Get uuid.
     * \return uuid string.
     */
    virtual const std::string &uuid() const;

    /**
     * \brief Set uuid.
     * \param[in] uuid string.
     */
    virtual void setUuid(const std::string &uuid);

    /**
     * \brief Get meta data.
     *
     * \param[in] type Meta data type.
     * \return Meta data string.
     */
    virtual const std::string &meta(Device::Meta type) const;

    /**
     * \brief Set meta data.
     *
     * \param[in] type Meta data type.
     * \param[in] value Meta data value.
     * \return True if meta data has been changed, else false.
     */
    virtual bool setMeta(Device::Meta type, const std::string value);

    /**
     * \brief Get the state.
     *
     * \return The current state.
     */
    virtual Device::State state() const;

    /**
     * \brief Set state.
     *
     * \param[in] state New state.
     */
    virtual void setState(Device::State state);


    /**
     * \brief Get the device mount state that whether it's new device or not
     *
     * \return True if device is first mounted on target, else false.
     */
    virtual bool isNewMountedDevice() const;

    /**
     * \brief Set the device mount state that whether it's new device or not
     *
     * \param[in] isNew device mount state.
     */
    virtual void setNewMountedDevice(bool isNew);

    /**
     * \brief Tell the timestamp when the device was available last
     * time.
     *
     * \return Time value in seconds since epoch.
     */
    virtual const std::chrono::system_clock::time_point &lastSeen() const;

    /**
     *\brief Does device specific media item detection.
     *
     * \param[in] observer Observer class for this device class.
     * \return True if device cares of media items, else false.
     */
    virtual bool scan(IMediaItemObserver *observer = nullptr);

    /**
     *\brief Gives us the current media item observer.
     *
     * \return The observer or nullptr if none is set.
     */
    virtual IMediaItemObserver *observer() const;

    /**
     * \brief Get device mountpoint.
     *
     * \return Mountpoint if available, empty string else.
     */
    virtual std::string mountpoint() const;

    /**
     * \brief Set device mountpoint.
     *
     * \param[in] mp The current device mountpoint.
     */
    virtual void setMountpoint(const std::string &mp);

    /**
     * \brief Get plugin for device.
     *
     * \return The plugin for this type of device.
     */
    std::shared_ptr<Plugin> plugin() const;

    /**
     * \brief Increase media item count by one for given media type.
     *
     * \param[in] type Media item type.
     */
    void incrementMediaItemCount(MediaItem::Type type);

    /**
     * \brief Increase processed media item count by one for given media type.
     *
     * \param[in] type Media item type.
     * \param[in] count increment Processed Item Count.
     */
    void incrementProcessedItemCount(MediaItem::Type type, int count = 1);

    /**
     * \brief Increase removed media item count by one for given media type.
     *
     * \param[in] type Media item type.
     * \param[in] count increment Removed Item Count.
     */
    void incrementRemovedItemCount(MediaItem::Type type, int count = 1);

    /**
     * \brief Increase total processed media item count.
     *
     * \param[in] count increment Processed Item Count.
     */
    void incrementTotalProcessedItemCount(int count = 1);

    /**
     * \brief Increase total removed media item count.
     *
     * \param[in] count increment Removed Item Count.
     */
    void incrementTotalRemovedItemCount(int count = 1);

    /**
     * \brief Increase media item count by putMeta method.
     *
     * \param[in] count increment Processed Item Count.
     */
    void incrementPutItemCount(int count = 1);

    /**
     * \brief Increase media item count by unflagDirty method.
     *
     * \param[in] count increment Processed Item Count.
     */
    void incrementDirtyItemCount(int count = 1);

    /**
     * \brief Increase media item count by unflagDirty method.
     *
     * \param[in] count increment Processed Item Count.
     */
    void incrementRemoveItemCount(int count = 1);

    /**
     * \brief check whether the buffered data for put should be flushed or not.
     *
     */
    bool needFlushed();

    /**
     * \brief check whether the buffered data for unflagDirty should be flushed or not.
     *
     */
    bool needDirtyFlushed();

    bool needFlushedForRemove();
    /**
     * \brief check if processing of media items inside device is done.
     *
     */
    bool processingDone();

    /**
     * \brief If done, activate cleanup task to delete db not matched to
     *        media files.
     *
     */
    void activateCleanUpTask();

    /**
     * \brief Return the media item count for given type.
     *
     * \param[in] type Media item type.
     * \return Media item count.
     */
    virtual int mediaItemCount(MediaItem::Type type);

    /**
     * \brief Create directory dedicated to the device for thumbnails.
     *
     * \return creation success or failure.
     */
    virtual bool createThumbnailDirectory();

    /**
     * \brief Create directory dedicated to the device for cache.
     *
     * \return creation success or failure.
     */
    virtual bool createCacheDirectory();

    /**
     * \brief Thread loop for file scanning.
     *
     */
    void scanLoop();

    void init();

private:
    /// Uri mandatory for construction.
    Device(): state_(Device::State::Inactive), available_(false), alive_(0), maxAlive_(0), observer_(nullptr) {};

    /// Check and decrement alive count.
    bool checkAlive();
    /// Reset alive count to original value.
    void resetAlive();

    /// Reset media item count for all media types.
    void resetMediaItemCount();

    /// Handler for internal state changes.
    void setState(Device::State state, bool force);

    /// Make class meta data usage thread safe.
    mutable std::shared_mutex lock_;

    /// Device Uri
    std::string uri_;
    /// Device mountpoint
    std::string mountpoint_;
    /// Device uuid
    std::string uuid_;
    /// Last seen timestamp of device.
    std::chrono::system_clock::time_point lastSeen_;
    /// The meta data map. The member is mutable as element are
    /// default constructed if not yet set.
    mutable std::map<Device::Meta, std::string> meta_;
    /// The current device state.
    Device::State state_;
    /// Device available state.
    bool available_;
    /// Alive refcount for device poll-mode.
    int alive_;
    /// Alive reset value.
    int maxAlive_;
    bool newMountedDevice_ = true;

    std::thread task_;
    std::mutex mutex_;
    std::mutex pmtx_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    bool exit_ = false;

    /// Media item observer.
    IMediaItemObserver *observer_;

    /// Media item count per media type.
    std::map<MediaItem::Type, int> mediaItemCount_;
    int totalItemCount_ = 0;
    /// Processed media item count per media type.
    std::map<MediaItem::Type, int> processedCount_;
    int totalProcessedCount_ = 0;
    int putCount_ = 0;
    int dirtyCount_ = 0;

    /// Processed media item count per media type.
    std::map<MediaItem::Type, int> removedCount_;
    int totalRemovedCount_ = 0;
    int removeCount_ = 0;

    Task cleanUpTask_;
};

/// Useful when iterating over enum.
Device::Meta &operator++(Device::Meta &meta);

/// This is handled with shared_ptr so give it an alias.
typedef std::shared_ptr<Device> DevicePtr;
