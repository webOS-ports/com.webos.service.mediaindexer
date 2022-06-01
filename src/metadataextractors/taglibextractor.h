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

#if defined HAS_TAGLIB
#include <tag.h>
#endif

#include <string.h>

#define TAGLIB_BASE_DIRECTORY THUMBNAIL_DIRECTORY
#define TAGLIB_FILE_NAME_SIZE 16
namespace TagLib { class File; }
namespace TagLib { class Tag; }
namespace TagLib { namespace ID3v2 { class Tag; } }
namespace TagLib { namespace MPEG { class File; } }
namespace TagLib { namespace MPEG { class Properties; } }
namespace TagLib { namespace Ogg { class XiphComment; } }
namespace TagLib { namespace Ogg { class File; } }
namespace TagLib { class ByteVector; }

/**
 * \brief Media parser class for meta data extraction.
 *
 * This class extracts meta data using the Taglib library.
 */
class TaglibExtractor : public IMetaDataExtractor
{
 public:
    TaglibExtractor();
    virtual ~TaglibExtractor();

    enum FileTypes {
        NotDefined = 0x0000,
        Mp3 = 0x0001,
        Ogg = 0x0002,
        AllTypes = 0xffff
    };

    /// From interface.
    bool extractMeta(MediaItem &mediaItem, bool extra = false) const;

 private:
    /// Get text frame from id3 tag key value
    std::string getTextFrame(TagLib::ID3v2::Tag *tag,      const TagLib::ByteVector &flag) const;

    /// Get attached image of mp3 from APIC key frame
    std::string saveAttachedImage(MediaItem &mediaItem, TagLib::ID3v2::Tag *tag, const std::string &fname) const;

    /// Set media item media per media type(for mp3 file format).
    void setMetaMp3(MediaItem &mediaItem, TagLib::ID3v2::Tag *tag, TagLib::MPEG::File *file,
        MediaItem::Meta flag) const;

    /// Set media item media per media type(for ogg file format).
    void setMetaOgg(MediaItem &mediaItem, TagLib::Ogg::XiphComment *tag, TagLib::Ogg::File *file,
        MediaItem::Meta flag) const;

    /// Extract meta data based on file information
    bool setMetaFromFile(MediaItem &mediaItem, TagLib::File *file, FileTypes types, bool extra) const;

    /// Extract meta data based on ID3 tag information
    bool setMetaFromTag(MediaItem &mediaItem, TagLib::Tag *tag, FileTypes types, bool extra) const;
};
