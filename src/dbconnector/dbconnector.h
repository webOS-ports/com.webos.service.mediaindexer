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

#include "logging.h"
#include "performancechecker.h"
#include "lunaconnector.h"
#include <luna-service2/lunaservice.h>
#include <pbnjson.hpp>

#include <string>
#include <memory>
#include <mutex>
#include <thread>
#include <map>
#include <list>

#define FLUSH_COUNT 100

enum SessionHdlType {
    HDL_DEFAULT = 0,
    HDL_LUNA_CONN,
    HDL_MAX
};

/// Connector to com.webos.mediadb.
class DbConnector
{
public:
    /**
     * \brief Configure luna service handle.
     *
     * This should be called before any object tries to request device
     * state change notifications.
     *
     * \param[in] lsHandle Luna service handle to use.
     */
    static void init(LSHandle *lsHandle);


    virtual void putRespObject(bool returnValue,
                                   pbnjson::JValue & obj,
                                   const int& errorCode = 0,
                                   const std::string& errorText = "No Error"
                                   );

    virtual bool sendResponse(LSHandle *sender, LSMessage* message,
                           const std::string &object);

protected:
    /// Session data attached to each luna request
    struct SessionData {
        /// A method name to identify the action.
        std::string dbServiceMethod;
        /// A request mediaDB method name.
        std::string dbMethod;
        /// A request query
        pbnjson::JValue query;
        /// Some arbitrary object.
        void *object;
    };

    virtual ~DbConnector();

    /**
     * \brief Db service response handler.
     *
     * \return Result of message processing.
     */
    virtual bool handleLunaResponse(LSMessage *msg) = 0;

    /**
     * \brief Db service response handler for meta data.
     *
     * \return Result of message processing.
     */
    virtual bool handleLunaResponseMetaData(LSMessage *msg) = 0;

    /**
     * \brief Send mergePut request with uri.
     *
     * The _kind property will be added to the props from this
     * method. The method will also set the query to search for
     * matching uris.
     *
     * \param[in] uri The object uri.
     * \param[in] precise Make precise uri match or not.
     * \param[in] props JSON object with the properties to be updated.
     * \param[in] obj Some object to send with the luna request.
     * \param[in] kind_name kind id.
     * \return True on success, false on error.
     */
    virtual bool mergePut(const std::string &uri, bool precise,
        pbnjson::JValue &props, void *obj = nullptr, const std::string &kind_name = "", bool atomic = false);

    virtual bool merge(const std::string &kind_name, pbnjson::JValue &props,
        const std::string &whereProp, const std::string &whereVal, bool precise = true, void *obj = nullptr, bool atomic = false, std::string method = std::string());

    virtual bool put(pbnjson::JValue &props, void *obj = nullptr, bool atomic = false, std::string method = std::string());
    /**
     * \brief Send find request with uri.
     *
     * \param[in] uri The object uri.
     * \param[in] precise Find precise matches or 'starts with'.
     * \param[in] obj Some object to send with the luna request.
     * \param[in] kind_name kind id.
     * \return True on success, false on error.
     */
    virtual bool find(const std::string &uri, bool precise = true,
        void *obj = nullptr, const std::string &kind_name = "", bool atomic = false);

    /**
     * \brief Send batch request with multiple database operations.(merge, put, find, get, del)
     *
     * \param[in] operations   The list of database operation to perform.
     * \param[in] dbMethod     Caller method.
     * \param[in] obj          Some object to send with the luna request.
     * \return                 True on success, false on error.
     */
    virtual bool batch(pbnjson::JValue &operations, const std::string &dbMethod, void *obj = nullptr, bool atomic = false);

    /**
     * \brief Send search request with uri.
     *
     * \param[in] kind_name kind id.
     * \param[in] selects kind column.
     * \param[in] where JSON object with the properties to search.
     * \param[in] filter JSON object with the properties to search.
     * \param[in] obj Some object to send with the luna request.
     * \param[in] atomic Sync/Async.
     * \return True on success, false on error.
     */
//    virtual bool search(const std::string &kind_name, pbnjson::JValue &selects,
//                        pbnjson::JValue &where, pbnjson::JValue &filter,
//                        void *obj = nullptr, bool atomic = false,
//                        const std::string &method = std::string(),
//                        int count = 0, const std::string &page = std::string());
      virtual bool search(pbnjson::JValue &query, const std::string &dbMethod,
                          void *obj = nullptr);
    /**
     * \brief Delete all objects with the given uri by JSON object.
     *
     * \param[in] kind_name kind id.
     * \param[in] where JSON object with the properties to delete.
     * \param[in] obj Some object to send with the luna request.
     * \param[in] atomic Sync/Async.
     * \return True on success, false on error.
     */
    virtual bool del(pbnjson::JValue &query, const std::string &dbMethod,
                     void *obj = nullptr);

    /**
     * \brief Give read only access to other services.
     *
     * \param[in] services List of service names.
     * \return True on success, false on error.
     */
    virtual bool roAccess(std::list<std::string> &services);

    /**
     * \brief Give read only access to other services.
     *
     * \param[in] services List of service names.
     * \param[in] kind List of service names.
     * \param[in] obj user data.
     * \return True on success, false on error.
     */
    virtual bool roAccess(std::list<std::string> &services,
                          std::list<std::string> &kinds, void *obj = nullptr,
                          bool atomic = false, const std::string &forcemethod = "");

    /// Each specific database connection will be a singleton.
    DbConnector(const char *kindId, bool async = false);
    DbConnector();

    /// Ensure database kind.
    virtual void ensureKind(const std::string &kind_name = "");

    /// Should be set from connector class constructor
    std::string kindId_;

    ///service name to send message to db8 service
    std::string serviceName_;

    /// Should be set from connector class constructor, gives us the
    /// indexes for kind creation
    pbnjson::JArray kindIndexes_;
    pbnjson::JArray uriIndexes_;

    /// Get message token to classify response and get attached data.
    bool sessionDataFromToken(LSMessageToken token, SessionData *sd, SessionHdlType hdlType = HDL_DEFAULT);

private:

    /// Do not use.
    //DbConnector();

    /// Db service url.
    static const char *dbUrl_;
    /// suffix to service name to make kind id
    static std::string suffix_;

    /// Luna service handle.
    static LSHandle *lsHandle_;

    /// Luna connector handle.
    std::unique_ptr<LunaConnector> connector_;

    /// Map of luna service message tokens and the method along with
    /// some call specific user data.
    /// messageMap_[0] : for handler using luna connector(SessionHdrType : HDL_DEFAULT)
    /// messageMap_[1] : for handler not using luna connector(SessionHdrType : HDL_LUNA_CONN)
    std::map<LSMessageToken, DbConnector::SessionData> messageMap_[HDL_MAX];

    /// Callback for luna responses.
    static bool onLunaResponse(LSHandle *lsHandle, LSMessage *msg, void *ctx);

    /// Callback for luna responses.
    // TODO this response will be merged above callback.
    static bool onLunaResponseMetaData(LSHandle *lsHandle, LSMessage *msg, void *ctx);

    /// Needed for the session data map.
    mutable std::mutex lock_;

    /// Remember session data.
    void rememberSessionData(LSMessageToken token,
                             const std::string &dbServiceMethod,
                             const std::string &dbMethod,
                             pbnjson::JValue &query,
                             void *object,
                             SessionHdlType hdlType = HDL_DEFAULT);
};
