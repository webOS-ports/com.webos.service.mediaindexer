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

#include "cachemanager.h"

std::unique_ptr<CacheManager> CacheManager::instance_;

CacheManager *CacheManager::instance()
{
    if (!instance_.get())
        instance_.reset(new CacheManager());
    return instance_.get();
}

CacheManager::CacheManager()
{
    // nothing to be done here
    LOG_DEBUG(MEDIA_INDEXER_CACHEMANAGER, "CacheManager ctor!");
}

CacheManager::~CacheManager()
{
    LOG_DEBUG(MEDIA_INDEXER_CACHEMANAGER, "CacheManager dtor!");
    for (auto &cache : caches_)
        cache.second.reset();
    caches_.clear();
}

std::shared_ptr<Cache> CacheManager::createCache(const std::string& devUri, const std::string& uuid)
{
    std::lock_guard<std::mutex> lock(mutex_);
    /*
    auto iter = caches_.find(devUri);
    if (iter != caches_.end()) {
        return iter->second;
    }
    */
    createCacheDirectory(uuid);
    std::string cachePath = CACHE_DIRECTORY + uuid + std::string("/") + std::string(CACHE_JSONFILE);
    auto cache = std::make_shared<Cache>(cachePath);
    caches_.emplace(devUri, cache);
    return cache;
}

int CacheManager::totalSize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    int total = 0;
    for (const auto &cache : caches_) {
        auto pCache = cache.second.get();
        total += pCache->size();
    }
    return total;
}

bool CacheManager::generateCacheFile(const std::string& devUri,
                                     const std::shared_ptr<Cache>& cache)
{
    std::lock_guard<std::mutex> lock(mutex_);
    bool ret = cache->generateCacheFile();
    caches_[devUri].reset();
    caches_.erase(devUri);
    return ret;
}

std::shared_ptr<Cache> CacheManager::readCache(const std::string& devUri, const std::string& uuid)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string cachePath = CACHE_DIRECTORY + uuid + std::string("/") + std::string(CACHE_JSONFILE);
    auto cache = std::make_shared<Cache>(cachePath);
    bool ret = cache->readCache();
    if (!ret) {
        LOG_WARNING(MEDIA_INDEXER_CACHEMANAGER, 0, "Failed to read cache file!");
        return nullptr;
    }
    caches_.emplace(devUri, cache);
    return cache;
}

void CacheManager::resetCache(const std::string& path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = caches_.find(path);
    if (iter != caches_.end()) {
        auto cache = iter->second;
        cache->resetCache();
        cache.reset();
        caches_.erase(path);
    }
}

void CacheManager::resetAllCache()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &cache : caches_) {
        auto pCache = cache.second;
        pCache->resetCache();
        pCache.reset();
    }
    caches_.clear();
}

void CacheManager::createCacheDirectory(const std::string& uuid)
{
    std::error_code err;
    std::string cacheDir = CACHE_DIRECTORY + uuid;
    if (!std::filesystem::is_directory(cacheDir)) {
        if (!std::filesystem::create_directory(cacheDir, err)) {
            LOG_ERROR(MEDIA_INDEXER_CACHEMANAGER, 0, "Failed to create directory %s, error : %s",cacheDir.c_str(), err.message().c_str());
            LOG_DEBUG(MEDIA_INDEXER_CACHEMANAGER, "Retry with create_directories");
            if (!std::filesystem::create_directories(cacheDir, err)) {
                LOG_ERROR(MEDIA_INDEXER_CACHEMANAGER, 0, "Retry Failed, error : %s", err.message().c_str());
            }
        }
    }
}

void CacheManager::printAllCache() const
{
    LOG_DEBUG(MEDIA_INDEXER_CACHEMANAGER, "--------------<Caches>--------------");
    for (const auto &cache : caches_) {
        LOG_DEBUG(MEDIA_INDEXER_CACHEMANAGER, "<%s>", cache.first.c_str());
        auto pCache = cache.second.get();
        pCache->printCache();
    }
    LOG_DEBUG(MEDIA_INDEXER_CACHEMANAGER, "------------------------------------");
}
