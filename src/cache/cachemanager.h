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

#include <mutex>
#include "cache.h"

/// alias
using Caches = std::unordered_map<std::string, std::shared_ptr<Cache>>;
using CacheMapIterator = CacheMap::iterator;

/// Cache manager class for performance improvement
class CacheManager
{
public:
    static CacheManager *instance();
    CacheManager();
    virtual ~CacheManager();
    std::shared_ptr<Cache> createCache(const std::string& devUri, const std::string& uuid);
    int totalSize();
    bool generateCacheFile(const std::string& devUri, const std::shared_ptr<Cache>& cache);
    std::shared_ptr<Cache> readCache(const std::string& devUri, const std::string& uuid);
    void resetCache(const std::string& path);
    void resetAllCache();
    void createCacheDirectory(const std::string& uuid);
    void printAllCache() const;

 private:
    /// Singleton
    CacheManager(std::string confPath);

    /// media item cache path
    std::string cachePath_;

    /// Singleton instance object.
    static std::unique_ptr<CacheManager> instance_;

    /// cache list for each device
    Caches caches_;

    /// mutex to prevent critical section
    std::mutex mutex_;
};
