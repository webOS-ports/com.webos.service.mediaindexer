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

#include "imetadataextractor.h"
#include "gstreamerextractor.h"
#include "taglibextractor.h"
#include "imageextractor.h"
#include "logging.h"

#include <cinttypes>
#include <fstream>
#include <iostream>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


std::shared_ptr<IMetaDataExtractor> IMetaDataExtractor::extractor(const MediaItem::ExtractorType& type) {
    std::shared_ptr<IMetaDataExtractor> extractor = nullptr;
    switch(type) {
        case MediaItem::ExtractorType::TagLibExtractor:
            extractor = std::make_shared<TaglibExtractor>();
            break;
        case MediaItem::ExtractorType::GStreamerExtractor:
            extractor = std::make_shared<GStreamerExtractor>();
            break;
        case MediaItem::ExtractorType::ImageExtractor:
            extractor = std::make_shared<ImageExtractor>();
            //extractor = std::make_shared<GStreamerExtractor>();
            break;
        default:
            LOG_ERROR(MEDIA_INDEXER_IMETADATAEXTRACTOR, 0, "Invalid extractor type : %d", type);
            break;
    }
    return extractor;
}

/// Get base filename from mediaItem
std::string IMetaDataExtractor::baseFilename(MediaItem &mediaItem, bool noExt, std::string delimeter) const
{
    std::string ret = "";
    std::string path = mediaItem.path();
    if (path.empty())
        return ret;
    ret = path.substr(path.find_last_of(delimeter) + 1);

    if (noExt)
    {
        std::string::size_type const p(ret.find_last_of('.'));
        std::string ret = ret.substr(0, p);
    }
    return ret;
}

/// Get random filename for attached image
std::string IMetaDataExtractor::randFilename() const
{
    size_t size = TAGLIB_FILE_NAME_SIZE-1;
    uint64_t val = 0;
    std::ifstream fs("/dev/urandom", std::ios::in|std::ios::binary);
    if (fs)
    {
        fs.read(reinterpret_cast<char *>(&val), sizeof(val));
    }
    fs.close();
    return std::to_string(val).substr(0, size);
}

/// Get extension from mediaItem
std::string IMetaDataExtractor::extension(MediaItem &mediaItem) const
{
    std::string ret = "";
    std::string path = mediaItem.path();
    if (path.empty())
        return ret;
    ret = path.substr(path.find_last_of('.') + 1);
    return ret;
}

std::string IMetaDataExtractor::lastModifiedDate(MediaItem &mediaItem, bool localTime) const
{
    std::string path = mediaItem.path();
    if (path.empty())
    {
        LOG_ERROR(MEDIA_INDEXER_IMETADATAEXTRACTOR, 0, "Invalid media item path");
        return "";
    }

    struct stat fStatus;
    if (stat(path.c_str(), &fStatus) < 0) {
        LOG_ERROR(MEDIA_INDEXER_IMETADATAEXTRACTOR, 0, "stat error, caused by : %s", strerror(errno));
        return "";
    }
    std::stringstream ss;
    std::time_t timeFormatted = fStatus.st_mtime;
    if (localTime)
    {
        ss << std::put_time(std::localtime(&timeFormatted), "%c %Z");
        LOG_DEBUG(MEDIA_INDEXER_IMETADATAEXTRACTOR, "Return time with formatted value %s", ss.str().c_str());
        return ss.str();
    }
    ss << std::put_time(std::gmtime(&timeFormatted), "%c %Z");
    LOG_DEBUG(MEDIA_INDEXER_IMETADATAEXTRACTOR, "Return time with formatted value %s", ss.str().c_str());
    return ss.str();
}

void IMetaDataExtractor::setMetaCommon(MediaItem &mediaItem) const
{
    std::string modified = lastModifiedDate(mediaItem, false);
    std::int64_t filesize = mediaItem.fileSize();
    mediaItem.setMeta(MediaItem::Meta::LastModifiedDate, modified);
    mediaItem.setMeta(MediaItem::Meta::FileSize, filesize);
}
