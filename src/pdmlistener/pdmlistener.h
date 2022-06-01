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

#include "pdmdevice.h"
#include "logging.h"

#include <luna-service2/lunaservice.h>
#include <pbnjson.hpp>

#include <string>
#include <list>
#include <map>

class IPdmObserver;

/// Listener for com.webos.service.pdm.
class PdmListener
{
public:
    /**
     * \brief Configure luna service handle.
     *
     * This should be called before any object tries to request device
     * state change notifications.
     *
     * \param[in] lsHandle Luna service handle to use.
     */
    static void init(LSHandle *lsHandle);

    /**
     * \brief Get singleton object of PdmListener.
     *
     * \return The singleton.
     */
    static PdmListener *instance();

    virtual ~PdmListener();

    /**
     * \brief Register for notification from certain storage device
     * type
     *
     * The pointer to the observer is not protected with a
     * std::shared_ptr as it is assumed that the observer object lives
     * longer than the pdm listener - or disables notification before
     * being destroy.
     *
     * \param[in] observer The observer object.
     * \param[in] type Device type to register for.
     * \param[in] on If observation shall be turned on or off.
     */
    void setDeviceNotifications(IPdmObserver *observer,
        PdmDevice::DeviceType type, bool on);

private:
    /// Singleton.
    PdmListener();

    /// Subscribe to com.webos.service.pdm for attached storage
    /// devices.
    void subscribe(void);

    /**
     * \brief Check if device is new and notify observers.
     *
     * \param[in] mountName Device mount name as unique identifier.
     * \param[in] dev Parsed DOM tree of device JSON.
     */
    void checkDevice(std::string &mountName, pbnjson::JValue &dev);

    /**
     * \brief Remove all devices that are dirty and notify observers.
     */
    void cleanupDevices(void);

    /// List of notification observers for USB storage device.
    std::map<PdmDevice::DeviceType, std::list<IPdmObserver *>> deviceObservers_;

    /// List of known devices where the key is the device mount path
    /// (unique).
    std::map<std::string, PdmDevice *> deviceMap_;

    /// List of root pathes for a specific device type.
    std::map<PdmDevice::DeviceType, std::list<std::string>> deviceMapByType_;

    /// Pdm service url.
    static const char *pdmUrl_;

    /// Singleton instance object.
    static std::unique_ptr<PdmListener> instance_;
    /// Luna service handle.
    static LSHandle *lsHandle_;
    /// Callback for luna device  notifications.
    static bool onDeviceNotification(LSHandle *lsHandle, LSMessage *msg,
        void *ctx);
};
