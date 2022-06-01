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
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Failed to create lunaconnector object");

    connector_->registerTokenCallback(
        [this](LSMessageToken & token, const std::string &dbServiceMethod,
               const std::string &dbMethod, void *obj) -> void {
            auto query = pbnjson::Object();
            rememberSessionData(token, dbServiceMethod, dbMethod, query, obj, HDL_LUNA_CONN);
        });

    connector_->registerTokenCancelCallback(
        [this](LSMessageToken & token, void *obj) -> void {
            if (!sessionDataFromToken(token, static_cast<SessionData*>(obj), HDL_LUNA_CONN)) {
                LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Failed in sessionDataFromToken for token %ld", (long)token);
            }
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
    LSError lsError;
    LSErrorInit(&lsError);
    LSMessageToken sessionToken;

    // ensure that kind exists
    std::string url = dbUrl_;
    url += "putKind";

    auto kind = pbnjson::Object();
    if (kind_name.empty()) {
        kind.put("id", kindId_);
    } else {
        kind.put("id", kind_name);
    }
    kind.put("indexes", kindIndexes_);
    kind.put("owner", serviceName_.c_str());

    LOG_INFO(MEDIA_INDEXER_DBCONNECTOR, 0, "Ensure kind '%s' or '%s'", kind_name.c_str(), kindId_.c_str());
/*
    if (!LSCall(lsHandle_, url.c_str(), kind.stringify().c_str(),
                DbConnector::onLunaResponse2, this, &sessionToken, &lsError)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "db service putKind error");
    }
*/
    if (!connector_->sendMessage(url.c_str(), kind.stringify().c_str(),
            DbConnector::onLunaResponse, this, true, &sessionToken)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Db service putKind error");
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

    LOG_INFO(MEDIA_INDEXER_DBCONNECTOR, 0, "Send mergePut for '%s', request : '%s'", uri.c_str(), request.stringify().c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Db service mergePut error");
        return false;
    }

    return true;
}

bool DbConnector::merge(const std::string &kind_name, pbnjson::JValue &props,
    const std::string &whereProp, const std::string &whereVal, bool precise, void *obj, bool atomic, std::string method)
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

    LOG_INFO(MEDIA_INDEXER_DBCONNECTOR, 0, "Send merges for '%s', request : '%s'", whereVal.c_str(), request.stringify().c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj, method)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Db service mergePut error");
        return false;
    }

    return true;
}

bool DbConnector::put(pbnjson::JValue &props, void *obj, bool atomic, std::string method)
{
    LSMessageToken sessionToken;
    bool async = !atomic;
    std::string url = dbUrl_;
    url += "put";

    auto request = pbnjson::Object();//props;
    request.put("objects", props);

    //LOG_DEBUG(MEDIA_INDEXER_DBCONNECTOR, "Send put for '%s', request : '%s'", uri.c_str(), request.stringify().c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj, method)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Db service put error");
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

    LOG_INFO(MEDIA_INDEXER_DBCONNECTOR, 0, "Send find for '%s'", uri.c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Db service find error");
        return false;
    }

    return true;
}

bool DbConnector::batch(pbnjson::JValue &operations, const std::string &dbMethod, void *obj, bool atomic)
{

    LSMessageToken sessionToken;
    bool async = !atomic;
    std::string url = dbUrl_;
    url += "batch";

    auto request = pbnjson::Object();
    request.put("operations", operations);

    LOG_INFO(MEDIA_INDEXER_DBCONNECTOR, 0, "Send batch for '%s'", dbMethod.c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj, dbMethod)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Db service batch error");
        return false;
    }

    return true;
}

bool DbConnector::search(pbnjson::JValue &query, const std::string &dbMethod, void *obj)
{
    LSError lsError;
    LSErrorInit(&lsError);
    LSMessageToken sessionToken;

    std::string url = dbUrl_;
    std::string dbServiceMethod = std::string("search");
    url += dbServiceMethod;

    auto request = pbnjson::Object();
    request.put("query", query);

    if (!LSCall(lsHandle_, url.c_str(), request.stringify().c_str(),
                DbConnector::onLunaResponseMetaData, this, &sessionToken, &lsError)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Db service search error");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        return false;
    }

    rememberSessionData(sessionToken, dbServiceMethod, dbMethod, query, obj);

    return true;
}

