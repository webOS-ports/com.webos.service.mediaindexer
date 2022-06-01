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

#include "logging.h"

#include <memory>
#include <list>

class Plugin;

/// Create new plugins by Uri.
class PluginFactory
{
public:
    PluginFactory();
    virtual ~PluginFactory() {};

    /**
     * \brief Return plugin for given uri if one exists.
     *
     * \param[in] uri The plugin base uri.
     * \return A managed pointer to the plugin if one exists.
     */
    std::shared_ptr<Plugin> plugin(const std::string &uri) const;

    /**
     * \brief Return list of available plugins.
     *
     * \return A list of plugin Uris.
     */
    const std::list<std::string> &plugins() const;

private:
    /// List of available plugin uris.
    std::list<std::string> pluginUris_;
};
