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
#include "imageextractor.h"
#define PNG_BYTES_TO_CHECK 8

static bool setJpegImageResolution(MediaItem &mediaItem, void *ctx);
static bool setBmpImageResolution(MediaItem &mediaItem, void *ctx);
static bool setPngImageResolution(MediaItem &mediaItem, void *ctx);
static bool setGifImageResolution(MediaItem &mediaItem, void *ctx);

std::map<MediaItem::Meta, ExifMapStructure> ImageExtractor::exifMap_ = {
    {MediaItem::Meta::DateOfCreation, {EXIF_IFD_0, {EXIF_TAG_DATE_TIME}}},
    //{MediaItem::Meta::Width, {EXIF_IFD_EXIF, {EXIF_TAG_PIXEL_X_DIMENSION}}},
    //{MediaItem::Meta::Height, {EXIF_IFD_EXIF, {EXIF_TAG_PIXEL_Y_DIMENSION}}},
    {MediaItem::Meta::GeoLocLongitude, {EXIF_IFD_GPS, {(ExifTag)(EXIF_TAG_GPS_LONGITUDE_REF), (ExifTag)(EXIF_TAG_GPS_LONGITUDE)}}},
    {MediaItem::Meta::GeoLocLatitude, {EXIF_IFD_GPS, {(ExifTag)(EXIF_TAG_GPS_LATITUDE_REF), (ExifTag)(EXIF_TAG_GPS_LATITUDE)}}}
};

std::vector<MediaItem::Meta> ImageExtractor::basicFlag_ = {
    MediaItem::Meta::Width,
    MediaItem::Meta::Height
};

std::vector<MediaItem::Meta> ImageExtractor::extraFlag_ = {
    MediaItem::Meta::DateOfCreation,
    MediaItem::Meta::GeoLocLongitude,
    MediaItem::Meta::GeoLocLatitude,
    MediaItem::Meta::GeoLocCountry,
    MediaItem::Meta::GeoLocCity
};

static std::map<MediaItem::Meta, std::string> gstTagMap = {
    {MediaItem::Meta::DateOfCreation, GST_TAG_DATE_TIME},
    {MediaItem::Meta::GeoLocLongitude, GST_TAG_GEO_LOCATION_LONGITUDE},
    {MediaItem::Meta::GeoLocLatitude, GST_TAG_GEO_LOCATION_LATITUDE},
    {MediaItem::Meta::GeoLocCountry, GST_TAG_GEO_LOCATION_COUNTRY},
    {MediaItem::Meta::GeoLocCity, GST_TAG_GEO_LOCATION_CITY}
};

std::map<std::string, std::function<bool(MediaItem &, void *)>> ImageExtractor::resolutionHadler_ = {
    {"jpg", setJpegImageResolution},
    {"bmp", setBmpImageResolution},
    {"png", setPngImageResolution},
    {"gif", setGifImageResolution}
};

bool setJpegImageResolution(MediaItem &mediaItem, void *ctx)
{
    //auto begin = std::chrono::high_resolution_clock::now();
    struct jpeg_decompress_struct cinfo;
    struct jpegErrorHandler {
        jpeg_error_mgr jerr;
        jmp_buf setjmpBuffer;
    } jpeg_error_handler;

    FILE *fp = fopen(mediaItem.path().c_str(), "rb");
    if(fp == NULL) {
         LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "Failed to open file %s", mediaItem.path().c_str());
         return false;
    }
    cinfo.err = jpeg_std_error(&jpeg_error_handler.jerr);
    jpeg_error_handler.jerr.error_exit = [](j_common_ptr info) {
        longjmp(reinterpret_cast<jpegErrorHandler*>(info->err)->setjmpBuffer, 1);
    };
    if(setjmp(jpeg_error_handler.setjmpBuffer)) {
        LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "error while reading JPEG file");
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return false;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);
    mediaItem.setMeta(MediaItem::Meta::Width, MediaItem::MetaData(cinfo.image_width));
    mediaItem.setMeta(MediaItem::Meta::Height, MediaItem::MetaData(cinfo.image_height));
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    //auto end = std::chrono::high_resolution_clock::now();
    //auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
    //LOG_DEBUG(MEDIA_INDEXER_IMAGEEXTRACTOR, "elapsed time = %d", (int)(elapsedTime.count()));
    return true;
}

bool setBmpImageResolution(MediaItem &mediaItem, void *ctx)
{
    //auto begin = std::chrono::high_resolution_clock::now();
    gint width, height;
    auto fname = mediaItem.path().c_str();
    if (gdk_pixbuf_get_file_info(fname, &width, &height)) {
        LOG_DEBUG(MEDIA_INDEXER_IMAGEEXTRACTOR, "set width/height of bmp file for %s, (%d, %d)", fname, width, height);
        mediaItem.setMeta(MediaItem::Meta::Width, MediaItem::MetaData(width));
        mediaItem.setMeta(MediaItem::Meta::Height, MediaItem::MetaData(height));
    } else {
        LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "Failed to get information from bmp %s", fname);
        return false;
    }
    //auto end = std::chrono::high_resolution_clock::now();
    //auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
    //LOG_DEBUG(MEDIA_INDEXER_IMAGEEXTRACTOR, "elapsed time = %d", (int)(elapsedTime.count()));
    return true;
}

