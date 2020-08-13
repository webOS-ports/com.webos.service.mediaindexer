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

#include "dbconnector.h"

/// From main.cpp.
extern const char *lunaServiceId;

const char *DbConnector::dbUrl_ = "luna://com.webos.mediadb/";
LSHandle *DbConnector::lsHandle_ = nullptr;
std::string DbConnector::suffix_ = ":1";

void DbConnector::init(LSHandle * lsHandle)
{
    lsHandle_ = lsHandle;
}

DbConnector::DbConnector(const char *serviceName, bool async) :
    serviceName_(serviceName)
{
    kindId_ = serviceName_ + suffix_;

    // nothing to be done here
    connector_ = std::unique_ptr<LunaConnector>(new LunaConnector(serviceName_, async));

    if (!connector_)
        LOG_ERROR(0, "Failed to create lunaconnector object");

    connector_->registerTokenCallback(
        [this](LSMessageToken & token, const std::string & method, void *obj) -> void {
            rememberSessionData(token, method, obj);
        });
}

DbConnector::DbConnector()
{
    // nothing to be done here
}

DbConnector::~DbConnector()
{
    // nothing to be done here
    connector_.reset();
}

void DbConnector::ensureKind(const std::string &kind_name)
{
    LSMessageToken sessionToken;

    // ensure that kind exists
    std::string url = dbUrl_;
    url += "putKind";

    auto kind = pbnjson::Object();
    if (kind_name.empty())
    {
        kind.put("id", kindId_);
        kind.put("indexes", kindIndexes_);
    }
    else
    {
        kind.put("id", kind_name);
        kind.put("indexes", uriIndexes_);
    }

    kind.put("owner", serviceName_.c_str());

    LOG_INFO(0, "Ensure kind '%s'", kind_name.c_str());

    if (!connector_->sendMessage(url.c_str(), kind.stringify().c_str(),
            DbConnector::onLunaResponse, this, true, &sessionToken)) {
        LOG_ERROR(0, "Db service putKind error");
    }
}

bool DbConnector::mergePut(const std::string &uri, bool precise,
    pbnjson::JValue &props, void *obj, const std::string &kind_name, bool atomic)
{
    LSMessageToken sessionToken;
    bool async = !atomic;
    std::string url = dbUrl_;
    url += "mergePut";

    // query for matching uri
    auto query = pbnjson::Object();
    if (kind_name.empty())
        query.put("from", kindId_);
    else
        query.put("from", kind_name);

    auto where = pbnjson::Array();
    auto cond = pbnjson::Object();
    cond.put("prop", "uri");
    cond.put("op", precise ? "=" : "%");
    cond.put("val", uri);
    where << cond;
    query.put("where", where);

    auto request = pbnjson::Object();
    // set the kind property in case the query fails
    if (kind_name.empty())
        props.put("_kind", kindId_);
    else
        props.put("_kind", kind_name);

    request.put("props", props);
    request.put("query", query);

    LOG_INFO(0, "Send mergePut for '%s', request : '%s'", uri.c_str(), request.stringify().c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj)) {
        LOG_ERROR(0, "Db service mergePut error");
        return false;
    }

    return true;
}

bool DbConnector::merge(const std::string &kind_name, pbnjson::JValue &props,
    const std::string &whereProp, const std::string &whereVal, bool precise, void *obj, bool atomic)
{
    LSMessageToken sessionToken;
    bool async = !atomic;
    std::string url = dbUrl_;
    url += "merge";

    // query for matching uri
    auto query = pbnjson::Object();
    query.put("from", kind_name);

    auto where = pbnjson::Array();
    auto cond = pbnjson::Object();
    cond.put("prop", whereProp);
    cond.put("op", precise ? "=" : "%");
    cond.put("val", whereVal);
    where << cond;
    query.put("where", where);

    auto request = pbnjson::Object();
    // set the kind property in case the query fails
    props.put("_kind", kind_name);

    request.put("props", props);
    request.put("query", query);

    LOG_INFO(0, "Send merges for '%s', request : '%s'", whereVal.c_str(), request.stringify().c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj)) {
        LOG_ERROR(0, "Db service mergePut error");
        return false;
    }

    return true;
}

bool DbConnector::find(const std::string &uri, bool precise,
    void *obj, const std::string &kind_name, bool atomic)
{
    LSMessageToken sessionToken;
    bool async = !atomic;
    std::string url = dbUrl_;
    url += "find";

    // query for matching uri
    auto query = pbnjson::Object();
    if (kind_name.empty())
        query.put("from", kindId_);
    else
        query.put("from", kind_name);

    auto where = pbnjson::Array();
    auto cond = pbnjson::Object();
    cond.put("prop", "uri");
    cond.put("op", precise ? "=" : "%");
    cond.put("val", uri);
    where << cond;
    query.put("where", where);

    auto request = pbnjson::Object();
    request.put("query", query);

    LOG_INFO(0, "Send find for '%s'", uri.c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj)) {
        LOG_ERROR(0, "Db service find error");
        return false;
    }

    return true;
}

