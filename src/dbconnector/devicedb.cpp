// Copyright (c) 2019 LG Electronics, Inc.
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

#include "devicedb.h"
#include "device.h"
#include "plugins/pluginfactory.h"
#include "plugins/plugin.h"

#include <cstdint>

std::unique_ptr<DeviceDb> DeviceDb::instance_;

DeviceDb *DeviceDb::instance()
{
    if (!instance_.get()) {
        instance_.reset(new DeviceDb);
        instance_->ensureKind();
    }
    return instance_.get();
}

DeviceDb::~DeviceDb()
{
    // nothing to be done here
}

void DeviceDb::injectKnownDevices(const std::string &uri)
{
    // request all devices from the database that start with the given
    // uri
    LOG_INFO(0, "Search for already known devices in database");
    find(uri, false);
}

bool DeviceDb::handleLunaResponse(LSMessage *msg)
{
    struct SessionData sd;
    if (!sessionDataFromToken(LSMessageGetResponseToken(msg), &sd))
        return false;

    auto method = sd.method;
    LOG_INFO(0, "Received response com.webos.service.db for: '%s'",
        method.c_str());

    if (method != std::string("find"))
        return true;

    // we do not need to check, the service implementation should do that
    pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());
    const char *payload = LSMessageGetPayload(msg);

    if (!parser.parse(payload)) {
        LOG_ERROR(0, "Invalid JSON message: %s", payload);
        return false;
    }

    pbnjson::JValue domTree(parser.getDom());

    if (!domTree.hasKey("results"))
        return false;

    auto matches = domTree["results"];

    // sanity check
    if (!matches.isArray())
        return true;

    for (ssize_t i = 0; i < matches.arraySize(); ++i) {
        auto match = matches[i];

        auto uri = match["uri"].asString();
        PluginFactory fac;
        auto plg = fac.plugin(uri);
        if (!plg)
            return true;
        int alive;
        match["alive"].asNumber(alive);

        auto device = std::make_shared<Device>(uri, alive, false);
        auto meta = match["name"].asString();
        device->setMeta(Device::Meta::Name, meta);
        meta = match["description"].asString();
        device->setMeta(Device::Meta::Description, meta);

        LOG_INFO(0, "Device '%s' will be injected into plugin", uri.c_str());

        plg->injectDevice(device);
    }

    return true;
}

DeviceDb::DeviceDb() :
    DbConnector("com.webos.service.mediaindexer.devices:1")
{
    auto index = pbnjson::Object();
    index.put("name", "uri");

    auto props = pbnjson::Array();
    auto prop = pbnjson::Object();
    prop.put("name", "uri");
    props << prop;

    index.put("props", props);

    kindIndexes_ << index;
}

void DeviceDb::deviceStateChanged(std::shared_ptr<Device> device)
{
    LOG_INFO(0, "Device '%s' has been %s", device->uri().c_str(),
        device->available() ? "added" : "removed");

    // we only write updates if device appears
    if (device->available())
        updateDevice(device);
}

void DeviceDb::deviceModified(std::shared_ptr<Device> device)
{
    LOG_INFO(0, "Device '%s' has been modified", device->uri().c_str());
    updateDevice(device);
}

void DeviceDb::updateDevice(std::shared_ptr<Device> device)
{
    // update or create the device in the database
    auto props = pbnjson::Object();
    props.put("uri", device->uri());
    props.put("name", device->meta(Device::Meta::Name));
    props.put("description", device->meta(Device::Meta::Description));
    props.put("alive", device->alive());
    props.put("lastSeen", device->lastSeen().time_since_epoch().count());

    mergePut(device->uri(), true, props);
}