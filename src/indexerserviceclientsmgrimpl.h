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

#ifndef INDEXER_SERVICE_CLIENTS_MGR_IMPL_H
#define INDEXER_SERVICE_CLIENTS_MGR_IMPL_H

#include <map>
#include <utility>
#include "logging.h"
#include "indexerserviceclientsmgr.h"

class IndexerServiceClientsMgrImpl : public IndexerServiceClientsMgr
{
public:
    IndexerServiceClientsMgrImpl();
    ~IndexerServiceClientsMgrImpl();

    bool addClient(const std::string &sender,
                   const std::string &method,
                   const LSMessageToken& token);

    bool removeClient(const std::string &sender,
                      const std::string &method,
                      const LSMessageToken& token);

    bool isClientExist(const std::string &sender,
                       const std::string &method,
                       const LSMessageToken& token);
private:
    std::map<LSMessageToken, std::pair<std::string, std::string>> clients_;

};

#endif /* INDEXER_SERVICE_CLIENTS_MGR_IMPL_H */

