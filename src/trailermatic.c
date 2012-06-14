/*
 * Copyright (C) 2008 Frank Aurich 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#define AM_DEFAULT_CONFIGFILE      "/etc/trailermatic.conf"
#define AM_DEFAULT_STATEFILE       ".trailermatic.state"
#define AM_DEFAULT_VERBOSE         P_MSG
#define AM_DEFAULT_NOFORK          0
#define AM_DEFAULT_MAXBUCKET       30

#define FROM_XML_FILE 0
#define TESTING 0

#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <getopt.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>     /* open */

#include "config_parser.h"
#include "downloads.h"
#include "feed_item.h"
#include "file.h"
#include "output.h"
#include "prowl.h"
#include "state.h"
#include "utils.h"
#include "version.h"
#include "web.h"
#include "xml_parser.h"

PRIVATE char AutoConfigFile[MAXPATHLEN + 1];
PRIVATE void session_free(auto_handle *as);

uint8_t closing = 0;
uint8_t nofork  = AM_DEFAULT_NOFORK;

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE void usage(void) {
  printf("usage: trailermatic [-fh] [-v level] [-l logfile] [-c file]\n"
    "\n"
    "Trailermatic %s\n"
    "\n"
    "  -f --nodeamon             Run in the foreground and log to stderr\n"
    "  -h --help                 Display this message\n"
    "  -v --verbose <level>      Set output level to <level> (default=1)\n"
    "  -c --configfile <path>    Path to configuration file\n"
    "  -o --once                 Quit Trailermatic after first check of RSS feeds\n"
    "  -l --logfile <file>       Log messages to <file>\n"
    "  -a --append-log           Don't overwrite logfile from a previous session"
    "\n", LONG_VERSION_STRING );
  exit(0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE void readargs(int argc, char ** argv, char **c_file, char** logfile, char **xmlfile,
                      uint8_t * nofork, uint8_t * verbose, uint8_t *once, uint8_t *append_log) {
  char optstr[] = "afhv:c:l:ox:";
  struct option longopts[] = {
    { "verbose",    required_argument, NULL, 'v' },
    { "nodaemon",   no_argument,       NULL, 'f' },
    { "help",       no_argument,       NULL, 'h' },
    { "configfile", required_argument, NULL, 'c' },
    { "once",       no_argument,       NULL, 'o' },
    { "logfile",    required_argument, NULL, 'l' },
    { "append-log", no_argument,       NULL, 'a' },
    { "xml",        required_argument, NULL, 'x' },
    { NULL, 0, NULL, 0 } };
  int opt;

  while (0 <= (opt = getopt_long(argc, argv, optstr, longopts, NULL ))) {
    switch (opt) {
      case 'a':
        *append_log = 1;
        break;
      case 'v':
        *verbose = atoi(optarg);
        break;
      case 'f':
        *nofork = 1;
        break;
      case 'c':
        *c_file = optarg;
        break;
      case 'l':
        *logfile = optarg;
        break;
      case 'x':
        *xmlfile = optarg;
        *nofork = 1;
        *once = 1;
        break;
      case 'o':
        *once = 1;
        break;
      default:
        usage();
        break;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE void shutdown_daemon(auto_handle *as) {
  dbg_printft(P_MSG, "Shutting down daemon");
  if (as && as->bucket_changed) {
    save_state(as->statefile, as->downloads);
  }

  session_free(as);
  SessionID_free();
  log_close();
  exit(EXIT_SUCCESS);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE int daemonize(void) {
  int fd;

  if (getppid() == 1) {
    return -1;
  }

  switch (fork()) {
    case 0:
      break;
    case -1:
      fprintf(stderr, "Error daemonizing (fork)! %d - %s", errno, strerror(
          errno));
      return -1;
    default:
      _exit(0);
  }

  umask(0); /* change the file mode mask */

  if (setsid() < 0) {
    fprintf(stderr, "Error daemonizing (setsid)! %d - %s", errno, strerror(
        errno));
    return -1;
  }

  switch (fork()) {
    case 0:
      break;
    case -1:
      fprintf(stderr, "Error daemonizing (fork2)! %d - %s\n", errno, strerror(
          errno));
      return -1;
    default:
      _exit(0);
  }

  fd = open("/dev/null", O_RDONLY);
  if (fd != 0) {
    dup2(fd, 0);
    close(fd);
  }

  fd = open("/dev/null", O_WRONLY);
  if (fd != 1) {
    dup2(fd, 1);
    close(fd);
  }

  fd = open("/dev/null", O_WRONLY);
  if (fd != 2) {
    dup2(fd, 2);
    close(fd);
  }
  return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE void signal_handler(int sig) {
  switch (sig) {
    case SIGINT:
    case SIGTERM: {
      dbg_printf(P_INFO2, "SIGTERM/SIGINT caught");
      closing = 1;
      break;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE void setup_signals(void) {
  signal(SIGCHLD, SIG_IGN);       /* ignore child       */
  signal(SIGTSTP, SIG_IGN);       /* ignore tty signals */
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTERM, signal_handler); /* catch kill signal */
  signal(SIGINT , signal_handler); /* catch kill signal */
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

/*
uint8_t am_get_verbose(void) {
  return verbose;
}*/

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

auto_handle* session_init(void) {
  char path[MAXPATHLEN];
  char *home;

  auto_handle *ses = am_malloc(sizeof(auto_handle));

  /* numbers */
  ses->max_bucket_items     = AM_DEFAULT_MAXBUCKET;
  ses->bucket_changed       = 0;
  ses->check_interval       = AM_DEFAULT_INTERVAL;

  /* strings */
  ses->download_folder        = get_temp_folder();
  home = get_home_folder();
  sprintf(path, "%s/%s", home, AM_DEFAULT_STATEFILE);
  am_free(home);
  ses->statefile             = am_strdup(path);
  ses->prowl_key             = NULL;
  ses->prowl_key_valid       = 0;

  /* lists */
  ses->filters               = NULL;
  ses->feeds                 = NULL;
  ses->downloads             = NULL;

  return ses;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE void session_free(auto_handle *as) {
  if (as) {
    am_free(as->download_folder);
    as->download_folder = NULL;
    am_free(as->statefile);
    as->statefile = NULL;
    am_free(as->prowl_key);
    as->prowl_key = NULL;
    freeList(&as->feeds, feed_free);
    freeList(&as->downloads, NULL);
    freeList(&as->filters, filter_free);
    am_free(as);
    as = NULL;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE void processRSSList(auto_handle *session, const simple_list items, uint16_t feedID) {

  simple_list current_item = items;
  simple_list current_url = NULL;
  am_filter filter = NULL;
  const char * url;
  char path[4096];
  HTTPResponse *response = NULL;

  while(current_item && current_item->data) {
    feed_item item = (feed_item)current_item->data;
    current_url = item->urls;
    while(current_url && current_url->data) {
      url = (char*)current_url->data;
      if(isMatch(session->filters, url, &filter)) {
        get_filename(path, NULL, url, session->download_folder);
        if (!has_been_downloaded(session->downloads, url) && !file_exists(path)) {
          dbg_printft(P_MSG, "[%d] Found new download: %s (%s)", feedID, item->name, url);
          response = downloadFile(url, path, filter->agent);
          if(response) {
            if(response->responseCode == 200) {
              if(session->prowl_key_valid) {
                prowl_sendNotification(PROWL_NEW_TRAILER, session->prowl_key, item->name);
              }

              dbg_printf(P_MSG, "  Download complete (%dMB) (%.2fkB/s)", response->size / 1024 / 1024, response->downloadSpeed / 1024);
              /* add url to bucket list */
              if (addToBucket(url, &session->downloads, session->max_bucket_items) == 0) {
                session->bucket_changed = 1;
                save_state(session->statefile, session->downloads);
              }
            } else {
              dbg_printf(P_ERROR, "  Error: Download failed (Error Code %d)", response->responseCode);
              if(session->prowl_key_valid) {
                prowl_sendNotification(PROWL_DOWNLOAD_FAILED, session->prowl_key, item->name);
              }
            }

            HTTPResponse_free(response);
          }
        } else {
          dbg_printf(P_MSG, "File downloaded previously: %s", basename(path));
        }
      }

      current_url = current_url->next;
    }

    current_item = current_item->next;
  }
}

PRIVATE HTTPResponse* getRSSFeed(const rss_feed* feed, CURL **session) {
  return getHTTPData(feed->url, feed->cookies, session);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE uint16_t processFeed(auto_handle *session, rss_feed* feed, uint8_t firstrun) {
  HTTPResponse *response = NULL;
  CURL         *curl_session = NULL;
  uint32_t item_count = 0;
  response = getRSSFeed(feed, &curl_session);
  dbg_printf(P_INFO2, "[processFeed] curl_session=%p", (void*)curl_session);

  if(!curl_session && response != NULL) {
    dbg_printf(P_ERROR, "curl_session == NULL but response != NULL");
    abort();
  }

  if (response) {
    if(response->responseCode == 200 && response->data) {
      simple_list items = parse_xmldata(response->data, response->size, &item_count, &feed->ttl);
      if(firstrun) {
        session->max_bucket_items += item_count;
        dbg_printf(P_INFO2, "History bucket size changed: %d", session->max_bucket_items);
      }
      processRSSList(session, items, feed->id);
      freeList(&items, freeFeedItem);
    }
    HTTPResponse_free(response);
    closeCURLSession(curl_session);
  }

  return item_count;
}

PRIVATE uint16_t processFile(auto_handle *session, const char* xmlfile) {
  uint32_t item_count = 0;
  char *xmldata = NULL;
  uint32_t fileLen = 0;
  uint32_t dummy_ttl;
  simple_list items;

  assert(xmlfile && *xmlfile);
  dbg_printf(P_INFO, "Reading RSS feed file: %s", xmlfile);
  xmldata = readFile(xmlfile, &fileLen);
  if(xmldata != NULL) {
    fileLen = strlen(xmldata);
    items = parse_xmldata(xmldata, fileLen, &item_count, &dummy_ttl);
    session->max_bucket_items += item_count;
    dbg_printf(P_INFO2, "History bucket size changed: %d", session->max_bucket_items);
    processRSSList(session, items, 0);
    freeList(&items, freeFeedItem);
    am_free(xmldata);
  }

  return item_count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv) {
  auto_handle *session = NULL;
  char *config_file = NULL;
  char *logfile = NULL;
  char *xmlfile = NULL;
  char erbuf[100];
  NODE *current = NULL;
  uint32_t count = 0;
  uint8_t first_run = 1;
  uint8_t once = 0;
  uint8_t verbose = AM_DEFAULT_VERBOSE;
  uint8_t append_log = 0;

  /* this sets the log level to the default before anything else is done.
  ** This way, if any outputting happens in readargs(), it'll be printed
  ** to stderr.
  */
  log_init(NULL, verbose, 0);

  readargs(argc, argv, &config_file, &logfile, &xmlfile, &nofork, &verbose, &once, &append_log);

  /* reinitialize the logging with the values from the command line */
  log_init(logfile, verbose, append_log);

  if(!config_file) {
    config_file = am_strdup(AM_DEFAULT_CONFIGFILE);
  }
  strncpy(AutoConfigFile, config_file, strlen(config_file));

  session = session_init();

  if(parse_config_file(session, AutoConfigFile) != 0) {
    if(errno == ENOENT) {
      snprintf(erbuf, sizeof(erbuf), "Cannot find file '%s'", config_file);
    } else {
      snprintf(erbuf, sizeof(erbuf), "Unknown error");
    }
    fprintf(stderr, "Error parsing config file: %s\n", erbuf);
    shutdown_daemon(session);
  }

  setup_signals();

  if(!nofork) {

    /* start daemon */
    if(daemonize() != 0) {
      dbg_printf(P_ERROR, "Error: Daemonize failed. Aborting...");
      shutdown_daemon(session);
    }

    dbg_printft( P_MSG, "Daemon started");
  }

  filter_printList(session->filters);

  dbg_printf(P_MSG, "Trailermatic version: %s", LONG_VERSION_STRING);
  dbg_printf(P_INFO, "verbose level: %d", verbose);
  dbg_printf(P_INFO, "foreground mode: %s", nofork == 1 ? "yes" : "no");
  dbg_printf(P_INFO, "config file: %s", AutoConfigFile);
  dbg_printf(P_INFO, "check interval: %d min", session->check_interval);
  dbg_printf(P_INFO, "download folder: %s", session->download_folder);
  dbg_printf(P_INFO, "state file: %s", session->statefile);
  dbg_printf(P_MSG,  "%d feed URLs", listCount(session->feeds));
  dbg_printf(P_MSG,  "Read %d filters from config file", listCount(session->filters));

  if(session->prowl_key) {
    dbg_printf(P_INFO, "Prowl API key: %s", session->prowl_key);
  }

  if(listCount(session->feeds) == 0) {
    dbg_printf(P_ERROR, "No feed URL specified in trailermatic.conf!\n");
    shutdown_daemon(session);
  }

  if(listCount(session->filters) == 0) {
    dbg_printf(P_ERROR, "No filters specified in trailermatic.conf!\n");
    shutdown_daemon(session);
  }

  /* check if Prowl API key is given, and if it is valid */
  if(session->prowl_key && verifyProwlAPIKey(session->prowl_key) ) { 
    session->prowl_key_valid = 1;
  }

  load_state(session->statefile, &session->downloads);
  while(!closing) {
    dbg_printft( P_INFO, "------ Checking for new trailers ------");
     if(xmlfile && *xmlfile) {
       processFile(session, xmlfile);
       once = 1;
    } else {
      current = session->feeds;
      count = 0;
      while(current && current->data) {
        ++count;
        dbg_printf(P_INFO2, "Checking feed %d ...", count);
        processFeed(session, current->data, first_run);
        current = current->next;
      }
      if(first_run) {
        dbg_printf(P_INFO2, "New bucket size: %d", session->max_bucket_items);
      }
      first_run = 0;
    }
    /* leave loop when program is only supposed to run once */
    if(once) {
      break;
    }
    sleep(session->check_interval * 60);
  }
  shutdown_daemon(session);
  return 0;
}

