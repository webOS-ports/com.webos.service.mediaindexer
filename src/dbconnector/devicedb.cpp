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
    LOG_INFO(MEDIA_INDEXER_DEVICEDB, 0, "Search for already known devices in database");
    find(uri, false, nullptr, "", true);
}

bool DeviceDb::handleLunaResponse(LSMessage *msg)
{
    struct SessionData sd;
    if (!sessionDataFromToken(LSMessageGetResponseToken(msg), &sd, HDL_LUNA_CONN))
        return false;

    auto dbServiceMethod = sd.dbServiceMethod;
    LOG_INFO(MEDIA_INDEXER_DEVICEDB, 0, "Received response com.webos.mediadb for: '%s'", dbServiceMethod.c_str());

    if (dbServiceMethod != std::string("find"))
        return true;

    // we do not need to check, the service implementation should do that
    pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());
    const char *payload = LSMessageGetPayload(msg);
    LOG_DEBUG(MEDIA_INDEXER_DEVICEDB, "payload : %s", payload);

    if (!parser.parse(payload)) {
        LOG_ERROR(MEDIA_INDEXER_DEVICEDB, 0, "Invalid JSON message: %s", payload);
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
        auto uuid = match["uuid"].asString();
        PluginFactory fac;
        auto plg = fac.plugin(uri);
        if (!plg)
            return true;
        int alive;
        match["alive"].asNumber(alive);

        LOG_INFO(MEDIA_INDEXER_DEVICEDB, 0, "Device '%s', uuid '%s' will be injected into plugin", uri.c_str(),uuid.c_str());

        if (plg->injectDevice(uri, alive, false, uuid)) {
            auto meta = match["name"].asString();
            plg->device(uri)->setMeta(Device::Meta::Name, meta);
            meta = match["description"].asString();
            plg->device(uri)->setMeta(Device::Meta::Description, meta);
        }
    }

    return true;
}

bool DeviceDb::handleLunaResponseMetaData(LSMessage *msg)
{
    // nothing to de done here
    return true;
}

DeviceDb::DeviceDb() :
    DbConnector("com.webos.service.mediaindexer.devices", true)
{
    std::list<std::string> indexes = {"uri", "available"}; // add index : available
    for (auto idx : indexes) {
        auto index = pbnjson::Object();
        index.put("name", idx);

        auto props = pbnjson::Array();
        auto prop = pbnjson::Object();
        prop.put("name", idx);
        props << prop;

        index.put("props", props);

        kindIndexes_ << index;
    }
}

void DeviceDb::deviceStateChanged(std::shared_ptr<Device> device)
{
    LOG_INFO(MEDIA_INDEXER_DEVICEDB, 0, "Device '%s' has been %s", device->uri().c_str(),
        device->available() ? "added" : "removed");

    // we only write updates if device appears
    //if (device->available())
    updateDevice(device);
}

void DeviceDb::deviceModified(std::shared_ptr<Device> device)
{
    LOG_INFO(MEDIA_INDEXER_DEVICEDB, 0, "Device '%s' has been modified", device->uri().c_str());
    updateDevice(device);
}

void DeviceDb::updateDevice(std::shared_ptr<Device> device)
{
    // update or create the device in the database
    auto props = pbnjson::Object();
    props.put("uri", device->uri());
    props.put("uuid", device->uuid());
    props.put("name", device->meta(Device::Meta::Name));
    props.put("description", device->meta(Device::Meta::Description));
    props.put("alive", device->alive());
    props.put("available", device->available()); //add device available status
    props.put("lastSeen", device->lastSeen().time_since_epoch().count());

    mergePut(device->uri(), true, props);
}
