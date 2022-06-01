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

#include "mtp.h"
#if defined HAS_PDM
#include "pdmlistener/pdmlistener.h"
#endif

#if defined HAS_LIBMTP
#include "libmtp.h"
#endif

#include <string>
#include <sstream>
#include <chrono>
#include <algorithm>

std::string Mtp::uri("mtp");
std::shared_ptr<Plugin> Mtp::instance_ = nullptr;

std::shared_ptr<Plugin> Mtp::instance()
{
    if (!Mtp::instance_)
        Mtp::instance_ = std::make_shared<Mtp>();
    return Mtp::instance_;
}

Mtp::Mtp() : Plugin(Mtp::uri)
{
#if defined HAS_LIBMTP
    LIBMTP_Init();
#endif
}

Mtp::~Mtp()
{
    runDeviceDetection(false);
}

#if defined HAS_PDM
void Mtp::pdmUpdate(const pbnjson::JValue &dev, bool available)
{
    // sanity check
    if (!dev.hasKey("storageDriveList"))
        return;

    auto drives = dev["storageDriveList"];

    // sanity check, we expect exactly 1 drive
    if (drives.arraySize() != 1)
        return;

    auto drive = drives[0];

    // sanity check
    if (!drive.hasKey("mountName"))
        return;
    if (!dev.hasKey("serialNumber"))
        return;

    // remember mountpoint for device creation
    auto mp = drive["mountName"].asString();

    std::string sn = dev["serialNumber"].asString();
    std::stringstream suri;
    suri << Mtp::uri << "://" << sn;
    auto uri = suri.str();
    // make sure we have no whitespace characters in the uri
    std::replace_if(uri.begin(), uri.end(),
        [](char c) { return std::isspace(c); }, '-');

    if (available) {
        addDevice(uri, mp);

        // check for meta data, the name is the product name, the
        // description is the vendor name
        if (dev.hasKey("productName"))
            modifyDevice(uri, Device::Meta::Name,
                dev["productName"].asString());
        if (dev.hasKey("vendorName"))
            modifyDevice(uri, Device::Meta::Description,
                dev["vendorName"].asString());
    } else {
        removeDevice(uri);
    }
}
#else
std::atomic<bool> Mtp::polling_(false);
std::thread *Mtp::poller_ = nullptr;

void Mtp::pollWork(Plugin *plugin)
{
    using namespace std::chrono_literals;

    LOG_DEBUG(MEDIA_INDEXER_MTP, "MTP poll work started");

    while (Mtp::polling_.load()) {
        LIBMTP_raw_device_t *rawDevs = nullptr;
        int deviceCount;

        auto err = LIBMTP_Detect_Raw_Devices(&rawDevs, &deviceCount);
        switch(err) {
        case LIBMTP_ERROR_NO_DEVICE_ATTACHED:
            LOG_DEBUG(MEDIA_INDEXER_MTP, "No MTP device found");
            break;
        case LIBMTP_ERROR_NONE:
            for (auto i = 0; i < deviceCount; ++i) {
                auto mtpDev = LIBMTP_Open_Raw_Device(&rawDevs[i]);
                if (!mtpDev)
                    continue;
                LIBMTP_Clear_Errorstack(mtpDev);
                std::string sn(LIBMTP_Get_Serialnumber(mtpDev));
                if (sn.empty())
                    continue;
                std::stringstream suri;
                suri << Mtp::uri << "://" << sn;
                std::string uri = suri.str();
                // make sure we have no whitespace characters in the uri
                std::replace_if(uri.begin(), uri.end(),
                    [](char c) { return std::isspace(c); }, '-');
                LOG_DEBUG(MEDIA_INDEXER_MTP, "Device found, constructed uri: '%s'", uri.c_str());
                plugin->addDevice(uri, 2);
                char *name = LIBMTP_Get_Friendlyname(mtpDev);
                if (name) {
                    LOG_DEBUG(MEDIA_INDEXER_MTP, "Device friendly name found: '%s'", name);
                    plugin->modifyDevice(uri, Device::Meta::Name, name);
                    free(name);
                } else {
                    plugin->modifyDevice(uri, Device::Meta::Name, sn);
                }
                LIBMTP_Release_Device(mtpDev);
            }
            break;
        case LIBMTP_ERROR_CONNECTING:
            [[fallthrough]]
        case LIBMTP_ERROR_MEMORY_ALLOCATION:
            [[fallthrough]]
        case LIBMTP_ERROR_GENERAL:
            LOG_ERROR(MEDIA_INDEXER_MTP, 0, "LIBMTP_Detect_Raw_Devices() failed (%i)", err);
        default: // ignore events we do not need to care about
            break;
        }

        if (rawDevs)
            free(rawDevs);
        rawDevs = nullptr;

        // Check if one of the devices has not been refreshed.
        plugin->checkDevices();

        // Delay next poll cycle.
        std::this_thread::sleep_for(1s);
    }
}
#endif

#if defined HAS_LIBMTP
void Mtp::scan(const std::string &uri)
{
    // first get the device from the uri
    auto dev = device(uri);
    if (!dev)
        return;

    LOG_INFO(MEDIA_INDEXER_MTP, 0, "Start file-tree-walk on device '%s'", dev->uri().c_str());

    auto obs = dev->observer();

    // this is a hack, for local simulation mode just take the first
    // device
    auto mtpDev = LIBMTP_Get_First_Device();
    if (!mtpDev)
        return;
    LIBMTP_file_t *files = LIBMTP_Get_Filelisting_With_Callback(mtpDev,
        nullptr, nullptr);

    for (auto file = files; file; file = file->next) {
        auto hash = file->modificationdate;
        auto contentType = filetypeToMime(file->filetype);
        if (contentType.empty())
            return;
        MediaItemPtr mi(new MediaItem(dev, file->filename, contentType, hash));
        obs->newMediaItem(std::move(mi));
    }

    LIBMTP_Release_Device(mtpDev);

    LOG_INFO(MEDIA_INDEXER_MTP, 0, "File-tree-walk on device '%s' has been completed",
        dev->uri().c_str());
}

std::string Mtp::filetypeToMime(int filetype) const
{
    if (LIBMTP_FILETYPE_IS_IMAGE(filetype))
        return "image";
    if (LIBMTP_FILETYPE_IS_AUDIO(filetype))
        return "audio";
    if (LIBMTP_FILETYPE_IS_VIDEO(filetype))
        return "video";

    return "";
}
#endif

int Mtp::runDeviceDetection(bool start)
{
#if defined HAS_PDM
    auto listener = PdmListener::instance();
    listener->setDeviceNotifications(
        static_cast<IPdmObserver *>(this), PdmDevice::DeviceType::MTP, start);
#else
    if (start && !Mtp::poller_) {
        Mtp::polling_ = true;
        Mtp::poller_ = new std::thread(pollWork, this);
    } else if (Mtp::poller_) {
        Mtp::polling_ = false;
        Mtp::poller_->join();
        delete Mtp::poller_;
        Mtp::poller_ = nullptr;
    }
#endif

    return 0;
}