// TODO : Need refactoring
bool DbConnector::search(const std::string &kind_name, pbnjson::JValue &selects,
    const std::string &prop, const std::string &val, bool precise, void *obj, bool atomic)
{
    LSMessageToken sessionToken;
    bool async = !atomic;
    std::string url = dbUrl_;
    url += "search";

    // query for matching uri
    auto query = pbnjson::Object();
    query.put("select", selects);
    query.put("from", kind_name);
    auto where = pbnjson::Array();
    auto cond = pbnjson::Object();
    cond.put("prop", prop);
    cond.put("op", precise ? "=" : "%");
    cond.put("val", val);
    where << cond;
    query.put("where", where);

    auto request = pbnjson::Object();
    request.put("query", query);
    
    LOG_INFO(0, "Send search for '%s' : '%s'", prop.c_str(), val.c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj)) {
        LOG_ERROR(0, "Db service search error");
        return false;
    }

    return true;
}
bool DbConnector::del(const std::string &uri, bool precise, const std::string &kind_name)
{
    LSMessageToken sessionToken;

    std::string url = dbUrl_;
    url += "del";

    // query for matching uri
    auto query = pbnjson::Object();
    if (kind_name.empty())
        query.put("from", kindId_);
    else
        query.put("from", kind_name);

    auto where = pbnjson::Array();
    auto cond = pbnjson::Object();
    cond.put("prop", "uri");
    cond.put("op", precise ? "=" : "%");
    cond.put("val", uri);
    where << cond;
    query.put("where", where);

    auto request = pbnjson::Object();
    request.put("query", query);

    LOG_INFO(0, "Send delete for '%s'", uri.c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, true, &sessionToken)) {
        LOG_ERROR(0, "Db service delete error");
        return false;
    }
    return true;
}

bool DbConnector::roAccess(std::list<std::string> &services)
{
    if (!lsHandle_) {
        LOG_CRITICAL(0, "Luna bus handle not set");
        return false;
    }

    LSError lsError;
    LSErrorInit(&lsError);
    LSMessageToken sessionToken;

    std::string url = dbUrl_;
    url += "putPermissions";

    auto permissions = pbnjson::Array();
    for (auto s : services) {
        auto perm = pbnjson::Object();
        auto oper = pbnjson::Object();
        oper.put("read", "allow");
        perm.put("operations", oper);
        perm.put("object", kindId_);
        perm.put("type", "db.kind");
        perm.put("caller", s);

        permissions << perm;
    }

    auto request = pbnjson::Object();
    request.put("permissions", permissions);

    LOG_INFO(0, "Send putPermissions");
    LOG_DEBUG("Request : %s", request.stringify().c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, true, &sessionToken)) {
        LOG_ERROR(0, "Db service permissions error");
        return false;
    }

    return true;
}

bool DbConnector::roAccess(std::list<std::string> &services, std::list<std::string> &kinds, void *obj, bool atomic)
{
    if (!lsHandle_) {
        LOG_CRITICAL(0, "Luna bus handle not set");
        return false;
    }

    LSError lsError;
    LSErrorInit(&lsError);
    LSMessageToken sessionToken;
    bool async = !atomic;
    std::string url = dbUrl_;
    url += "putPermissions";

    auto permissions = pbnjson::Array();
    for (auto s : services) {
        for (auto k : kinds) {
            auto perm = pbnjson::Object();
            auto oper = pbnjson::Object();
            oper.put("read", "allow");
            perm.put("operations", oper);
            perm.put("object", k);
            perm.put("type", "db.kind");
            perm.put("caller", s);

            permissions << perm;
        }
    }

    auto request = pbnjson::Object();
    request.put("permissions", permissions);

    LOG_INFO(0, "Send putPermissions");
    LOG_DEBUG("Request : %s", request.stringify().c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj)) {
        LOG_ERROR(0, "Db service permissions error");
        return false;
    }

    return true;
}


void DbConnector::putRespObject(bool returnValue, pbnjson::JValue & obj,
                const int& errorCode,
                const std::string& errorText)
{
    obj.put("returnValue", returnValue);
    obj.put("errorCode", errorCode);
    obj.put("errorText", errorText);
}

bool DbConnector::sendResponse(LSHandle *sender, LSMessage* message, const std::string &object)
{
    if (!connector_)
        return false;

    return connector_->sendResponse(sender, message, object);
}

bool DbConnector::sessionDataFromToken(LSMessageToken token, SessionData *sd)
{
    std::lock_guard<std::mutex> lock(lock_);

    auto match = messageMap_.find(token);
    if (match == messageMap_.end())
        return false;

    *sd = match->second;
    messageMap_.erase(match);
    return true;
}

bool DbConnector::onLunaResponse(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    DbConnector *connector = static_cast<DbConnector *>(ctx);
    LOG_DEBUG("onLunaResponse");
    return connector->handleLunaResponse(msg);
}

void DbConnector::rememberSessionData(LSMessageToken token,
    const std::string &method, void *object)
{
    // remember token for response - we could do that after the
    // request has been issued because the response will happen
    // from the mainloop in the same thread context
    LOG_DEBUG("Save method %s, token %ld pair", method.c_str(), (long)token);
    std::lock_guard<std::mutex> lock(lock_);
    SessionData sd;
    sd.method = method;
    sd.object = object;
    auto p = std::make_pair(token, sd);


    messageMap_.emplace(p);
}
