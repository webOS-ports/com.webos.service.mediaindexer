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

#include "device.h"
#include "logging.h"

#include <memory>
#include <string>
#include <list>
#include <map>
#include <shared_mutex>

class IDeviceObserver;
class IMediaItemObserver;

/**
 * \brief Base class for device plugins.
 *
 * Each derived plugin needs to define two additional static members:
 * 'static std::string uri' which takes the uri prefix like 'upnp'
 */
class Plugin
{
public:
    /**
     * \brief Check if testUri includes refUri.
     *
     * \param[in] refUri The reference Uri.
     * \param[in] testUri The uri to test against refUri.
     * \return True if testUri starts with refUri.
     */
    static bool matchUri(const std::string &refUri, const std::string &testUri);

    virtual ~Plugin();

    /// Acquire shared lock on plugin.
    void lock();

    /// Release shared lock on plugin.
    void unlock();

    /**
     * \brief Enable/disable notifications for device
     * appear/disappear.
     *
     * The pointer to the observer is not protected with a
     * std::shared_ptr as it is assumed that the observer object lives
     * longer than the plugin - or disables notification before being
     * destroyed.
     *
     * \param[in] observer The observer object.
     * \param[in] on If observation shall be turned on or off.
     */
    virtual void setDeviceNotifications(IDeviceObserver *observer, bool on);

    /**
     * \brief Get Uri to list of supported Uris.
     *
     * \return The uri the plugin registered for.
     */
    virtual const std::string &uri() const;

    /**
     * \brief Check if device already exists, if not, register and
     * publish new device and mark as not available.
     *
     * \param[in] device The new device with meta set.
     * \return True if new device has been added, else false.
     */
    virtual bool injectDevice(std::shared_ptr<Device> device);

    /**
     * \brief Check if device already exists, if not, register and
     * publish new device and mark as not available.
     *
     * \param[in] uri    The device uri.
     * \param[in] alive  Optional alive counter to be set on device.
     * \param[in] avail  Set initial available state.
     * \param[in] uuid   The device uuid.
     * \return True if new device has been added, else false.
     */
    virtual bool injectDevice(const std::string &uri, int alive, bool avail = true, std::string uuid = "");

    /**
     * \brief Check if device already exists, if not, register and
     * publish new device.
     *
     * \param[in] uri The device uri.
     * \param[in] alive Optional alive counter to be set on device.
     * \return True if device has been added, else false.
     */
    virtual bool addDevice(const std::string &uri, int alive = -1);

    /**
     * \brief Check if device already exists, if not, register and
     * publish new device.
     *
     * \param[in] uri The device uri.
     * \param[in] mp The device mountpoint.
     * \param[in] uuid The device uuid.
     * \param[in] alive Optional alive counter to be set on device.
     * \return True if device has been added, else false.
     */
    virtual bool addDevice(const std::string &uri, const std::string &mp, std::string uuid = "", int alive = -1);

    /**
     * \brief A device has disappeared.
     *
     * \param[in] uri The device uri.
     * \return True if device has been marked 'not available', else false.
     */
    virtual bool removeDevice(const std::string &uri);

    /**
     * \brief Mark all devices as not available.
     */
    virtual void removeAll();

    /**
     * \brief Change device meta data.
     *
     * \param[in] uri The device uri.
     * \param[in] type The meta data type.
     * \param[in] value The meta data value.
     */
    virtual void modifyDevice(const std::string &uri, Device::Meta type,
        const std::string &value);

    /**
     * \brief Check if device is already known to plugin.
     *
     * \param[in] uri The device uri.
     * \return True if device is already there and available, else false.
     */
    virtual bool hasDevice(const std::string &uri) const;

    /**
     * \brief Get a device from the uri.
     *
     * \param[in] uri The device uri.
     * \return Returns the device or a nullptr.
     */
    virtual std::shared_ptr<Device> device(const std::string &uri) const;

    /**
     * \brief Check all devices if still available.
     *
     * This will consider the alive counter state and mark all devices
     * where the alive counter expired as not available.
     */
    virtual void checkDevices(void);

    /**
     * \brief Get device map.
     *
     * The caller must make sure that the device map does not change
     * while in use, you can use this method safely when calling
     * within the observer notifier or before the plugin becomes active.
     *
     * \return A reference to the device list.
     */
    const std::map<std::string, std::shared_ptr<Device>> &devices() const;

    /**
     * \brief Check if device detection is enabled.
     *
     * \return True if observers are attached, else false.
     */
    bool active(void) const;

    /**
     * \brief Scan device for media.
     *
     * \param[in] uri The device uri.
     */
    virtual void scan(const std::string &uri);

    /**
     * \brief Does the meta data extraction.
     *
     * \param[in] mediaItem The media item.
     */
    virtual void extractMeta(MediaItem &mediaItem, bool expand = false);

    /**
     * \brief Build playback uri from media item uri.
     *
     * The default implementation for file-tree-walk create the media
     * item uri by concatenating the device uri with the file path so
     * we do the reverse process here and prepend the remaining path
     * with 'file://'.
     *
     * \param[in] uri Media item uri.
     * \return Playback item uri if possible.
     */
    virtual std::optional<std::string> getPlaybackUri(const std::string &uri);

protected:
    /**
     * \brief Create plugin by uri.
     *
     * \param[in] uri Base uri of plugin.
     */
    Plugin(const std::string &uri);

    /**
     * \brief Start/stop device detection.
     *
     * \param[in] start If detection shall be started or stopped.
     * \return 0 on success, -1 on error.
     */
    virtual int runDeviceDetection(bool start) = 0;

private:
    /// Plugin must be constructed with uri.
    Plugin() {};

    /// Like device() but does not acquire the lock.
    std::shared_ptr<Device> deviceUnlocked(const std::string &uri) const;

    /// Adds observer and returns true if this was the first one, you
    /// can use the method with nullptr to check if there is only a
    /// single oberserver registered.
    bool addObserver(IDeviceObserver *observer);

    /// Removes observer.
    void removeObserver(IDeviceObserver *observer);

    /// Notify the observer about a device state change.
    void notifyObserversStateChange(const std::shared_ptr<Device> &device,
        IDeviceObserver *observer = nullptr) const;

    /// Notify the observer about a device modification.
    void notifyObserversModify(const std::shared_ptr<Device> &device) const;

    /// Check if the path is a hidden file.
    bool isHiddenfolder(std::string &filepath);


    //TODO: need refactoring!
    bool doFileTreeWalk(const std::shared_ptr<Device>& device,
                        IMediaItemObserver* observer,
                        const std::string& mountPoint);

    //TODO: need refactoring!
    bool doFileTreeWalkWithCache(const std::shared_ptr<Device>& device,
                                 IMediaItemObserver* observer,
                                 const std::string& mountPoint);

    /// Lock this instance, this is locked when the observer is
    /// notified and the observer may call back into one of the
    /// protected methods so better make it recursive.
    mutable std::shared_mutex lock_;

    /// Uris support by this plugin.
    std::string uri_;
    /// Map of devices to their uri detected by this plugin.
    std::map<std::string, std::shared_ptr<Device>> devices_;
    /// List of device notification observers.
    std::list<IDeviceObserver *> deviceObservers_;
};
