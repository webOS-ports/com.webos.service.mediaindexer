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

#if defined HAS_PDM
#include "pdmlistener/ipdmobserver.h"
#include <pbnjson.hpp>
#endif

#include <thread>
#include <atomic>

/// USB plugin class definition..
#if defined HAS_PDM
class Usb : public Plugin, public IPdmObserver
#else
class Usb : public Plugin
#endif
{
public:
    /// Base uri of this plugin.
    static std::string uri;

    /**
     * \brief Get USB plugin singleton instance.
     *
     * \return Singleton object in std::shared_ptr.
     */
    static std::shared_ptr<Plugin> instance();

    /// We cannot make this private because it is used from std::make_shared()
    Usb();
    virtual ~Usb();

#if defined HAS_PDM
    void pdmUpdate(const pbnjson::JValue &dev, bool available);
#endif

private:
    /// Escape sequence of space in volume label.
    inline static const std::string space_ = "\\x20";

    /// Singleton object.
    static std::shared_ptr<Plugin> instance_;

    /// From plugin base class.
    int runDeviceDetection(bool start);
};
