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

#pragma once

#include "logging.h"
#include "imediaitemobserver.h"
#include <pbnjson.hpp>

#include <memory>
#include <map>
#include <optional>
#include <variant>
#include <string>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <vector>

class Device;

/// This is handled with unique_ptr so give it an alias.
typedef std::unique_ptr<MediaItem> MediaItemPtr;


typedef struct MediaItemWrapper
{
    MediaItemPtr mediaItem_;
} MediaItemWrapper_t;


/// Base class for devices like MTP or UPnP servers.
class MediaItem
{
public:
    /// Common Meta data type specifiers.
    enum class CommonType : int {
        URI, ///< Uri of media item
        DIRTY, ///< Dirty flag of media item set when device is unavailable
        HASH, ///< Unique value for media item
        TYPE, ///< Media type, possible value : audio / video / image
        MIME, ///< Mime type of media item
        FILEPATH, ///< Accessible file path
        KIND, ///< DB kind
        EOL
    };
    /// Meta data type specifiers.
    enum class Meta : int {
        // Common Meta Data
        Title, ///< Media title, mandatory.
        Genre, ///< Media genre.
        Album, ///< Media album.
        Artist, ///< Media artist.
        Duration, ///< Media duration in seconds.
        Thumbnail, ///< Attached Picture
        LastModifiedDate, ///< Last modified date.(formatted)
        LastModifiedDateRaw, ///< Last modified date.(not formatted)
        FileSize, ///< File size.
        Width, ///< Video width.
        Height, ///< Video height.
        // Extra Meta Data
        Track, ///< Track number in album.
        AlbumArtist, ///< The album artist, set to artist of not available.
        TotalTracks, ///< Total number of tracks in album.
        DateOfCreation, ///< Date of creation.
        Year, ///< Recoding time(Year).
        GeoLocLongitude, ///< Location longitude.
        GeoLocLatitude, ///< Location latitude.
        GeoLocCountry, ///< Location country code.
        GeoLocCity, ///< Location city name.
        VideoCodec, ///< Video Codec Information.
        AudioCodec, ///< Audio Codec Information.
        SampleRate, ///< Audio sample rate.
        Channels, ///< Audio channels.
        BitRate, ///< Audio bitrate.
        BitPerSample, ///<Audio bit per sample.
        Lyric, ///< Audio Lyric.
        FrameRate, ///< Video framerate.
        EOL /// End of list marker.
    };

    /// Audio Meta data type specifiers.
    enum class AudioMeta : int {
        SampleRate,
        Channels,
        Bitrate,
        BitPerSample,
        EOL
    };

    /// Video Meta data type specifiers.
    enum class VideoMeta : int {
        Width,
        Height,
        FrameRate,
        EOL
    };

    /// Image Meta data type specifiers.
    enum class ImageMeta : int {
        EOL
    };

    /// Media item type specifier.
    enum class Type : int {
        Audio, ///< Audio type media item.
        Video, ///< Video type media item.
        Image, ///< Image type media item.
        EOL ///< End of list marker.
    };

    enum class ExtractorType : int {
        TagLibExtractor,
        GStreamerExtractor,
        ImageExtractor,
        EOL
    };

    /**
     * \brief Check if given MIME type is supported.
     *
     * \param[in] mime MIME type string.
     * \return True if supported, else false.
     */
    static bool mimeTypeSupported(const std::string &mime);

    /**
     * \brief Check if given ext type is supported.
     *
     * \param[in] ext extension type string.
     * \return True if supported, else false.
     */
    static bool extTypeSupported(const std::string &ext);

    /**
     * \brief Check if given media item is supported.
     *
     * \param[in] ext extension type string.
     * \return True if supported, else false.
     */
    static bool mediaItemSupported(const std::string &path, std::string &mimeType);

    /**
     * \brief Get media type from mime type.
     *
     * \param[in] mime MIME type string.
     * \return MediaItem::Type audio, video or image.
     */
    static MediaItem::Type typeFromMime(const std::string &mime);

    /**
     * \brief Convert media type to string.
     *
     * \param[in] type Media type.
     * \return The related string.
     */
    static std::string mediaTypeToString(MediaItem::Type type);

