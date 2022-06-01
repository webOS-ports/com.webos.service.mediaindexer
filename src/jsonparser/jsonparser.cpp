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

#include "jsonparser.h"

JSonParser::JSonParser(const char* message)
{
    pbnjson::JDomParser parser;

    if (!parser.parse(message)) {
        throw parser_error("JSON parsing failure");
    }

    _dom = parser.getDom();
    LOG_DEBUG(MEDIA_INDEXER_JSONPARSER, "JSON string is '%s'", _dom.stringify().c_str());
}

JSonParser::JSonParser(const pbnjson::JValue& value)
{
    if (!value.isObject()) {
        throw parser_error("value is not json object");
    }

    _dom = value;
    LOG_DEBUG(MEDIA_INDEXER_JSONPARSER, "JSON string is '%s'", _dom.stringify().c_str());
}
