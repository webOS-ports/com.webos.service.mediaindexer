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

#if defined HAS_GSTREAMER
#include "gstreamerextractor.h"
#endif

#if defined HAS_TAGLIB
#include "taglibextractor.h"
#endif

std::unique_ptr<IMetaDataExtractor> IMetaDataExtractor::extractor(
    MediaItem::Type type, std::string &ext) {
#if defined HAS_TAGLIB
    if (type == MediaItem::Type::Audio && ext.compare(TAGLIB_EXT_MP3) == 0) {
        std::unique_ptr<IMetaDataExtractor>
            extractor(static_cast<IMetaDataExtractor *>(new TaglibExtractor()));
        return extractor;
    }
#endif
#if defined HAS_GSTREAMER
    std::unique_ptr<IMetaDataExtractor>
        extractor(static_cast<IMetaDataExtractor *>(new GStreamerExtractor()));
    return extractor;
#endif

    return nullptr;
}
