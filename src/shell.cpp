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

#include "shell.h"
#include "mediaindexer.h"
#include "logging.h"

#include <iostream>

#include <editline/readline.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <sys/types.h>
#include <unistd.h>

void Shell::run(int  argc, char *argv[])
{
   char *line, *s;

   setlocale(LC_CTYPE, "");
   rl_readline_name = (char *) "Media-Indexer";

   stifle_history(100);

   // Read until quit.
   int stop = 0;
   while (!stop) {
       line = readline ("#> ");

       if (!line)
           break;

       s = stripwhite(line);

       if (*s) {
           char* expansion;
           int result;

           result = history_expand(s, &expansion);

           if (result < 0 || result == 2) {
               LOG_ERROR(MEDIA_INDEXER_MEDIAINDEXER, 0, "editline: %s", expansion);
           } else {
               add_history(expansion);
               stop = executeLine(expansion);
           }
           free(expansion);
       }

       free(line);
   }
}

char *Shell::stripwhite (char *str)
{
    char *s = str, *t;

    while (isspace(*s))
        s++;

    if (*s == '\0')
        return (s);

   t = s + strlen (s) - 1;
   while (t > s && isspace (*t))
      t--;
   *++t = '\0';

   return s;
}

void Shell::printHelp(void)
{
    std::cout << "usage:" << std::endl;
    std::cout << "  get_plugin <uri>" << std::endl;
    std::cout << "  put_plugin <uri>" << std::endl;
    std::cout << "  run_detect" << std::endl;
    std::cout << "  stop_detect" << std::endl;
    std::cout << "  get_playback_uri" << std::endl;
    std::cout << "  quit" << std::endl;
}

void Shell::getPlugin(const char *uri)
{
    if (uri)
        MediaIndexer::instance()->get(std::string(uri));
}

void Shell::putPlugin(const char *uri)
{
    if (uri)
        MediaIndexer::instance()->put(std::string(uri));
}

void Shell::runDetect()
{
    MediaIndexer::instance()->setDetect(true);
}

void Shell::stopDetect()
{
    MediaIndexer::instance()->setDetect(false);
}

void Shell::getPlaybackUri(const char *uri)
{
    auto pbUri = MediaIndexer::instance()->getPlaybackUri(uri);
    if (pbUri)
        std::cout << "playback uri for '" << uri << "' is: " <<
            pbUri.value() << std::endl;
    else
        std::cout << "No playback uri found for '" << uri << std::endl;
}

int Shell::executeLine(char *exp)
{
    char *cmd = NULL, *arg = NULL;
    int ret = 0;

    Shell::parseLine(exp, &cmd, &arg);

    if (!strcmp(cmd, "help"))
        Shell::printHelp();
    else if (!strcmp(cmd, "get_plugin"))
        if (arg)
            Shell::getPlugin(arg);
        else
            Shell::getPlugin("");
    else if (!strcmp(cmd, "put_plugin"))
        Shell::putPlugin(arg);
    else if (!strcmp(cmd, "run_detect"))
        Shell::runDetect();
    else if (!strcmp(cmd, "stop_detect"))
        Shell::stopDetect();
    else if (!strcmp(cmd, "get_playback_uri"))
        Shell::getPlaybackUri(arg);
    else if (!strcmp(cmd, "quit"))
        ret = -1;

    if (cmd)
        free(cmd);
    if (arg)
        free(arg);

    return ret;
}

void Shell::parseLine(const char *exp, char **cmd, char **arg)
{
    *arg = nullptr;
    *cmd = strdup(exp);

    // check if an argument is available
    char *sep = strchr(*cmd, ' ');
    if (!sep)
        return;

    *arg = strdup(sep + 1);
    *sep = '\0';
}
