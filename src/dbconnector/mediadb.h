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

#include "dbconnector.h"
#include "mediaitem.h"

#include <memory>
#include <mutex>
#include <list>

class Device;

/// Connector to com.webos.mediadb.
class MediaDb : public DbConnector
{
public:

    // TODO: other APIs shouled be added
    enum class MediaDbMethod : int {
        GetAudioList,
        GetVideoList,
        GetImageList,
        GetAudioMetaData,
        GetVideoMetaData,
        GetImageMetaData,
        RequestDelete,
        RemoveDirty,
        EOL
    };

    /**
     * \brief Get device database connector.
     *
     * \return Singleton object.
     */
    static MediaDb *instance();

    virtual ~MediaDb();

    /**
     * \brief Db service response handler.
     *
     * \return Result of message processing.
     */
    virtual bool handleLunaResponse(LSMessage *msg);


    /**
     * \brief Db service response handler.
     *
     * \return Result of message processing.
     */
    virtual bool handleLunaResponseMetaData(LSMessage *msg);

    /**
     * \brief Check media item hash fpr change.
     *
     * If the media item did not change it will be free'd. If it did
     * change, the observer will get notified.
     *
     * \param[in] mediaItem The media item to check.
     */
    void checkForChange(MediaItemPtr mediaItem);

    /**
     * \brief Check whether db data of media item should be updated or not.
     *
     * If the media item did not change it will be free'd. If it did
     * change, the observer will get notified.
     *
     * \param[in] mediaItem The media item to check.
     */
    bool needUpdate(MediaItem *mediaItem);

    /**
     * \brief Update the media item meta in the database.
     *
     * \param[in] mediaItem The media item to update.
     */
    void updateMediaItem(MediaItemPtr mediaItem);

    /**
     * \brief Get the file path for the given media item uri.
     *
     * \param[in] uri Base uri for plugin identification.
     * \return The file path if available.
     */
    std::optional<std::string> getFilePath(const std::string &uri) const;

    /**
     * \brief Mark all media items of this device dirty.
     *
     * \param[in] device The device to mark dirty.
     */
    void markDirty(std::shared_ptr<Device> device, MediaItem::Type type = MediaItem::Type::EOL);

    /**
     * \brief Mark media item as not dirty.
     *
     * \param[in] uri Uri of the media item to unflag.
     */
    void unflagDirty(MediaItemPtr mediaItem);

    void unmarkAllDirty(std::shared_ptr<Device> device, MediaItem::Type type = MediaItem::Type::EOL);

    /**
     * \brief Remove all dirty flagged items for this device.
     *
     * \param[in] device The device to remove dirties.
     */
    void removeDirty(Device* device);

    /**
     * \brief Add service to db access list.
     *
     * \param[in] serviceName The service name.
     */
    void grantAccess(const std::string &serviceName);

    void grantAccessAll(const std::string &serviceName, bool atomic, pbnjson::JValue &resp, const std::string &methodName = std::string());

    bool getAudioList(const std::string &uri, int count, LSMessage *msg = nullptr, bool expand = false);

    bool getVideoList(const std::string &uri, int count, LSMessage *msg = nullptr, bool expand = false);

    bool getImageList(const std::string &uri, int count, LSMessage *msg = nullptr, bool expand = false);

    void makeUriIndex();

    /**
     * \brief request to delete in Media DB.
     *
     * \param[in] uri Uri of the media item.
     * \param[in] resp return response.
     */
    bool requestDelete(const std::string &uri, LSMessage *msg = nullptr);

    /**
     * \brief guess media type with uri.
     *
     * \param[in] uri Uri of the media item.
     */
    MediaItem::Type guessType(const std::string &uri);

    /**
     * \brief flush dirty flag handling.
     *
     * \param[in] device The device to unflag dirty.
     */
    void flushUnflagDirty(Device* device);

    /**
     * \brief flush dirty flag handling.
     *
     * \param[in] device The device to remove items.
     */
    void flushDeleteItems(Device* device);