    /**
     * \brief Convert meta type to string.
     *
     * \param[in] meta The meta type.
     * \return The related string.
     */
    static std::string metaToString(MediaItem::Meta meta);

    /**
     * \brief Convert common meta type to string.
     *
     * \param[in] meta The common meta type.
     * \return The related string.
     */
    static std::string metaToString(MediaItem::CommonType meta);

    /// Simplify type definition for use with std::optional.
    typedef std::variant<std::int64_t, double, std::int32_t, std::string, std::uint32_t> MetaData;

    /**
     * \brief put specific meta data to json object.
     *
     * \param[in] metaStr Meta type indicator.
     * \param[in] data Meta data corresponds to meta type.
     * \return result(true or false).
     */
    static bool putProperties(std::string metaStr, std::optional<MetaData> data, pbnjson::JValue &props);

    /**
     * \brief Construct media item.
     *
     * The device is referenced from a std::shared_ptr as the device
     * might be destroyed in the plugin while still in use from this
     * media item.
     *
     * The path string must begin with a '/' if it is a file path and
     * must not begin with '/' else.
     *
     * \param[in] device The device this media item belongs to.
     * \param[in] path The media item path.
     * \param[in] mime The MIME type information.
     * \param[in] hash Some hash to check for modifications.
     * \param[in] filesize The media item filesize(byte).
     */
    MediaItem(std::shared_ptr<Device> device, const std::string &path,
              const std::string &mime, unsigned long hash, unsigned long filesize = 0);

    MediaItem(std::shared_ptr<Device> device, const std::string &path, unsigned long hash,
              const MediaItem::Type &type);
    /**
     * \brief Construct media item.
     *
     * The device is referenced from a std::shared_ptr as the device
     * might be destroyed in the plugin while still in use from this
     * media item.
     *
     * The path string must begin with a '/' if it is a file path and
     * must not begin with '/' else.
     *
     * \param[in] device The device this media item belongs to.
     * \param[in] path The media item path.
     * \param[in] mime The MIME type information.
     * \param[in] hash Some hash to check for modifications.
     * \param[in] filesize The media item filesize(byte).
     * \param[in] ext The media file extension.
     * \param[in] type The media item type.
     * \param[in] extType The extractor type.
     */
    MediaItem(std::shared_ptr<Device> device, const std::string &path,
              const std::string &mime, unsigned long hash, unsigned long filesize,
              const std::string &ext, const MediaItem::Type &type,
              const MediaItem::ExtractorType &extType);

    /**
     * \brief Construct media item only with uri.
     *
     * The media item is created with only referencing uri of media file.
     *
     * The purpose of this constructor is for direct meta data extraction.
     *
     * \param[in] device The device this media item belongs to.
     * \param[in] path The media item path.
     * \param[in] mime The MIME type information.
     * \param[in] hash Some hash to check for modifications.
     * \param[in] filesize The media item filesize(byte).
     */
    MediaItem(const std::string &uri);

    virtual ~MediaItem() {};

    /**
     * \brief put meta data of this media item to given json object.
     *
     * This will write all available meta data to json object
     * given as input parameter.
     *
     * \param[in] meta Json object that meta data of this item to be stored.
     * \return result(true or false).
     */
    bool putExtraMetaToJson(pbnjson::JValue &meta);

    /**
     * \brief Get the identifier of this media item.
     *
     * This will most often be the timestamp of last modification,
     * however, it could also be something else if the timestamp is
     * not available, e. g. for upnp items it is not guaranteed to be
     * available.
     *
     * \return The timestamp.
     */
    unsigned long hash() const;

    /**
     * \brief Get file size of media item(unit : byte).
     *
     * \return The filesize.
     */
     unsigned long fileSize() const;

    /**
     * \brief Give the path as set from constructor.
     *
     * \return The path.
     */
    const std::string &path() const;

    /**
     * \brief Give the file extension as set from constructor.
     *
     * \return The file extension.
     */
    const std::string &ext() const;

    /**
     * \brief Get the media item device.
     *
     * \return The device.
     */
    std::shared_ptr<Device> device() const;

   /**
     * \brief Get uuid of device.
     *
     * \return uuid of device.
     */
    const std::string &uuid() const;

