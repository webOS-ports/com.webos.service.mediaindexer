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

#if defined HAS_GSTREAMER
#include <gst/gst.h>
#include <gst/pbutils/gstdiscoverer.h>
#include <gst/pbutils/pbutils.h>
#include <gst/audio/audio.h>
#endif

#include <turbojpeg.h>
#include <chrono>
#include <mutex>

#define GST_TAG_THUMBNAIL "thumbnail"

/**
 * \brief Media parser class for meta data extraction.
 *
 * This class extracts meta data using the GStreamer framework.
 */
class GStreamerExtractor : public IMetaDataExtractor
{
public:
    enum class StreamMeta : int {
        SampleRate = 0, ///< Audio sample rate.
        Channels, ///< Audio channels.
        BitRate, ///< Audio bitrate.
        BitPerSample, ///<Audio bit per sample.
        Width, ///< Video width.
        Height, ///< Video height.
        FrameRate, ///< Video framerate.
        EOL /// End of list marker.
    };

    GStreamerExtractor();
    virtual ~GStreamerExtractor();

    /// From interface.
    bool extractMeta(MediaItem &mediaItem, bool extra = false) const;

private:
    /// Get media item meta identifier from GStreamer tag.
    MediaItem::Meta metaFromTag(const char *gstTag) const;

    /// Get Thumbnail Image of video
    bool getThumbnail(MediaItem &mediaItem, std::string &filename, const std::string &ext = "jpg") const;

    /// Save Image to jpeg with libjpeg-turbo
    bool saveBufferToImage(void *data, int32_t width, int32_t height,
                                  const std::string &filename, const std::string &ext = "jpg") const;

    /// Set media item media per media type.
    void setMeta(MediaItem &mediaItem, GstDiscovererInfo *metaInfo,
        const char *tag) const;
    void setStreamMeta(MediaItem &mediaItem, GstDiscovererStreamInfo *streamInfo, bool extra = false) const;

    static std::map<std::string, MediaItem::Meta> metaMap_;
};

/// Useful when iterating over enum.
GStreamerExtractor::StreamMeta &operator++(GStreamerExtractor::StreamMeta &meta);