bool setPngImageResolution(MediaItem &mediaItem, void *ctx)
{
    //auto begin = std::chrono::high_resolution_clock::now();
    unsigned char buf[PNG_BYTES_TO_CHECK];
    auto fname = mediaItem.path().c_str();
    FILE *fp = fopen(fname, "rb");
    if (fp == NULL) {
        LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "Failed to read file %s", fname);
        return false;
    }
    if (fread(buf, 1, PNG_BYTES_TO_CHECK, fp) != PNG_BYTES_TO_CHECK) {
        LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "Failed to read %d bytes from file %s", PNG_BYTES_TO_CHECK, fname);
        goto png_read_failure;
    }

    if (!png_sig_cmp(buf, 0, PNG_BYTES_TO_CHECK)) {
        png_structp pngPtr;
        png_infop infoPtr;
        pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (pngPtr == NULL) {
            LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "Failed to create read struct(type : png_structp)");
            goto png_read_failure;
        }

        infoPtr = png_create_info_struct(pngPtr);
        if (infoPtr == NULL) {
            LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "Failed to create info struct(type : png_infop)");
            goto png_read_failure;
        }
        png_init_io(pngPtr, fp);
        png_set_sig_bytes(pngPtr, PNG_BYTES_TO_CHECK);

        if (setjmp(png_jmpbuf(pngPtr))) {
            LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "error while reading PNG file");
            png_destroy_read_struct(&pngPtr, &infoPtr, NULL);
            goto png_read_failure;
        }
        png_read_info(pngPtr, infoPtr);
        uint32_t width = static_cast<uint32_t>(png_get_image_width(pngPtr, infoPtr));
        uint32_t height = static_cast<uint32_t>(png_get_image_height(pngPtr, infoPtr));
        mediaItem.setMeta(MediaItem::Meta::Width, MediaItem::MetaData(width));
        mediaItem.setMeta(MediaItem::Meta::Height, MediaItem::MetaData(height));
        png_destroy_read_struct(&pngPtr, &infoPtr, NULL);
        fclose(fp);
    } else {
        LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "png_sig_cmp failed");
        fclose(fp);
        return false;
    }
    //auto end = std::chrono::high_resolution_clock::now();
    //auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
    //LOG_DEBUG(MEDIA_INDEXER_IMAGEEXTRACTOR, "elapsed time = %d", (int)(elapsedTime.count()));
    return true;
png_read_failure:
    fclose(fp);
    return false;
}

bool setGifImageResolution(MediaItem &mediaItem, void *ctx)
{
    //auto begin = std::chrono::high_resolution_clock::now();
    int err;
    auto fname = mediaItem.path().c_str();
    GifFileType* gifFileType = DGifOpenFileName(fname, &err);
    if (!gifFileType) {
        LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "DGifOpenFileName() failed - %s", GifErrorString(err));
        return false;
    }
    if (DGifSlurp(gifFileType) == GIF_ERROR) {
        LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "DGifSlurp() failed - %s", GifErrorString(gifFileType->Error));
        DGifCloseFile(gifFileType, &err);
        return false;
    }
    mediaItem.setMeta(MediaItem::Meta::Width, MediaItem::MetaData((std::uint32_t)gifFileType->SWidth));
    mediaItem.setMeta(MediaItem::Meta::Height, MediaItem::MetaData((std::uint32_t)gifFileType->SHeight));
    DGifCloseFile(gifFileType, &err);
    //auto end = std::chrono::high_resolution_clock::now();
    //auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
    //LOG_DEBUG(MEDIA_INDEXER_IMAGEEXTRACTOR, "elapsed time = %d", (int)(elapsedTime.count()));
    return true;
}

ImageExtractor::ImageExtractor()
{
    // nothing to be done here
}

ImageExtractor::~ImageExtractor()
{
    // nothing to be done here
}

