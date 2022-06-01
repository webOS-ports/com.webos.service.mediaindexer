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

#include "dbconnector.h"
#include "ideviceobserver.h"

#include <memory>

/// Connector to com.webos.mediadb.
class DeviceDb : public DbConnector, public IDeviceObserver
{
public:
    /**
     * \brief Get device database connector.
     *
     * \return Singleton object.
     */
    static DeviceDb *instance();

    virtual ~DeviceDb();

    /**
     * \brief Request all devices for the given uri and inject them
     * into the plugin.
     *
     * \param[in] uri The uri to match.
     * \return List of devices.
     */
    void injectKnownDevices(const std::string &uri);

    /**
     * \brief Db service response handler.
     *
     * \return Result of message processing.
     */
    virtual bool handleLunaResponse(LSMessage *msg);


    /**
     * \brief Db service response handler for meta data.
     *
     * \return Result of message processing.
     */
    virtual bool handleLunaResponseMetaData(LSMessage *msg);

protected:
    /// Singleton.
    DeviceDb();

    /// DeviceObserver interface.
    void deviceStateChanged(std::shared_ptr<Device> device);

    /// DeviceObserver interface.
    void deviceModified(std::shared_ptr<Device> device);

private:
    /// Singleton object.
    static std::unique_ptr<DeviceDb> instance_;
    /**
     * \brief Update or create the device in the database.
     *
     * \param[in] device Shared pointer for device.
     */
    void updateDevice(std::shared_ptr<Device> device);
};
