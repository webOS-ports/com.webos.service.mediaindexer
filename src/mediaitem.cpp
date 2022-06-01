// Copyright (c) 2019-2021 LG Electronics, Inc.
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
#include <gio/gio.h>
#include <exception>
#include <stdexcept>
#include <fstream>

std::vector<std::string> MediaItem::notSupportedExt_ = {
    "rv",
    "ra",
    "rm",
    "asf"
};


// Not part of Device class, this is defined at the bottom of device.h
MediaItem::Type &operator++(MediaItem::Type &type)
{
    if (type == MediaItem::Type::EOL)
        return type;
    type = static_cast<MediaItem::Type>(static_cast<int>(type) + 1);
    return type;
}

// Not part of Device class, this is defined at the bottom of device.h
MediaItem::ExtractorType &operator++(MediaItem::ExtractorType &type)
{
    if (type == MediaItem::ExtractorType::EOL)
        return type;
    type = static_cast<MediaItem::ExtractorType>(static_cast<int>(type) + 1);
    return type;
}

// TODO: need to change as template
// Not part of Device class, this is defined at the bottom of device.h
MediaItem::Meta &operator++(MediaItem::Meta &meta)
{
    if (meta == MediaItem::Meta::EOL)
        return meta;
    meta = static_cast<MediaItem::Meta>(static_cast<int>(meta) + 1);
    return meta;
}

// Not part of Device class, this is defined at the bottom of device.h
MediaItem::AudioMeta &operator++(MediaItem::AudioMeta &meta)
{
    if (meta == MediaItem::AudioMeta::EOL)
        return meta;
    meta = static_cast<MediaItem::AudioMeta>(static_cast<int>(meta) + 1);
    return meta;
}

// Not part of Device class, this is defined at the bottom of device.h
MediaItem::VideoMeta &operator++(MediaItem::VideoMeta &meta)
{
    if (meta == MediaItem::VideoMeta::EOL)
        return meta;
    meta = static_cast<MediaItem::VideoMeta>(static_cast<int>(meta) + 1);
    return meta;
}

// Not part of Device class, this is defined at the bottom of device.h
MediaItem::ImageMeta &operator++(MediaItem::ImageMeta &meta)
{
    if (meta == MediaItem::ImageMeta::EOL)
        return meta;
    meta = static_cast<MediaItem::ImageMeta>(static_cast<int>(meta) + 1);
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

    LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "MIME type '%s' not supported", mime.c_str());

    return false;
}

bool MediaItem::extTypeSupported(const std::string &ext)
{
    if (ext.empty()) {
        LOG_ERROR(MEDIA_INDEXER_MEDIAITEM, 0, "Input fpath is invalid");
        return false;
    }
    auto it = std::find(notSupportedExt_.begin(), notSupportedExt_.end(), ext);
    if (it != notSupportedExt_.end()) {
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "ext %s is not supported extension", ext.c_str());
        return false;
    }
    return true;
}

bool MediaItem::mediaItemSupported(const std::string &path, std::string &mimeType)
{

    gchar *contentType = NULL;
    gboolean uncertain;
    bool _mimeTypeSupported = false;
    contentType = g_content_type_guess(path.c_str(), NULL, 0,
        &uncertain);

    LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "contentType : %s", contentType);

    if (!contentType) {
        LOG_INFO(MEDIA_INDEXER_MEDIAITEM, 0, "MIME type detection is failed for '%s'",
            path.c_str());
        return false;
    }
    mimeType = contentType;
    g_free(contentType);
    std::string ext = path.substr(path.find_last_of('.') + 1);
    if (!extTypeSupported(ext)) {
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "skip file scanning for %s", path.c_str());
        return false;
    }
    _mimeTypeSupported = mimeTypeSupported(mimeType);
    if (!_mimeTypeSupported) {
        // get the file extension for the ts or ps.
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "scan ext '%s'", ext.c_str());

        //TODO: switch case
        if (!ext.compare("ts"))
            mimeType = std::string("video/MP2T");
        else if (!ext.compare("ps"))
            mimeType = std::string("video/MP2P");
        else if (!ext.compare("asf"))
            mimeType = std::string("video/x-asf");
        else {
            LOG_INFO(MEDIA_INDEXER_MEDIAITEM, 0, "it's NOT ts/ps/asf. need to check for '%s'", path.c_str());
            return false;
        }
        // again check the mimtType supported or not.
        _mimeTypeSupported = mimeTypeSupported(mimeType);
    }

    if (uncertain && !_mimeTypeSupported) {
        LOG_INFO(MEDIA_INDEXER_MEDIAITEM, 0, "Invalid MIME type for '%s'", path.c_str());
        return false;
    }
    return true;
}


