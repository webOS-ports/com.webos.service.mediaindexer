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

#include "mediaitem.h"
#include "device.h"
#include "jsonparser/jsonparser.h"

#include <memory>

#define EXT_JPG "jpg"
#define EXT_JPEG "jpeg"
#define EXT_PNG "png"
#define EXT_MP3 "mp3"
#define EXT_OGG "ogg"


/// Interface definition for device observers.
class IMetaDataExtractor
{
public:
    /**
     * \brief Construct an extractor.
     *
     * Be aware that the MediaParser constructs only one extrator for
     * all media items and that they are extracted from concurrent
     * threads so be careful with member variables.
     */
    static std::shared_ptr<IMetaDataExtractor> extractor(const MediaItem::ExtractorType& type);
    virtual ~IMetaDataExtractor() {};

    /**
     * \brief Extract meta data into media item.
     *
     * \param[in] mediaItem The media item.
     */
    virtual bool extractMeta(MediaItem &mediaItem, bool extra = false) const = 0;


    /// Get base filename from mediaItem
    virtual std::string baseFilename(MediaItem &mediaItem, bool noExt = false, std::string delimeter = "//") const;

    /// Get random filename for attached image
    virtual std::string randFilename() const;

    /// Get extension from mediaItem
    virtual std::string extension(MediaItem &mediaItem) const;

    /// lastmodified date type, support both uint64_t and string type
    typedef std::variant<std::int64_t, std::string> Date;

    /**
     * \brief Gives us the last modified date value with formatted string value.
     * \param[in] mediaItem mediaitem.
     * \param[in] localtime specifies time zone of returns, default : false
     */
    virtual std::string lastModifiedDate(MediaItem &mediaItem, bool localTime = false) const;

    void setMetaCommon(MediaItem &mediaItem) const;

protected:
    IMetaDataExtractor() {};
};
