// Copyright (c) 2019 LG Electronics, Inc.
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

#include "mediaitem.h"
#include "device.h"
#include "plugins/pluginfactory.h"
#include "plugins/plugin.h"

#include <cinttypes>

// Not part of Device class, this is defined at the bottom of device.h
MediaItem::Type &operator++(MediaItem::Type &type)
{
    if (type == MediaItem::Type::EOL)
        return type;
    type = static_cast<MediaItem::Type>(static_cast<int>(type) + 1);
    return type;
}

// Not part of Device class, this is defined at the bottom of device.h
MediaItem::Meta &operator++(MediaItem::Meta &meta)
{
    if (meta == MediaItem::Meta::EOL)
        return meta;
    meta = static_cast<MediaItem::Meta>(static_cast<int>(meta) + 1);
    return meta;
}

bool MediaItem::mimeTypeSupported(const std::string &mime)
{
    for (auto type = MediaItem::Type::Audio;
         type < MediaItem::Type::EOL; ++type) {
        auto typeString = MediaItem::mediaTypeToString(type);
        if (!mime.compare(0, typeString.size(), typeString))
            return true;
    }

    LOG_DEBUG("MIME type '%s' not supported", mime.c_str());

    return false;
}

std::string MediaItem::mediaTypeToString(MediaItem::Type type)
{
    switch (type) {
    case MediaItem::Type::Audio:
        return std::string("audio");
    case MediaItem::Type::Video:
        return std::string("video");
    case MediaItem::Type::Image:
        return std::string("image");
    case MediaItem::Type::EOL:
        return "";
    }

    return "";
}

std::string MediaItem::metaToString(MediaItem::Meta meta)
{
    switch (meta) {
    case MediaItem::Meta::Title:
        return std::string("title");
    case MediaItem::Meta::Genre:
        return std::string("genre");
    case MediaItem::Meta::Album:
        return std::string("album");
    case MediaItem::Meta::Artist:
        return std::string("artist");
    case MediaItem::Meta::AlbumArtist:
        return std::string("album_artist");
    case MediaItem::Meta::Track:
        return std::string("track");
    case MediaItem::Meta::TotalTracks:
        return std::string("total_tracks");
    case MediaItem::Meta::DateOfCreation:
        return std::string("date_of_creation");
    case MediaItem::Meta::Duration:
        return std::string("duration");
    case MediaItem::Meta::GeoLocLongitude:
        return std::string("geo_location_longitude");
    case MediaItem::Meta::GeoLocLatitude:
        return std::string("geo_location_latitude");
    case MediaItem::Meta::GeoLocCountry:
        return std::string("geo_location_country");
    case MediaItem::Meta::GeoLocCity:
        return std::string("geo_location_city");
    case MediaItem::Meta::EOL:
        return "";
    }

    return "";
}

MediaItem::MediaItem(std::shared_ptr<Device> device, const std::string &path,
    const std::string &mime, unsigned long hash) :
    device_(device),
    type_(Type::EOL),
    hash_(hash),
    parsed_(false),
    uri_(""),
    mime_(mime),
    path_("")
{
    // create uri
    uri_ = device->uri();
    if (uri_.back() != '/' && path.front() != '/')
        uri_.append("/");
    uri_.append(path);

    path_ = path;

    // set the type
    for (auto type = MediaItem::Type::Audio;
         type < MediaItem::Type::EOL; ++type) {
        auto typeString = MediaItem::mediaTypeToString(type);
        if (!!mime_.compare(0, typeString.size(), typeString))
            continue;

        type_ = type;
        break;
    }

    if (type_ != Type::EOL)
        device_->incrementMediaItemCount(type_);
}

unsigned long MediaItem::hash() const
{
    return hash_;
}

const std::string &MediaItem::path() const
{
    return path_;
}

std::shared_ptr<Device> MediaItem::device() const
{
    return device_;
}

std::optional<MediaItem::MetaData> MediaItem::meta(Meta meta) const
{
    auto m = meta_.find(meta);
    if (m != meta_.end())
        return m->second;
    else
        return std::nullopt;
}

void MediaItem::setMeta(Meta meta, MetaData value)
{
    // if meta data is set the media item is supposed to be parsed
    parsed_ = true;

    switch (value.index()) {
    case 0:
        // valgrind complains about 'std::int64_t' here, for clean
        // valgrind output we need to set 'long'
        LOG_DEBUG("Setting '%s' on '%s' to '%" PRIu64 "'", metaToString(meta).c_str(),
            uri_.c_str(), std::get<std::int64_t>(value));
        break;
    case 1:
        LOG_DEBUG("Setting '%s' on '%s' to '%f'", metaToString(meta).c_str(),
            uri_.c_str(), std::get<double>(value));
        break;
    case 2:
        LOG_DEBUG("Setting '%s' on '%s' to '%s'", metaToString(meta).c_str(),
            uri_.c_str(), std::get<std::string>(value).c_str());
        break;
    }

    // make the arist the album artist of none has been set yet
    if (meta == MediaItem::Meta::Artist &&
        !this->meta(MediaItem::Meta::AlbumArtist))
        meta_.insert_or_assign(MediaItem::Meta::AlbumArtist, value);

    // save the meta data
    meta_.insert_or_assign(meta, value);
}

bool MediaItem::parsed() const
{
    return parsed_;
}

const std::string &MediaItem::uri() const
{
    return uri_;
}

const std::string &MediaItem::mime() const
{
    return mime_;
}

MediaItem::Type MediaItem::type() const
{
    return type_;
}

IMediaItemObserver *MediaItem::observer() const
{
    return device_->observer();
}