MediaItem::Type MediaItem::typeFromMime(const std::string &mime)
{
    for (auto type = MediaItem::Type::Audio;
         type < MediaItem::Type::EOL; ++type) {
        auto typeString = MediaItem::mediaTypeToString(type);
        if (!mime.compare(0, typeString.size(), typeString))
            return type;
    }

    LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "MIME type '%s' not supported", mime.c_str());
    return MediaItem::Type::EOL;
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
    case MediaItem::Meta::Year:
        return std::string("year");
    case MediaItem::Meta::Thumbnail:
        return std::string("thumbnail");
    case MediaItem::Meta::GeoLocLongitude:
        return std::string("geo_location_longitude");
    case MediaItem::Meta::GeoLocLatitude:
        return std::string("geo_location_latitude");
    case MediaItem::Meta::GeoLocCountry:
        return std::string("geo_location_country");
    case MediaItem::Meta::GeoLocCity:
        return std::string("geo_location_city");
    case MediaItem::Meta::LastModifiedDate:
        return std::string("last_modified_date");
    case MediaItem::Meta::FileSize:
        return std::string("file_size");
    case MediaItem::Meta::SampleRate:
        return std::string("sample_rate");
    case MediaItem::Meta::Channels:
        return std::string("channels");
    case MediaItem::Meta::BitRate:
        return std::string("bit_rate");
    case MediaItem::Meta::BitPerSample:
        return std::string("bit_per_sample");
    case MediaItem::Meta::VideoCodec:
        return std::string("video_codec");
    case MediaItem::Meta::AudioCodec:
        return std::string("audio_codec");
    case MediaItem::Meta::Lyric:
        return std::string("lyric");
    case MediaItem::Meta::Width:
        return std::string("width");
    case MediaItem::Meta::Height:
        return std::string("height");
    case MediaItem::Meta::FrameRate:
        return std::string("frame_rate");
    case MediaItem::Meta::EOL:
        return "";
    default:
        return "";
    }
}

bool MediaItem::putProperties(std::string metaStr, std::optional<MediaItem::MetaData> data, pbnjson::JValue &props)
{
    if (data.has_value()) {
        auto content = data.value();
        switch (content.index()) {
            case 0:
                props.put(metaStr, std::get<std::int64_t>(content));
                LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Setting '%s' to '%" PRIu64 "'", metaStr.c_str(), std::get<std::int64_t>(content));
                break;
            case 1:
                props.put(metaStr, std::get<double>(content));
                LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Setting '%s' to '%f'", metaStr.c_str(), std::get<double>(content));
                break;
            case 2:
                props.put(metaStr, std::get<std::int32_t>(content));
                LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Setting '%s' to '%d'", metaStr.c_str(), std::get<std::int32_t>(content));
                break;
            case 3:
                props.put(metaStr, std::get<std::string>(content));
                LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Setting '%s' to '%s'", metaStr.c_str(), std::get<std::string>(content).c_str());
                break;
            case 4:
                props.put(metaStr, static_cast<int32_t>(std::get<std::uint32_t>(content)));
                LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Setting '%s' to '%d'", metaStr.c_str(), std::get<std::uint32_t>(content));
                break;
        }
    } else {
        props.put(metaStr, std::string(""));
        LOG_WARNING(MEDIA_INDEXER_MEDIAITEM, 0, "data doesn't have value for meta type %s",metaStr.c_str());
    }
    return true;
}


