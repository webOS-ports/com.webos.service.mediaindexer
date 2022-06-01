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

#pragma once

#include "dbconnector.h"

#include <functional>

/// Connector to com.webos.mediadb.
class SettingsDb : public DbConnector
{
public:
    /**
     * \brief Get settings database connector.
     *
     * \return Singleton object.
     */
    static SettingsDb *instance();

    virtual ~SettingsDb();

    /**
     * \brief Apply settings for specified uri.
     *
     * \param[in] uri The settings uri.
     */
    void applySettings(const std::string &uri);

    /**
     * \brief Set enable state for specified plugin.
     *
     * \param[in] uri The settings uri.
     * \param[in] enable The enable state.
     */
    void setEnable(const std::string &uri, bool enable);

    /**
     * \brief Db service response handler.
     *
     * \return Result of message processing.
     */
    virtual bool handleLunaResponse(LSMessage *msg);

    /**
     * \brief Db service response handler.
     *
     * \return Result of message processing.
     */
    virtual bool handleLunaResponseMetaData(LSMessage *msg);

protected:
    /// Singleton.
    SettingsDb();

private:
    /// Singleton object.
    static std::unique_ptr<SettingsDb> instance_;
};
