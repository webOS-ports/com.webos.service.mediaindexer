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

#include "indexerserviceclientsmgrimpl.h"

IndexerServiceClientsMgrImpl::IndexerServiceClientsMgrImpl()
{
    //nothing to be done here
}

IndexerServiceClientsMgrImpl::~IndexerServiceClientsMgrImpl()
{
    clients_.clear();
}

bool IndexerServiceClientsMgrImpl::addClient(const std::string &sender,
                                             const std::string &method,
                                             const LSMessageToken &token)
{
    if (!isClientExist(sender, method, token)) {
        LOG_ERROR(MEDIA_INDEXER_INDEXERSERVICECLT, 0, "client already added: sender[%s] method[%s] token[%ld]",
                sender.c_str(), method.c_str(), token);
        return false;
    }
    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICECLT, "Client added: sender[%s] method[%s] token[%ld]",
            sender.c_str(), method.c_str(), token);
    auto client = std::make_pair(sender, method);
    clients_.emplace(token, client);
    return true;
}

bool IndexerServiceClientsMgrImpl::removeClient(const std::string &sender,
                                                const std::string &method,
                                                const LSMessageToken &token)
{
    const auto &it = clients_.find(token);
    if (it == clients_.end())
        return false;

    const auto &client = it->second;
    auto targetClient = std::make_pair(sender, method);

    if (client != targetClient) {
        LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICECLT, "Failed to remove: sender[%s]<->stored sender[%s] \
                method[%s]<->stored method[%s]", sender.c_str(), client.first.c_str(),
                method.c_str(), client.second.c_str());
        return false;
    }

    LOG_DEBUG(MEDIA_INDEXER_INDEXERSERVICECLT, "Client removed: sender[%s] method[%s] token[%ld]",
            client.first.c_str(), client.second.c_str(), token);
    clients_.erase(it);
    return true;
}

bool IndexerServiceClientsMgrImpl::isClientExist(const std::string &sender,
                                                 const std::string &method,
                                                 const LSMessageToken &token)
{
    const auto &it = clients_.find(token);
    if (it == clients_.end()) {
        return false;
    }

    const auto &client = it->second;
    auto targetClient = std::make_pair(sender, method);
    return (client == targetClient) ? true : false;
}