std::string MediaItem::metaToString(MediaItem::CommonType meta)
{
    switch (meta) {
        case MediaItem::CommonType::URI:
            return std::string("uri");
        case MediaItem::CommonType::DIRTY:
            return std::string("dirty");
        case MediaItem::CommonType::HASH:
            return std::string("hash");
        case MediaItem::CommonType::TYPE:
            return std::string("type");
        case MediaItem::CommonType::MIME:
            return std::string("mime");
        case MediaItem::CommonType::FILEPATH:
            return std::string("file_path");
        case MediaItem::CommonType::KIND:
            return std::string("_kind");
        case MediaItem::CommonType::EOL:
            return "";
        default:
            return "";
    }
}

MediaItem::MediaItem(std::shared_ptr<Device> device, const std::string &path,
                     const std::string &mime, unsigned long hash, unsigned long filesize)
    : device_(device)
    , type_(Type::EOL)
    , hash_(hash)
    , filesize_(filesize)
    , parsed_(false)
    , uri_("")
    , mime_(mime)
    , path_("")
    , ext_("")
    , extractorType_(MediaItem::ExtractorType::EOL)
{
    LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "path : %s, mime : %s, device->uri : %s", path.c_str(), mime.c_str(), device->uri().c_str());
    // create uri
    uri_ = device->uri();
    if (uri_.back() != '/' && path.front() != '/')
        uri_.append("/");
    uri_.append(path);

    path_ = path;
    ext_ = path_.substr(path_.find_last_of('.') + 1);
    // set the type
    for (auto type = MediaItem::Type::Audio;
         type < MediaItem::Type::EOL; ++type) {
        auto typeString = MediaItem::mediaTypeToString(type);
        if (!!mime_.compare(0, typeString.size(), typeString))
            continue;

        type_ = type;
        break;
    }

    // generate random file name
    thumbnailFileName_ = generateRandFilename() + THUMBNAIL_EXTENSION;
    
    if (type_ != Type::EOL)
        device_->incrementMediaItemCount(type_);
}

MediaItem::MediaItem(std::shared_ptr<Device> device, const std::string &path,
                     const std::string &mime, unsigned long hash, unsigned long filesize,
                     const std::string &ext, const MediaItem::Type &type,
                     const MediaItem::ExtractorType &extType)
    : device_(device)
    , type_(type)
    , hash_(hash)
    , filesize_(filesize)
    , parsed_(false)
    , uri_("")
    , mime_(mime)
    , path_(path)
    , ext_(ext)
    , extractorType_(extType)
{
    LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "path : %s, mime : %s, device->uri : %s", path.c_str(), mime.c_str(),
            device->uri().c_str());
    // create uri
    uri_ = device->uri();
    if (uri_.back() != '/' && path.front() != '/')
        uri_.append("/");
    uri_.append(path);

    // generate random file name
    thumbnailFileName_ = generateRandFilename() + THUMBNAIL_EXTENSION;

    if (type_ != Type::EOL)
        device_->incrementMediaItemCount(type_);
}

MediaItem::MediaItem(std::shared_ptr<Device> device, const std::string &path,
                     unsigned long hash, const MediaItem::Type &type)
    : device_(device)
    , type_(type)
    , hash_(hash)
    , filesize_(0)
    , parsed_(false)
    , mime_("")
    , path_(path)
    , ext_("")
    , extractorType_(MediaItem::ExtractorType::EOL)
{
    LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "path : %s, device->uri : %s", path.c_str(), device->uri().c_str());
    // create uri
    uri_ = device->uri();
    if (uri_.back() != '/' && path.front() != '/')
        uri_.append("/");
    uri_.append(path);

    ext_ = path_.substr(path_.find_last_of('.') + 1);

    // generate random file name
    thumbnailFileName_ = generateRandFilename() + THUMBNAIL_EXTENSION;
}

