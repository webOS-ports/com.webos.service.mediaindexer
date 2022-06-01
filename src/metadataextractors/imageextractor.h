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

#include "imetadataextractor.h"

#include <libexif/exif-data.h>
#include <gst/gst.h>
#include <gst/pbutils/gstdiscoverer.h>
#include <gst/pbutils/pbutils.h>
#include <jpeglib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <png.h>
#include <gif_lib.h>

#include <vector>
#include <map>
#include <string>
#include <functional>
#include <csetjmp>

typedef struct  {
        ExifIfd ifd;
        std::vector<ExifTag> tag;
} ExifMapStructure;

/**
 * \brief Media parser class for meta data extraction.
 *
 * This class extracts meta data using the Taglib library.
 */
class ImageExtractor : public IMetaDataExtractor
{
 public:
    ImageExtractor();
    virtual ~ImageExtractor();

    /// From interface.
    bool extractMeta(MediaItem &mediaItem, bool extra = false) const;

 private:
    bool getExifData(MediaItem &mediaItem) const;

    void resetExifData() const;

    void setMeta(MediaItem &mediaItem, bool extra) const;

    bool setDefaultMeta(MediaItem &mediaItem, bool extra) const;

    void setMetaFromExif(MediaItem &mediaItem, bool extra) const;

    void setMetaFromExifMap(MediaItem &mediaItem, bool extra) const;

    static std::map<std::string, std::function<bool(MediaItem &, void *)>> resolutionHadler_;
    static std::map<MediaItem::Meta, ExifMapStructure> exifMap_;
    static std::vector<MediaItem::Meta> basicFlag_;
    static std::vector<MediaItem::Meta> extraFlag_;
    mutable ExifData* exifData_ = nullptr;
};