bool DbConnector::del(pbnjson::JValue &query, const std::string &dbMethod, void *obj)
{
    LSError lsError;
    LSErrorInit(&lsError);
    LSMessageToken sessionToken;
    std::string url = dbUrl_;
    std::string dbServiceMethod = std::string("del");
    url += dbServiceMethod;

    auto request = pbnjson::Object();
    request.put("query", query);
    if (!LSCall(lsHandle_, url.c_str(), request.stringify().c_str(),
                DbConnector::onLunaResponseMetaData, this, &sessionToken, &lsError)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Db service del error");
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
        return false;
    }

    rememberSessionData(sessionToken, dbServiceMethod, dbMethod, query, obj);

    return true;
}

bool DbConnector::roAccess(std::list<std::string> &services)
{
    if (!lsHandle_) {
        LOG_CRITICAL(MEDIA_INDEXER_DBCONNECTOR, 0, "Luna bus handle not set");
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

    LOG_INFO(MEDIA_INDEXER_DBCONNECTOR, 0, "Send putPermissions");
    LOG_DEBUG(MEDIA_INDEXER_DBCONNECTOR, "Request : %s", request.stringify().c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, true, &sessionToken)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Db service permissions error");
        return false;
    }

    return true;
}

bool DbConnector::roAccess(std::list<std::string> &services, std::list<std::string> &kinds, 
                                void *obj, bool atomic, const std::string &forcemethod)
{
    if (!lsHandle_) {
        LOG_CRITICAL(MEDIA_INDEXER_DBCONNECTOR, 0, "Luna bus handle not set");
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
            oper.put("delete", "allow");
            oper.put("update", "allow");
            perm.put("operations", oper);
            perm.put("object", k);
            perm.put("type", "db.kind");
            perm.put("caller", s);

            permissions << perm;
        }
    }

    auto request = pbnjson::Object();
    request.put("permissions", permissions);

    LOG_INFO(MEDIA_INDEXER_DBCONNECTOR, 0, "Send putPermissions");
    LOG_DEBUG(MEDIA_INDEXER_DBCONNECTOR, "Request : %s", request.stringify().c_str());

    if (!connector_->sendMessage(url.c_str(), request.stringify().c_str(),
            DbConnector::onLunaResponse, this, async, &sessionToken, obj, forcemethod)) {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Db service permissions error");
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

bool DbConnector::sessionDataFromToken(LSMessageToken token, SessionData *sd,
                                SessionHdlType hdlType)
{
    std::lock_guard<std::mutex> lock(lock_);

    auto match = messageMap_[hdlType].find(token);
    if (match == messageMap_[hdlType].end())
        return false;
    if (sd)
        *sd = match->second;
    else {
        LOG_ERROR(MEDIA_INDEXER_DBCONNECTOR, 0, "Invalid SessionData");
        return false;
    }
    messageMap_[hdlType].erase(match);
    return true;
}

bool DbConnector::onLunaResponse(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    DbConnector *connector = static_cast<DbConnector *>(ctx);
    LOG_DEBUG(MEDIA_INDEXER_DBCONNECTOR, "onLunaResponse");
    return connector->handleLunaResponse(msg);
}

bool DbConnector::onLunaResponseMetaData(LSHandle *lsHandle, LSMessage *msg, void *ctx)
{
    DbConnector *connector = static_cast<DbConnector *>(ctx);
    return connector->handleLunaResponseMetaData(msg);
}

void DbConnector::rememberSessionData(LSMessageToken token,
                                      const std::string &dbServiceMethod,
                                      const std::string &dbMethod,
                                      pbnjson::JValue &query,
                                      void *object,
                                      SessionHdlType hdlType)
{
    // remember token for response - we could do that after the
    // request has been issued because the response will happen
    // from the mainloop in the same thread context
    LOG_DEBUG(MEDIA_INDEXER_DBCONNECTOR, "Save dbServiceMethod %s, dbMethod %s, token %ld pair", dbServiceMethod.c_str(), dbMethod.c_str(), (long)token);
    SessionData sd = {dbServiceMethod, dbMethod, query, object};
    auto p = std::make_pair(token, sd);
    std::lock_guard<std::mutex> lock(lock_);
    messageMap_[hdlType].emplace(p);
}