MediaItem::MediaItem(const std::string &uri)
    : device_(Device::device(uri))
    , type_(Type::EOL)
    , parsed_(false)
    , uri_(uri)
    , extractorType_(MediaItem::ExtractorType::EOL)
{
    try {
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "uri_ : %s, device->uri() : %s", uri_.c_str(), device_->uri().c_str());
        std::size_t sz = uri_.find(device_->uri());
        if (sz == std::string::npos) {
            LOG_ERROR(MEDIA_INDEXER_MEDIAITEM, 0, "Failed to found %s for uri : %s",device_->uri().c_str(), uri_.c_str());
        }
        sz += device_->uri().length();
        path_ = uri_.substr(sz);
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "path_ : %s",path_.c_str());
        ext_ = path_.substr(path_.find_last_of('.') + 1);
        auto fpath = std::filesystem::path(path_);
        filesize_ = std::filesystem::file_size(fpath);
        hash_ = std::filesystem::last_write_time(fpath).time_since_epoch().count();

        // generate random file name
        thumbnailFileName_ = generateRandFilename() + THUMBNAIL_EXTENSION;

        if (!MediaItem::mediaItemSupported(path_, mime_)) {
            LOG_ERROR(MEDIA_INDEXER_MEDIAITEM, 0, "Media Item %s is not supported by this system", path_.c_str());
            throw std::runtime_error("error");
        }

        // set the type
        for (auto type = MediaItem::Type::Audio;
             type < MediaItem::Type::EOL; ++type) {
            auto typeString = MediaItem::mediaTypeToString(type);
            if (!!mime_.compare(0, typeString.size(), typeString))
                continue;

            type_ = type;
            break;
        }
    } catch (const std::exception & e) {
        LOG_ERROR(MEDIA_INDEXER_MEDIAITEM, 0, "MediaItem::Ctor failure: %s", e.what());
    } catch (...) {
        LOG_ERROR(MEDIA_INDEXER_MEDIAITEM, 0, "MediaItem::Ctor failure by unexpected failure");
    }
}

bool MediaItem::putExtraMetaToJson(pbnjson::JValue &meta)
{
    for (auto _meta = MediaItem::Meta::Track; _meta < MediaItem::Meta::EOL; ++_meta) {
        auto metaStr = metaToString(_meta);
        auto data = this->meta(_meta);
        if ((type_ == MediaItem::Type::Audio && isAudioMeta(_meta))
            ||(type_ == MediaItem::Type::Video && (isVideoMeta(_meta) || isAudioMeta(_meta)))
            ||(type_ == MediaItem::Type::Image && isImageMeta(_meta))) {
            if (!putProperties(metaStr, data, meta)) {
                LOG_ERROR(MEDIA_INDEXER_MEDIAITEM, 0, "Failed to meta data to json object, type : %s", metaStr.c_str());
                return false;
            }
        }
    }
    return true;

}


unsigned long MediaItem::hash() const
{
    return hash_;
}

unsigned long MediaItem::fileSize() const
{
    return filesize_;
}

const std::string &MediaItem::path() const
{
    return path_;
}

const std::string &MediaItem::ext() const
{
    return ext_;
}

std::shared_ptr<Device> MediaItem::device() const
{
    return device_;
}