    /**
     * \brief put meta data of media item to buffer.
     *        If more than a certain number is accumulated,
     *        it is flushed.
     * \param[in] params Meta data formatted with json format.
     * \param[in] device The device contains the media item.
     * \return true if the operation is sucessful.
     */
    bool putMeta(pbnjson::JValue &params, DevicePtr device);

    /**
     * \brief flush meta data from buffer to db8 service.
     * \param[in] device The device contains the media item related to meta data to be flushed.
     * \return true if the operation is sucessful.
     */
    bool flushPut(Device* device);

    /**
     * \brief reset temporary buffer used in first scanning procedure
     * \param[in] uri The uri of corresponding device.
     * \return true if the operation is sucessful.
     */
    bool resetFirstScanTempBuf(const std::string &uri);

    /**
     * \brief reset temporary buffer used in rescanning procedure
     * \param[in] uri The uri of corresponding device.
     * \return true if the operation is sucessful.
     */
    bool resetReScanTempBuf(const std::string &uri);

    /**
     * \brief request to delete in Media DB.
     *
     * \param[in] uri Uri of the media item.
     * \param[in] type Type of the media item.
     * \param[in] device The device to remove items.
     */
    void requestDeleteItem(MediaItemPtr mediaitem);
protected:
    /// Singleton.
    MediaDb();

private:
    bool prepareWhere(const std::string &key,
                      const std::string &value,
                      bool precise,
                      pbnjson::JValue &whereClause) const;

    bool prepareWhere(const std::string &key,
                      bool value,
                      bool precise,
                      pbnjson::JValue &whereClause) const;

    bool prepareOperation(const std::string &method,
                          pbnjson::JValue &param,
                          pbnjson::JValue &operationClause) const;



    /// Singleton object.
    static std::unique_ptr<MediaDb> instance_;
    std::map<MediaItem::Type, std::string> kindMap_ = {
        {MediaItem::Type::Audio, AUDIO_KIND},
        {MediaItem::Type::Video, VIDEO_KIND},
        {MediaItem::Type::Image, IMAGE_KIND}
    };

    std::map<std::string, MediaDbMethod> dbMethodMap_ = {
        { std::string("getAudioList"),   MediaDbMethod::GetAudioList  },
        { std::string("getVideoList"),   MediaDbMethod::GetVideoList  },
        { std::string("getImageList"),   MediaDbMethod::GetImageList  },
        { std::string("getAudioMetaData"),   MediaDbMethod::GetAudioMetaData  },
        { std::string("getVideoMetaData"),   MediaDbMethod::GetVideoMetaData  },
        { std::string("getImageMetaData"),   MediaDbMethod::GetImageMetaData  },
        { std::string("requestDelete"),  MediaDbMethod::RequestDelete },
        { std::string("removeDirty"),    MediaDbMethod::RemoveDirty   }
    };

    /// List of services that should have read-only access to
    /// database.
    std::list<std::string> dbClients_;
    std::map<std::string, unsigned long> mediaItemMap_;
    std::mutex mutex_;

    //static constexpr char MEDIA_KIND[]  = "com.webos.service.mediaindexer.media:1";
    static constexpr char AUDIO_KIND[] = "com.webos.service.mediaindexer.audio:1";
    static constexpr char VIDEO_KIND[] = "com.webos.service.mediaindexer.video:1";
    static constexpr char IMAGE_KIND[] = "com.webos.service.mediaindexer.image:1";

    static constexpr char URI[] = "uri";
    static constexpr char HASH[] = "hash";
    static constexpr char DIRTY[] = "dirty";
    static constexpr char TYPE[] = "type";
    static constexpr char MIME[] = "mime";
    static constexpr char FILE_PATH[] = "file_path";

    std::map<std::string, pbnjson::JValue> firstScanTempBuf_;
    std::map<std::string, pbnjson::JValue> reScanTempBuf_;
};
