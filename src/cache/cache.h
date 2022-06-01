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

#include "mediaitem.h"
#include <pbnjson.hpp>
#include <unordered_map>
#include <tuple>
#include <utility>

/// alias
using CacheMap = std::unordered_map<std::string, 
                                    std::tuple<unsigned long, MediaItem::Type, std::string>>;
using CacheMapIterator = CacheMap::iterator;

/// Cache class based on media uri and hash
class Cache
{
public:
    Cache(const std::string& path);
    ~Cache();

    void insertItem(const std::string& uri, const unsigned long& hash,
                    const MediaItem::Type& type, const std::string& thumbnailFile);
    int size() const;
    const std::string& getPath() const;
    bool setPath(const std::string& path);
    bool generateCacheFile();
    bool readCache();
    bool isExist(const std::string& uri, const unsigned long& hash);
    void resetCache();
    void clear();
    void printCache() const;
    const CacheMap& getRemainingCache() const;

 private:
    /// cacheMap
    CacheMap cacheMap_;

    /// cacheList for generate Cache file
    CacheMap cacheItems_;

    /// media item cache path
    std::string cachePath_;
};
