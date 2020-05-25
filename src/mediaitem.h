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

#pragma once

#include "logging.h"
#include "imediaitemobserver.h"

#include <memory>
#include <map>
#include <optional>
#include <variant>
#include <string>
#include <filesystem>

class Device;

/// Base class for devices like MTP or UPnP servers.
class MediaItem
{
public:
    /// Meta data type specifiers.
    enum class Meta {
        Title, ///< Media title, mandatory.
        Genre, ///< Media genre.
        Album, ///< Media album.
        Artist, ///< Media artist.
        AlbumArtist, ///< The album artist, set to artist of not
                     ///available.
        Track, ///< Track number in album.
        TotalTracks, ///< Total number of tracks in album.
        DateOfCreation, ///< Date of creation.
        Duration, ///< Media duration in seconds.
        GeoLocLongitude, ///< Location longitude.
        GeoLocLatitude, ///< Location latitude.
        GeoLocCountry, ///< Location country code.
        GeoLocCity, ///< Location city name.
        EOL /// End of list marker.
    };

    /// Media item type specifier.
    enum class Type {
        Audio, ///< Audio type media item.
        Video, ///< Video type media item.
        Image, ///< Image type media item.
        EOL ///< End of list marker.
    };

    /**
     * \brief Check if given MIME type is supported.
     *
     * \param[in] mime MIME type string.
     * \return True if supported, else false.
     */
    static bool mimeTypeSupported(const std::string &mime);

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

    /// Simplify type definition for use with std::optional.
    typedef std::variant<std::int64_t, double, std::string> MetaData;

    /**
     * \brief Construct device by uri.
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
     */
    MediaItem(std::shared_ptr<Device> device, const std::string &path,
        const std::string &mime, unsigned long hash);
    virtual ~MediaItem() {};

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
     * \brief Give the path as set from constructor.
     *
     * \return The path.
     */
    const std::string &path() const;

    /**
     * \brief Get the media item device.
     *
     * \return The device.
     */
    std::shared_ptr<Device> device() const;

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
     *\brief Gives us the current media item observer.
     *
     * \return The observer or nullptr if none is set.
     */
    virtual IMediaItemObserver *observer() const;

private:
    /// Get message id.
    LOG_MSGID;

    /// Device mandatory for construction.
    MediaItem() {};

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
    /// If the media item has been parsed.
    bool parsed_;
    /// The media item uri.
    std::string uri_;
    /// The MIME type
    std::string mime_;
    /// The path string.
    std::string path_;
};

/// Useful when iterating over enum.
MediaItem::Type &operator++(MediaItem::Type &type);

/// Useful when iterating over enum.
MediaItem::Meta &operator++(MediaItem::Meta &meta);

/// This is handled with unique_ptr so give it an alias.
typedef std::unique_ptr<MediaItem> MediaItemPtr;