bool ImageExtractor::setDefaultMeta(MediaItem &mediaItem, bool extra) const
{
    //auto begin = std::chrono::high_resolution_clock::now();
    GstDiscoverer *discoverer = gst_discoverer_new(GST_SECOND, NULL);
    GError *error = nullptr;
    GValue val = G_VALUE_INIT;
    std::string uri = "file://" + mediaItem.path();
    if (!discoverer) {
        LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "ERROR : Failed to create GstDiscover object");
        return false;
    }

    GstDiscovererInfo *discoverInfo = gst_discoverer_discover_uri(discoverer, uri.c_str(), &error);
    if (!discoverInfo) {
        LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "GStreamer discoverer failed on '%s' with '%s'", uri.c_str(), error->message);
        if (error)
            g_error_free(error);
        g_object_unref(discoverer);
        return false;
    }
    if (!extra) {
        GstDiscovererStreamInfo *streamInfo = gst_discoverer_info_get_stream_info(discoverInfo);
        if (GST_IS_DISCOVERER_VIDEO_INFO(streamInfo)) {
            GstDiscovererVideoInfo *video_info =
                        reinterpret_cast<GstDiscovererVideoInfo*>(streamInfo);

            mediaItem.setMeta(MediaItem::Meta::Width,
                        MediaItem::MetaData(gst_discoverer_video_info_get_width(video_info)));
            mediaItem.setMeta(MediaItem::Meta::Height,
                        MediaItem::MetaData(gst_discoverer_video_info_get_height(video_info)));
        }
        if (streamInfo)
            gst_discoverer_stream_info_unref(streamInfo);
    } else {
        const GstTagList *metaInfo = gst_discoverer_info_get_tags(discoverInfo);
        MediaItem::MetaData data;
        for (auto& tag : gstTagMap) {
            if (!!metaInfo && gst_tag_list_copy_value (&val, metaInfo, tag.second.c_str())) {
                // these are the value types that are currently supported
                if (G_VALUE_HOLDS_STRING (&val)) {
                    data = {g_value_get_string(&val)};
                } else if (G_VALUE_HOLDS_UINT64 (&val)) {
                    // we can only do int64_t with libpbnjson :-(
                    data = {std::int64_t(GST_TIME_AS_SECONDS(g_value_get_uint64(&val)))};
                } else if (G_VALUE_HOLDS_DOUBLE (&val)) {
                    data = {g_value_get_double(&val)};
                } else if (GST_VALUE_HOLDS_DATE_TIME (&val)) {
                    GstDateTime *dateTime = nullptr;
                    auto ret = gst_tag_list_get_date_time(metaInfo, tag.second.c_str(), &dateTime);
                    if (ret) {
                        data = {gst_date_time_to_iso8601_string(dateTime)};
                        gst_date_time_unref(dateTime);
                    }
                } else {
                    data = {std::string("")};
                }
                mediaItem.setMeta(tag.first, data);
            }
        }
    }
    //auto end = std::chrono::high_resolution_clock::now();
    //auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
    //LOG_DEBUG(MEDIA_INDEXER_IMAGEEXTRACTOR, "elapsed time = %d", (int)(elapsedTime.count()));
    if (error)
        g_error_free(error);
    if (discoverInfo)
        g_object_unref(discoverInfo);
    g_object_unref(discoverer);
    return true;

}


bool ImageExtractor::getExifData(MediaItem &mediaItem) const
{
    auto fname = mediaItem.path().c_str();
    exifData_ = exif_data_new_from_file(fname);

    if (exifData_)
        return true;
    return false;
}

void ImageExtractor::resetExifData() const
{
    if (exifData_) {
        exif_data_unref(exifData_);
        exifData_ = nullptr;
    }
}

void ImageExtractor::setMetaFromExifMap(MediaItem &mediaItem, bool extra) const
{
    MediaItem::MetaData data;
    char buf[1024] = {0, };
    std::vector<MediaItem::Meta> &flagMap = (!extra ? basicFlag_ : extraFlag_);
    for (auto flag : flagMap) {
        if (exifMap_.find(flag) != exifMap_.end()) {
            auto exifMap = exifMap_[flag];
            std::string retVal = std::string();
            for (auto tag : exifMap.tag) {
                auto entry = exif_content_get_entry(exifData_->ifd[exifMap.ifd], tag);
                if (entry) {
                    exif_entry_get_value(entry, buf, sizeof(buf));
                }
                if (retVal.empty())
                    retVal = std::string(buf);
                else
                    retVal += std::string(" ") + std::string(buf);
            }
            if (!extra) {
                if (!retVal.empty())
                    data = {std::stoi(retVal)};
                else
                    data = {0};
            } else
                data = {retVal};
        } else {
            data = {std::string("")};
        }
        mediaItem.setMeta(flag, data);
    }
    resetExifData();
}

void ImageExtractor::setMetaFromExif(MediaItem &mediaItem, bool extra) const
{
    setMetaFromExifMap(mediaItem, extra);
}

void ImageExtractor::setMeta(MediaItem &mediaItem, bool extra) const
{
    if (!extra) {
        auto ext = mediaItem.ext();
        if(resolutionHadler_.find(ext) != resolutionHadler_.end())
            resolutionHadler_[ext](mediaItem, (void *)(this));
        else
            setDefaultMeta(mediaItem, false);
    } else {
        if (getExifData(mediaItem))
            setMetaFromExif(mediaItem, true);
        else
            setDefaultMeta(mediaItem, true);
    }
}

bool ImageExtractor::extractMeta(MediaItem &mediaItem, bool extra) const
{
    std::string uri(mediaItem.path());
    if (mediaItem.type() != MediaItem::Type::Image) {
        LOG_ERROR(MEDIA_INDEXER_IMAGEEXTRACTOR, 0, "mediaitem type is not image");
        return false;
    }
    LOG_DEBUG(MEDIA_INDEXER_IMAGEEXTRACTOR, "Extract meta data from '%s' (%s)b",
        uri.c_str(), MediaItem::mediaTypeToString(mediaItem.type()).c_str());

    setMetaCommon(mediaItem);
    mediaItem.setMeta(MediaItem::Meta::Title, MediaItem::MetaData(baseFilename(mediaItem, true)));
    setMeta(mediaItem, extra);
    return true;
}