    /**
     * \brief Get a specific meta data entry.
     *
     * \param[in] meta The type of meta data.
     * \return The meta data value if available.
     */
    std::optional<MetaData> meta(Meta meta) const;

    /**
     * \brief Change meta data entry.
     *
     * \param[in] meta The type of meta data.
     * \param[in] value The new meta data value.
     */
    void setMeta(Meta meta, MetaData value);

    /**
     * \brief Set media item parsed.
     * \param[in] value The value to indicate media item is parsed.
     *
     */
    void setParsed(bool value) { parsed_ = value; }

    /**
     * \brief Set media item type.
     * \param[in] type The value to indicate media item type.
     *
     */
    void setType(MediaItem::Type type) { type_ = type; }

    /**
     * \brief Set extractor type for this media item.
     * \param[in] type The value to indicate extractor type.
     *
     */
    void setExtractorType(MediaItem::ExtractorType type) { extractorType_ = type; }

    /**
     * \brief Check if media item has been parsed.
     *
     * \return True if parsed, else false.
     */
    bool parsed() const;

    /**
     * \brief Get the media item uri.
     *
     * \return The URI string.
     */
    const std::string &uri() const;

    /**
     * \brief Get the media item MIME type.
     *
     * \return The MIME string.
     */
    const std::string &mime() const;

    /**
     * \brief Get the media item type.
     *
     * \return The type.
     */
    MediaItem::Type type() const;

    /**
     * \brief Get the extractor type.
     *
     * \return The type.
     */
    MediaItem::ExtractorType extractorType() const;

    /**
     *\brief Gives us the current media item observer.
     *
     * \return The observer or nullptr if none is set.
     */
    virtual IMediaItemObserver *observer() const;

    bool isMediaMeta(Meta meta);
    bool isAudioMeta(Meta meta);
    bool isVideoMeta(Meta meta);
    bool isImageMeta(Meta meta);

    /**
     *\brief Generate the random thumbnail file name of media item
     *
     * \return The thumbnail file name.
     */
    std::string generateRandFilename() const;

    /**
     *\brief Get the thumbnail file name of media item
     *
     * \return The thumbnail file name.
     */
    std::string getThumbnailFileName() const;

    /**
     * \brief Set the thumbnail file name as given name
     * \param[in] name The value to indicate thumbnail name.
     */
    void setThumbnailFileName(const std::string& name);

private:
    /// Get message id.
    LOG_MSGID;

    /// Device mandatory for construction.
    MediaItem() : type_(Type::EOL), hash_(0), filesize_(0), parsed_(false), extractorType_(MediaItem::ExtractorType::EOL) {};

    /// Device this media item belongs to.
    std::shared_ptr<Device> device_;
    /// Type of media item.
    Type type_;
    /// Set of meta data available for this media item.
    std::map<MediaItem::Meta, MediaItem::MetaData> meta_;
    /// A file hash to check for modifications, could be the
    /// modification timestamp of a file, the file size or something
    /// else.
    unsigned long hash_;
    /// filesize
    unsigned long filesize_;
    /// If the media item has been parsed.
    bool parsed_;
    /// The media item uri.
    std::string uri_;
    /// The MIME type
    std::string mime_;
    /// The path string.
    std::string path_;
    /// The file extension
    std::string ext_;
    /// Type of extractor.
    ExtractorType extractorType_;
    /// Not supported ext
    static std::vector<std::string> notSupportedExt_;
    /// The thumbnail file name
    std::string thumbnailFileName_;
    const int thumbnailFileNameLength_ = 15; 
};

/// Useful when iterating over enum.
MediaItem::Type &operator++(MediaItem::Type &type);

/// Useful when iterating over enum.
MediaItem::ExtractorType &operator++(MediaItem::ExtractorType &type);

// TODO: need to change as template
/// Useful when iterating over enum.
MediaItem::Meta &operator++(MediaItem::Meta &meta);

/// Useful when iterating over enum.
MediaItem::AudioMeta &operator++(MediaItem::AudioMeta &meta);

/// Useful when iterating over enum.
MediaItem::VideoMeta &operator++(MediaItem::VideoMeta &meta);

/// Useful when iterating over enum.
MediaItem::ImageMeta &operator++(MediaItem::ImageMeta &meta);