const std::string &MediaItem::uuid() const
{
    return device_->uuid();
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
    // parsed_ = true;

    switch (value.index()) {
    case 0:
        // valgrind complains about 'std::int64_t' here, for clean
        // valgrind output we need to set 'long'
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Setting '%s' on '%s' to '%" PRIu64 "'", metaToString(meta).c_str(),
            uri_.c_str(), std::get<std::int64_t>(value));
        break;
    case 1:
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Setting '%s' on '%s' to '%f'", metaToString(meta).c_str(),
            uri_.c_str(), std::get<double>(value));
        break;
    case 2:
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Setting '%s' on '%s' to '%d'", metaToString(meta).c_str(),
            uri_.c_str(), std::get<std::int32_t>(value));
        break;
    case 3:
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Setting '%s' on '%s' to '%s'", metaToString(meta).c_str(),
            uri_.c_str(), std::get<std::string>(value).c_str());
        break;
    case 4:
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Setting '%s' on '%s' to '%d'", metaToString(meta).c_str(),
            uri_.c_str(), std::get<std::uint32_t>(value));
        break;
    default:
        LOG_DEBUG(MEDIA_INDEXER_MEDIAITEM, "Invalid index for setting meta '%s'", uri_.c_str());
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

MediaItem::ExtractorType MediaItem::extractorType() const
{
    return extractorType_;
}

IMediaItemObserver *MediaItem::observer() const
{
    return device_->observer();
}

bool MediaItem::isMediaMeta(Meta meta){
    switch (meta) {
        case MediaItem::Meta::FileSize:
        case MediaItem::Meta::DateOfCreation:
        case MediaItem::Meta::LastModifiedDate:
            return true;
        default:
            return false;
    }
    return false;
}

bool MediaItem::isAudioMeta(Meta meta){
    switch (meta) {
        case MediaItem::Meta::Title:
        case MediaItem::Meta::Genre:
        case MediaItem::Meta::Album:
        case MediaItem::Meta::Artist:
        case MediaItem::Meta::Duration:
        case MediaItem::Meta::Thumbnail:
        case MediaItem::Meta::FileSize:
        case MediaItem::Meta::LastModifiedDate:
        case MediaItem::Meta::AlbumArtist:
        case MediaItem::Meta::Track:
        case MediaItem::Meta::TotalTracks:
        case MediaItem::Meta::SampleRate:
        case MediaItem::Meta::BitPerSample:
        case MediaItem::Meta::Channels:
        case MediaItem::Meta::BitRate:
        case MediaItem::Meta::AudioCodec:
        case MediaItem::Meta::Lyric:
        case MediaItem::Meta::DateOfCreation:
            return true;
        default:
            return false;
    }
    return false;
}

bool MediaItem::isVideoMeta(Meta meta){
    switch (meta) {
        case MediaItem::Meta::Title:
        case MediaItem::Meta::Duration:
        case MediaItem::Meta::Width:
        case MediaItem::Meta::Height:
        case MediaItem::Meta::VideoCodec:
        case MediaItem::Meta::AudioCodec:
        case MediaItem::Meta::Thumbnail:
        case MediaItem::Meta::FrameRate:
        case MediaItem::Meta::FileSize:
        case MediaItem::Meta::DateOfCreation:
        case MediaItem::Meta::LastModifiedDate:
            return true;
        default:
            return false;
    }
    return false;
}

bool MediaItem::isImageMeta(Meta meta){
    switch (meta) {
        case MediaItem::Meta::Title:
        case MediaItem::Meta::Width:
        case MediaItem::Meta::Height:
        case MediaItem::Meta::GeoLocLongitude:
        case MediaItem::Meta::GeoLocLatitude:
        case MediaItem::Meta::GeoLocCountry:
        case MediaItem::Meta::GeoLocCity:
        case MediaItem::Meta::FileSize:
        case MediaItem::Meta::DateOfCreation:
        case MediaItem::Meta::LastModifiedDate:
            return true;
        default:
            return false;
    }
    return false;
}

std::string MediaItem::generateRandFilename() const
{
    uint64_t val = 0;
    std::ifstream fs("/dev/urandom", std::ios::in|std::ios::binary);
    if (fs)
        fs.read(reinterpret_cast<char *>(&val), sizeof(val));
    fs.close();
    return std::to_string(val).substr(0, thumbnailFileNameLength_);
}

std::string MediaItem::getThumbnailFileName() const
{
    return thumbnailFileName_;
}

void MediaItem::setThumbnailFileName(const std::string& name)
{
    thumbnailFileName_ = name;
}
