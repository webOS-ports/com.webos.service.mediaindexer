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

#include "storage.h"

#include <cstdlib>
#include <string>
#include <sstream>
#include <filesystem>

std::string Storage::uri("storage");
std::shared_ptr<Plugin> Storage::instance_ = nullptr;

std::shared_ptr<Plugin> Storage::instance()
{
    if (!Storage::instance_)
        Storage::instance_ = std::make_shared<Storage>();
    return Storage::instance_;
}

Storage::Storage() : Plugin(Storage::uri)
{
    // extract all specified storage paths and register the devices
    std::istringstream devs(STORAGE_DEVS);
    if (std::getenv("STORAGE_DEVS"))
        devs.str(std::getenv("STORAGE_DEVS"));
    std::string dev;
    while (std::getline(devs, dev, ';')) {
        // no error checks here, this is set from the build
        // environment and simply needs to be done carefully, no
        // runtime checks required then
        std::istringstream details(dev);
        struct StorageDevice storageDev;
        std::getline(details, storageDev.path, ',');
        std::getline(details, storageDev.name, ',');
        std::getline(details, storageDev.desc, ',');
        std::error_code err;
        if (!std::filesystem::is_directory(storageDev.path))
        {
            if (!std::filesystem::create_directory(storageDev.path, err))
            {
                LOG_WARNING(MEDIA_INDEXER_STORAGE, 0, "Failed to create directory %s, error : %s",
                    storageDev.path.c_str(), err.message().c_str());
                LOG_DEBUG(MEDIA_INDEXER_STORAGE, "Retry with create_directories");
                if (!std::filesystem::create_directories(storageDev.path, err)) {
                    LOG_ERROR(MEDIA_INDEXER_STORAGE, 0, "Retry Failed, error : %s", err.message().c_str());
                }
            }
        }
        devs_.push_back(storageDev);
    }
}

Storage::~Storage()
{
}

int Storage::runDeviceDetection(bool start)
{
    LOG_DEBUG(MEDIA_INDEXER_STORAGE, "%s all configured paths", start ? "Set" : "Unset");

    for (auto const & dev : devs_) {
        std::stringstream suri;
        suri << Storage::uri << "://" << // Uri identifier.
            dev.path; // Path on local file system.
        auto uri = suri.str();

        if (start) {
            addDevice(uri, dev.path);
            modifyDevice(uri, Device::Meta::Name, dev.name);
            modifyDevice(uri, Device::Meta::Description, dev.desc);
        } else {
            removeDevice(uri);
        }
    }
    return 0;
}
