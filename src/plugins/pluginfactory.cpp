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

#include "pluginfactory.h"
#include "plugin.h"

#if defined HAS_PLUGIN_MTP
#include "plugins/mtp.h"
#endif

#if defined HAS_PLUGIN_UPNP
#include "plugins/upnp.h"
#endif

#if defined HAS_PLUGIN_MSC
#include "plugins/msc.h"
#endif

#if defined HAS_PLUGIN_STORAGE
#include "plugins/storage.h"
#endif

PluginFactory::PluginFactory()
{
#if defined HAS_PLUGIN_MTP
    pluginUris_.push_back(Mtp::uri);
#endif

#if defined HAS_PLUGIN_UPNP
    pluginUris_.push_back(Upnp::uri);
#endif

#if defined HAS_PLUGIN_MSC
    pluginUris_.push_back(Usb::uri);
#endif

#if defined HAS_PLUGIN_STORAGE
    pluginUris_.push_back(Storage::uri);
#endif
}

std::shared_ptr<Plugin> PluginFactory::plugin(const std::string &uri) const
{
    std::shared_ptr<Plugin> plg = nullptr;

#if defined HAS_PLUGIN_MTP
    if (Plugin::matchUri(Mtp::uri, uri))
        plg = Mtp::instance();
#endif

#if defined HAS_PLUGIN_UPNP
    if (Plugin::matchUri(Upnp::uri, uri))
        plg = Upnp::instance();
#endif

#if defined HAS_PLUGIN_MSC
    if (Plugin::matchUri(Usb::uri, uri))
        plg = Usb::instance();
#endif

#if defined HAS_PLUGIN_STORAGE
    if (Plugin::matchUri(Storage::uri, uri))
        plg = Storage::instance();
#endif

    LOG_INFO(MEDIA_INDEXER_PLUGINFACTORY, 0, "%s found for uri: '%s'", !!plg ? "Plugin" : "No plugin",
        uri.c_str());

    return plg;
}

const std::list<std::string> &PluginFactory::plugins() const
{
    return pluginUris_;
}
