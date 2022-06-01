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

#include "plugin.h"
#include "logging.h"

/// STORAGE plugin class definition..
class Storage : public Plugin
{
public:
    /// Base uri of this plugin.
    static std::string uri;

    /**
     * \brief Get STORAGE plugin singleton instance.
     *
     * \return Singleton object in std::shared_ptr.
     */
    static std::shared_ptr<Plugin> instance();

    /// We cannot make this private because it is used from std::make_shared()
    Storage();
    virtual ~Storage();

private:
    /// Storage device definition structure.
    struct StorageDevice {
        /// Local path to storage.
        std::string path;
        /// Name of storage device.
        std::string name;
        /// Description for storage device.
        std::string desc;
    };

    /// Singleton object.
    static std::shared_ptr<Plugin> instance_;

    /// From plugin base class.
    int runDeviceDetection(bool start);

    /// List of local storage paths to observe.
    std::list<StorageDevice> devs_;
};
