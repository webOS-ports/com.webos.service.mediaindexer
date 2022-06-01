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

#include "pdmlistener.h"
#include "ipdmobserver.h"

#include <algorithm>

const char *PdmListener::pdmUrl_ = "luna://com.webos.service.pdm/getAttachedStorageDeviceList";
LSHandle *PdmListener::lsHandle_ = nullptr;
std::unique_ptr<PdmListener> PdmListener::instance_;

void PdmListener::init(LSHandle * lsHandle)
{
    lsHandle_ = lsHandle;
}

PdmListener *PdmListener::instance()
{
    if (!lsHandle_) {
        LOG_CRITICAL(MEDIA_INDEXER_PDMLISTENER, 0, "Luna bus handle not set");
        return nullptr;
    }

    if (!instance_.get())
        instance_.reset(new PdmListener);
    return instance_.get();
}

PdmListener::PdmListener() :
    deviceObservers_(),
    deviceMap_()
{
}

PdmListener::~PdmListener()
{
    // now delete the pdmDevice objects
    for (auto &devmap : deviceMap_) {
        if (devmap.second)
            delete devmap.second;
    }
    deviceMap_.clear();
}

void PdmListener::setDeviceNotifications(IPdmObserver *observer,
    PdmDevice::DeviceType type, bool on)
{
    if (on) {
        // if we do not have any observers let's start subscription
        // now that we add one
        if (!deviceObservers_.empty()) {
            // Check if observer is already registered.
            auto it = std::find_if(deviceObservers_[type].begin(),
                deviceObservers_[type].end(),
                [observer](IPdmObserver *obs) { return (obs == observer); });

            if (it != deviceObservers_[type].end())
                return;
        } else {
            subscribe();
        }

        LOG_DEBUG(MEDIA_INDEXER_PDMLISTENER, "Add observer %p", observer);

        // Remember the observer for notifications.
        deviceObservers_[type].push_back(observer);

        // push already known devices to new observer
        for (auto const mountName : deviceMapByType_[type]) {
            auto dev = deviceMap_[mountName];
            observer->pdmUpdate(dev->dev(), true);
        }

    } else if (!deviceObservers_[type].empty()) {
        LOG_DEBUG(MEDIA_INDEXER_PDMLISTENER, "Remove observer %p", observer);
        deviceObservers_[type].remove(observer);
    }
}

void PdmListener::subscribe()
{
    auto subscription = pbnjson::Object();

    subscription.put("subscribe", true);
    LOG_INFO(MEDIA_INDEXER_PDMLISTENER, 0, "Subscribed for com.webos.service.pdm/getAttachedStorageDeviceList");

    LSError lsError;
    LSErrorInit(&lsError);
    LSMessageToken sessionToken;

    if (!LSCall(lsHandle_, pdmUrl_, subscription.stringify().c_str(),
            PdmListener::onDeviceNotification, this, &sessionToken,
            &lsError)) {
        LOG_ERROR(MEDIA_INDEXER_PDMLISTENER, 0, "PDM service subscription error");
        return;
    }
}

void PdmListener::checkDevice(std::string &mountName, pbnjson::JValue &dev)
{
    auto match = deviceMap_.find(mountName);
    if (match != deviceMap_.end()) {
        deviceMap_[mountName]->markDirty(false);
    } else {
        PdmDevice *pdmDev = new PdmDevice(mountName, dev);

        // do we support this type of device at all?
        if (pdmDev->type() == PdmDevice::DeviceType::UNSUPPORTED) {
            delete pdmDev;
            return;
        }

        // append newly detected device to list of available devices
        deviceMap_[mountName] = pdmDev;

        // map this device to the device type
        deviceMapByType_[pdmDev->type()].push_back(mountName);

        // tell all observers for this device type of the new device
        for (auto const obs : deviceObservers_[pdmDev->type()])
            obs->pdmUpdate(pdmDev->dev(), true);
    }
}

void PdmListener::cleanupDevices()
{
    for (auto &devmap : deviceMap_) {
        PdmDevice *dev = devmap.second;
        if (dev) {
            if (!dev->dirty())
                continue;

            // tell all observers for this device type of the removed
            // device
            for (auto const obs : deviceObservers_[dev->type()])
                obs->pdmUpdate(dev->dev(), false);

            // remove all references from the deviceMapByType_
            auto deviceType = dev->type();
            deviceMapByType_[deviceType].remove(dev->mountName());

            // remove device from list of devices
            delete dev;
        }
        deviceMap_.erase(devmap.first);
    }
}

bool PdmListener::onDeviceNotification(LSHandle *lsHandle, LSMessage *msg,
    void *ctx)
{
    PdmListener *pl = static_cast<PdmListener *>(ctx);

    const char *payload = LSMessageGetPayload(msg);
    // we do not need to check, the service implementation should do that
    pbnjson::JDomParser parser(pbnjson::JSchema::AllSchema());

    if (!parser.parse(payload)) {
        LOG_ERROR(MEDIA_INDEXER_PDMLISTENER, 0, "Invalid JSON message: %s", payload);
        return false;
    }

    LOG_INFO(MEDIA_INDEXER_PDMLISTENER, 0, "Pdm attached storage device update received: %s", payload);

    pbnjson::JValue domTree(parser.getDom());

    // check if all devices are gone
    if (!domTree.hasKey("storageDeviceList")) {
        pl->deviceMap_.clear();
        pl->deviceMapByType_.clear();
        return true;
    }

    auto storageDeviceList = domTree["storageDeviceList"];

    // sanity check
    if (!storageDeviceList.isArray())
        return true;

    // our reference list is deviceMap_, mark all devices dirty and
    // then check if they really are
    for (auto const &[uri, dev] : pl->deviceMap_)
        dev->markDirty(true);

    // now iterate through the devices to find those where observers
    // are attached for
    for (ssize_t i = 0; i < storageDeviceList.arraySize(); ++i) {
        if (!storageDeviceList[i].hasKey("storageDriveList")) {
            LOG_DEBUG(MEDIA_INDEXER_PDMLISTENER, "storageDriveList is not valid format");
            continue;
        }

        auto storageDriveList = storageDeviceList[i]["storageDriveList"];

        // storageDriveList always has only one drive.
        auto drive = storageDriveList[0];
        if (!drive.hasKey("mountName")) {
            LOG_ERROR(MEDIA_INDEXER_PDMLISTENER, 0, "mountName field is missing!");
            continue;
        }

        auto mountName = drive["mountName"].asString();
        if (mountName.empty()) {
            LOG_ERROR(MEDIA_INDEXER_PDMLISTENER, 0, "mountName is NULL!");
            continue;
        }

        auto dev = storageDeviceList[i];
        pl->checkDevice(mountName, dev);
    }

    pl->cleanupDevices();
    
    return true;
}
