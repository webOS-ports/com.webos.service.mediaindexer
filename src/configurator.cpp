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

#include "configurator.h"

std::unique_ptr<Configurator> Configurator::instance_;

Configurator *Configurator::instance()
{
    if (!instance_.get())
        instance_.reset(new Configurator(JSON_CONFIGURATION_FILE));
    return instance_.get();
}

Configurator::Configurator(std::string confPath)
    : confPath_(confPath)
    , force_sw_decoders_(false)
{
    init();
}

Configurator::~Configurator()
{
    extensions_.clear();
}

void Configurator::init()
{
    // get the JDOM tree from configuration file
    pbnjson::JValue root = pbnjson::JDomParser::fromFile(confPath_.c_str());
    if (!root.isObject()) {
        LOG_ERROR(0, "configuration file parsing error! need to check %s", confPath_.c_str());
        return;
    }

    // check force-sw-decoders field
    if (!root.hasKey("force-sw-decoders"))
        LOG_WARNING(0, "Can't find force-sw-decoders property. use H/W decoder instead by default!");
    else
        force_sw_decoders_ = root["force-sw-decoders"].asBool();

    // check supportedMediaExtension field
    if (!root.hasKey("supportedMediaExtension")) {
        LOG_WARNING(0, "Can't find supportedMediaExtension field. need to check it!");
        return;
    }

    // get the extentions from supportedMediaExtension field
    pbnjson::JValue supportedExtensions = root["supportedMediaExtension"];

    // for audio extension
    if (supportedExtensions.hasKey("audio")) {
        pbnjson::JValue audioExtension = supportedExtensions["audio"];
        for (int idx = 0; idx < audioExtension.arraySize(); idx++) {
            auto ext = audioExtension[idx].asString();
            // check extension for setting extractor type.
            // mp3 and ogg extension uses taglib extractor others GStreamer use instead.
            if (ext.compare("mp3") == 0 || ext.compare("ogg") == 0)
                extensions_.insert(std::make_pair(ext,
                            std::make_pair(MediaItem::Type::Audio,
                                MediaItem::ExtractorType::TagLibExtractor)));
            else
                extensions_.insert(std::make_pair(ext,
                            std::make_pair(MediaItem::Type::Audio,
                                MediaItem::ExtractorType::GStreamerExtractor)));
        }
    }

    // for video extension
    if (supportedExtensions.hasKey("video")) {
        pbnjson::JValue videoExtension = supportedExtensions["video"];
        for (int idx = 0; idx < videoExtension.arraySize(); idx++)
            extensions_.insert(std::make_pair(videoExtension[idx].asString(),
                        std::make_pair(MediaItem::Type::Video,
                            MediaItem::ExtractorType::GStreamerExtractor)));
    }

    // for image extension
    if (supportedExtensions.hasKey("image")) {
        pbnjson::JValue imageExtension = supportedExtensions["image"];
        for (int idx = 0; idx < imageExtension.arraySize(); idx++)
            extensions_.insert(std::make_pair(imageExtension[idx].asString(),
                        std::make_pair(MediaItem::Type::Image,
                            MediaItem::ExtractorType::ImageExtractor)));
    }

    printSupportedExtension();
}

bool Configurator::isSupportedExtension(const std::string& ext) const
{
    //printSupportedExtension();
    auto ret = extensions_.find(ext);
    if (ret != extensions_.end())
        return true;
    else
        return false;
}

MediaItemTypeInfo Configurator::getTypeInfo(const std::string& ext) const
{
    auto ret = extensions_.find(ext);
    if (ret != extensions_.end()) {
        return ret->second;
    }
    else {
        return std::make_pair(MediaItem::Type::EOL, MediaItem::ExtractorType::EOL);
    }
}

ExtensionMap Configurator::getSupportedExtensions() const
{
    return extensions_;
}

bool Configurator::getForceSWDecodersProperty() const
{
    return force_sw_decoders_;
}

std::string Configurator::getConfigurationPath() const
{
    return confPath_;
}

bool Configurator::insertExtension(const std::string& ext, const MediaItem::Type& type,
        const MediaItem::ExtractorType& exType)
{
    auto ret = extensions_.insert(std::make_pair(ext, std::make_pair(type, exType)));
    return ret.second;
}

bool Configurator::removeExtension(const std::string& ext)
{
    extensions_.erase(ext);
    return true;
}

void Configurator::printSupportedExtension() const
{
    LOG_DEBUG("--------------Supported extensions--------------");
    for (const auto &ext : extensions_)
        LOG_DEBUG("%s", ext.first.c_str());
    LOG_DEBUG("------------------------------------------------");
}
