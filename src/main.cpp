// Copyright (c) 2018-2020 LG Electronics, Inc.
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

/**
 * \mainpage Documentation WebOS com.webos.service.mediaindexer.
 *
 * The mediaindexer service provides databases for available media
 * devices as well as the media items on the devices including meta
 * data.
 *
 * \li A UML class diagram of the service:
 * \startuml
 * !include ../../../uml/media-indexer.puml
 * \enduml
 *
 * \li An UML sequence diagram of device detection and media parsing
 * for the MSC (USB mass storage device) plugin:
 * \startuml
 * !include ../../../uml/msc-plugin.pseq
 * \enduml
 *
 * \li An UML sequence diagram of device detection and media parsing
 * for the UPnP (DLNA server) plugin:
 * \startuml
 * !include ../../../uml/upnp-plugin.pseq
 * \enduml
 *
 * \li A UML sequence diagram for resolving the media item uri to a
 * playback uri:
 * \startuml
 * !include ../../../uml/upnp-playback-uri.pseq
 * \enduml
 */

#include "mediaindexer.h"
#include "logging.h"

#include <glib.h>
#include <signal.h>

#if defined STANDALONE
#include <shell.h>
#endif

#if defined HAS_GSTREAMER
#include <gst/gst.h>
#endif

#include <chrono>
#include <thread>
#include <atomic>

static GMainLoop *mainLoop = nullptr;
static std::atomic<bool> terminating(false);

#if defined HAS_LUNA
// The Luna service name of this application
const char *lunaServiceId = "com.webos.service.mediaindexer";
#endif

static void signalHandler(int sigNum)
{
    if (terminating)
        return;

    terminating = true;
    LOG_WARNING(MEDIA_INDEXER_MAIN, 0, "Graceful shutdown");

#if defined HAS_LUNA
    if (mainLoop)
        g_main_loop_unref(mainLoop);
#endif

#if defined HAS_GSTREAMER
    gst_deinit();
#endif

    exit(sigNum);
}

int main(int argc, char *argv[])
{
    using namespace std::chrono_literals;

    // install signal handler
    //signal(SIGABRT, signalHandler);
    //signal(SIGINT, signalHandler);

    LOG_INFO(MEDIA_INDEXER_MAIN, 0, "//*****************************************//");
    LOG_INFO(MEDIA_INDEXER_MAIN, 0, "//                                         //");
    LOG_INFO(MEDIA_INDEXER_MAIN, 0, "//      Mediaindexer service started       //");
    LOG_INFO(MEDIA_INDEXER_MAIN, 0, "//                                         //");
    LOG_INFO(MEDIA_INDEXER_MAIN, 0, "//*****************************************//");

#if defined HAS_GSTREAMER
    gst_init(nullptr, nullptr);
#endif

    // we need the mainloop for the luna service and client as well as
    // for the GStreamer stuff
    LOG_DEBUG(MEDIA_INDEXER_MAIN, "Enable and configure glib mainloop");
    mainLoop = g_main_loop_new(NULL, false);

    LOG_INFO(MEDIA_INDEXER_MAIN, 0, "Enable media indexer service");
    MediaIndexer::init(mainLoop);
    MediaIndexer::instance();

#if defined STANDALONE
    Shell::run(argc, argv);
#endif

#if defined HAS_LUNA
    g_main_loop_run(mainLoop);
#endif

#if defined HAS_GSTREAMER
    gst_deinit();
#endif

    return 0;
}
