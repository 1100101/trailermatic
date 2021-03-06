/* $Id$
* $Name$
* $ProjectName$
*/

/**
* @file web.c
*
* Provides basic functionality for communicating with HTTP and FTP servers.
*/

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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <curl/curl.h>
#include <stdint.h>

#include "web.h"
#include "output.h"
#include "regex.h"
#include "urlcode.h"
#include "utils.h"

/** \cond */
#define DATA_BUFFER_SIZE 1024 * 100
#define HEADER_BUFFER 500
/** \endcond */

PRIVATE char *gSessionID = NULL;

PRIVATE uint8_t gbGlobalInitDone = FALSE;

/** Generic struct storing data and the size of the contained data */
typedef struct HTTPData {
 char   *data;  /**< Stored data */
 size_t  buffer_size;
 size_t  buffer_pos;  /**< Size of the stored data */
} HTTPData;

/** Struct storing information about data downloaded from the web */
typedef struct WebData {
  char      *url;              /**< URL of the WebData object */
  long       responseCode;     /**< HTTP response code        */
  size_t     content_length;   /**< size of the received data determined through header field "Content-Length" */
  char      *content_filename; /**< name of the downloaded file determined through header field "Content-Length" */
  HTTPData  *response;         /**< HTTP response in a HTTPData object */
} WebData;


PUBLIC void SessionID_free(void) {
   am_free(gSessionID);
   gSessionID = NULL;
}

PRIVATE size_t write_header_callback(void *ptr, size_t size, size_t nmemb, void *data) {
  size_t       line_len = size * nmemb;
  WebData     *mem  = (WebData*)data;
  const char  *line = (const char*)ptr;
  char        *tmp  = NULL;
  char        *filename = NULL;
  const char  *content_pattern = "Content-Disposition:\\s(inline|attachment);\\s+filename=\"?(.+?)\"?;?\\r?\\n?$";
  int          content_length = 0;
  static uint8_t isMoveHeader = 0;

  /* check the header if it is a redirection header */
  if(line_len >= 9 && !memcmp(line, "Location:", 9)) {
    isMoveHeader = 1;
    if(mem->response->data != NULL) {
      am_free(mem->response->data);
      mem->content_length = 0;
    }
  } else if(line_len >= 15 && !memcmp(line, "Content-Length:", 15)) {
  /* parse header for Content-Length to allocate correct size for data->response->data */
    tmp = getRegExMatch("Content-Length:\\s(\\d+)", line, 1);
    if(tmp != NULL) {
      dbg_printf(P_INFO2, "Content-Length: %s", tmp);
      content_length = atoi(tmp);
      if(content_length > 0 && !isMoveHeader) {
        mem->content_length = content_length;
        mem->response->buffer_size = content_length + 1;
        mem->response->data = am_realloc(mem->response->data, mem->response->buffer_size);
      }
      am_free(tmp);
    }
  } else if(line_len >= 19 && !memcmp(line, "Content-Disposition", 19)) {
    /* parse header for Content-Disposition to get correct filename */
    filename = getRegExMatch(content_pattern, line, 2);
    if(filename) {
      mem->content_filename = filename;
      dbg_printf(P_INFO2, "[write_header_callback] Found filename: %s", mem->content_filename);
    }
  } else if(line_len >= 2 && !memcmp(line, "\r\n", 2)) {
    /* We're at the end of a header, reaset the relocation flag */
    isMoveHeader = 0;
  }

  return line_len;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE size_t write_data_callback(void *ptr, size_t size, size_t nmemb, void *data) {
  size_t line_len = size * nmemb;
  WebData *mem = data;

  /**
   * if content-length detection in write_header_callback was not successful, mem->response->data will be NULL
   * as a fallback, allocate a predefined size of memory and realloc if necessary
  **/
  if(!mem->response->data) {
    mem->response->buffer_size = DATA_BUFFER_SIZE;
    mem->response->data = (char*)am_malloc(mem->response->buffer_size);
    dbg_printf(P_INFO2, "[write_data_callback] allocated %d bytes for mem->response->data", mem->response->buffer_size);
  }

  if(mem->response->buffer_pos + line_len + 1 > mem->response->buffer_size) {
   do {
    mem->response->buffer_size *= 2;
   }while(mem->response->buffer_size < mem->response->buffer_pos + line_len + 1);

    mem->response->data = (char *)am_realloc(mem->response->data, mem->response->buffer_size);
  }

  if(mem->response->data) {
    memcpy(&(mem->response->data[mem->response->buffer_pos]), ptr, line_len);
    mem->response->buffer_pos += line_len;
    mem->response->data[mem->response->buffer_pos] = 0;
  }

  return line_len;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE struct HTTPData* HTTPData_new(void) {
  HTTPData* data = NULL;

  data = am_malloc(sizeof(struct HTTPData));
  if(!data) {
    return NULL;
  }

  data->data = NULL;
  data->buffer_size = 0;
  data->buffer_pos = 0;
  return data;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE void HTTPData_free(HTTPData* data) {

  if(data)
    am_free(data->data);
  am_free(data);
  data = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

/** \brief Free a WebData object and all memory associated with it
*
* \param[in] data Pointer to a WebData object
*/
PRIVATE void WebData_free(struct WebData *data) {

  if(data) {
    am_free(data->url);
    am_free(data->content_filename);
    HTTPData_free(data->response);
    am_free(data);
    data = NULL;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

/** \brief Create a new WebData object
*
* \param[in] url URL for a WebData object
*
* The parameter \a url is optional. You may provide \c NULL if no URL is required or not known yet.
*/
PRIVATE struct WebData* WebData_new(const char *url) {
  WebData *data = NULL;

  data = am_malloc(sizeof(WebData));
  if(!data)
    return NULL;

  data->url = NULL;
  data->content_filename = NULL;
  data->content_length = -1;
  data->response = NULL;

  if(url) {
    data->url = am_strdup((char*)url);
  }

  data->response = HTTPData_new();
  if(!data->response) {
    WebData_free(data);
    return NULL;
  }
  return data;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE void WebData_clear(struct WebData *data) {

  if(data) {
    am_free(data->content_filename);
    data->content_filename = NULL;

    if(data->response) {
      am_free(data->response->data);
      data->response->data = NULL;
      data->response->buffer_size = 0;
      data->response->buffer_pos = 0;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE HTTPResponse* HTTPResponse_new(void) {
  HTTPResponse* resp = (HTTPResponse*)am_malloc(sizeof(struct HTTPResponse));
  if(resp) {
    resp->size = 0;
    resp->responseCode = 0;
    resp->data = NULL;
    resp->content_filename = NULL;
    resp->downloadSpeed = 0;
  }
  return resp;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

PUBLIC void HTTPResponse_free(struct HTTPResponse *response) {
  if(response) {
    am_free(response->data);
    am_free(response->content_filename);
    am_free(response);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

PRIVATE CURL* am_curl_init(uint8_t isPost) {
  CURL * curl = curl_easy_init();

  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L );
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, getenv( "AM_CURL_VERBOSE" ) != NULL);
  curl_easy_setopt(curl, CURLOPT_POST, isPost ? 1 : 0);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1500L );
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 55L );
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
  curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 600L );
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L );
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L );
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L );

  // The  encoding option was renamed in curl 7.21.6
#if LIBCURL_VERSION_NUM < 0x071506
  curl_easy_setopt(curl, CURLOPT_ENCODING, "" );
#else
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "" );
#endif
  dbg_printf(P_INFO2, "[am_curl_init] Created new curl session %p", (void*)curl);

  return curl;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

/** \brief Download a file from a given URL
*
* \param[in] url URL of the object to download
* \return a HTTPResponse object containing the response code.
*
* getHTTPData() attempts to download the file pointed to by \a url and stores the content in a WebData object.
* The function returns \c NULL if the download failed.
*/

PUBLIC HTTPResponse* downloadFile(const char *url, const char *filename, const char *useragent) {
  CURLcode      res;
  CURL         *curl_handle = NULL;
  char         *escaped_url = NULL;
  HTTPResponse *resp = NULL;
  long responseCode = -1;
  FILE *stream = NULL;
  double downloadSize;
  double downloadSpeed;  

  if(!url || !filename) {
    return NULL;
  }

  dbg_printf(P_INFO2, "[getHTTPData] url=%s", url);
  if(gbGlobalInitDone == FALSE) {
    curl_global_init(CURL_GLOBAL_ALL);
    gbGlobalInitDone = TRUE;
  }

  curl_handle = am_curl_init(FALSE);

  if(!curl_handle)
  {
    dbg_printf(P_ERROR, "curl_handle is uninitialized!");
  }

  if(filename && *filename) {
    stream = fopen(filename, "wb");
    if(stream == NULL) {
      dbg_printf(P_ERROR, "Cannot open '%s' for writing: %s", filename, strerror(errno));
    }
  }

  if(curl_handle && stream)
  {
    escaped_url = url_encode_whitespace(url);
    assert(escaped_url);
    resp = HTTPResponse_new();
    curl_easy_setopt(curl_handle, CURLOPT_URL, escaped_url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, stream);

    if(useragent && *useragent) {
      curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, useragent);
    }

    res = curl_easy_perform(curl_handle);

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_getinfo(curl_handle, CURLINFO_SIZE_DOWNLOAD, &downloadSize);
    curl_easy_getinfo(curl_handle, CURLINFO_SPEED_DOWNLOAD, &downloadSpeed);
    dbg_printf(P_INFO2, "[getHTTPData] response code: %d", responseCode);
    if(res != 0) {
        dbg_printf(P_ERROR, "[getHTTPData] '%s': %s (retval: %d)", url, curl_easy_strerror(res), res);
    } else {
      /* Only the very first connection attempt (where curl_session == NULL) should store the session,
      ** and only the last one should close the session.
      */
      resp->responseCode = responseCode;
      resp->size = (size_t)downloadSize;
      resp->downloadSpeed = downloadSpeed; 
    }

    am_free(escaped_url);
  }
  else
  {
    resp = NULL;
  }

  if(curl_handle != NULL) {
    closeCURLSession(curl_handle);
  }

  if(stream != NULL) {
    fclose(stream);
  }

  return resp;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

/** \brief Download data from a given URL
*
* \param[in] url URL of the object to download
* \return a WebData object containing the complete response as well as header information
*
* getHTTPData() attempts to download the file pointed to by \a url and stores the content in a WebData object.
* The function returns \c NULL if the download failed.
*/

PUBLIC HTTPResponse* getHTTPData(const char *url, const char *cookies, CURL ** curl_session) {
  CURLcode      res;
  CURL         *curl_handle = NULL;
  CURL         *session = *curl_session;
  char         *escaped_url = NULL;
  WebData      *data = NULL;
  HTTPResponse *resp = NULL;
  long responseCode = -1;

  if(!url) {
    return NULL;
  }

  data = WebData_new(url);

  if(!data) {
    return NULL;
  }

  dbg_printf(P_INFO2, "[getHTTPData] url=%s, curl_session=%p", url, (void*)session);
  if(session == NULL) {
    if(gbGlobalInitDone == FALSE) {
      curl_global_init(CURL_GLOBAL_ALL);
      gbGlobalInitDone = TRUE;
    }
    session = am_curl_init(FALSE);
    *curl_session = session;
  }

  curl_handle = session;

  if(curl_handle) {
    escaped_url = url_encode_whitespace(url);
    assert(escaped_url);
    curl_easy_setopt(curl_handle, CURLOPT_URL, escaped_url);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, write_header_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, data);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEHEADER, data);

    if(cookies && *cookies) {
      /* if there's an explicit cookie string, use it */
      curl_easy_setopt(curl_handle, CURLOPT_COOKIE, cookies);
    } else {
      /* otherwise, enable cookie-handling since there might be cookies defined within the URL */
      curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    }

    res = curl_easy_perform(curl_handle);
    /* curl_easy_cleanup(curl_handle); */
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &responseCode);
    dbg_printf(P_INFO2, "[getHTTPData] response code: %d", responseCode);
    if(res != 0) {
        dbg_printf(P_ERROR, "[getHTTPData] '%s': %s (retval: %d)", url, curl_easy_strerror(res), res);
    } else {
      /* Only the very first connection attempt (where curl_session == NULL) should store the session,
      ** and only the last one should close the session.
      */
      resp = HTTPResponse_new();
      resp->responseCode = responseCode;
      //copy data if present
      if(data->response->data) {
        resp->size = data->response->buffer_pos;
        resp->data = am_strndup(data->response->data, resp->size);
      }
      //copy filename if present
      if(data->content_filename) {
        resp->content_filename = am_strdup(data->content_filename);
      }
    }
    am_free(escaped_url);
  } else {
    dbg_printf(P_ERROR, "curl_handle is uninitialized!");
    resp = NULL;
  }
  WebData_free(data);
  return resp;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

#define MAXLEN 200

/** \brief Upload data to a specified URL.
*
* \param url Path to where data shall be uploaded
* \param auth (Optional) authentication information in the form of "user:password"
* \param data Data that shall be uploaded
* \param data_size size of the data
* \return Web server response
*/
PUBLIC HTTPResponse* sendHTTPData(const char *url, const void *data, uint32_t data_size) {
  CURL *curl_handle = NULL;
  CURLcode res;
  long rc, tries = 2;
  WebData* response_data = NULL;
  HTTPResponse* resp = NULL;

  struct curl_slist * headers = NULL;

  if( !url || !data ) {
    return NULL;
  }

  response_data = WebData_new(url);

  do {
    --tries;
    WebData_clear(response_data);

    if( curl_handle == NULL) {
      if(gbGlobalInitDone == FALSE) {
        curl_global_init(CURL_GLOBAL_ALL);
        gbGlobalInitDone = TRUE;
      }
      if( ( curl_handle = am_curl_init(TRUE) ) ) {
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data_callback);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, write_header_callback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, response_data);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEHEADER, response_data);
        curl_easy_setopt(curl_handle, CURLOPT_URL, response_data->url);
      } else {
        dbg_printf(P_ERROR, "am_curl_init() failed");
        break;
      }
    }

    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, data_size);

    if( ( res = curl_easy_perform(curl_handle) ) ) {
      dbg_printf(P_ERROR, "Upload to '%s' failed: %s", url, curl_easy_strerror(res));
      break;
    } else {
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &rc);
      dbg_printf(P_INFO2, "response code: %ld", rc);
      if(rc == 409) {
        if(gSessionID) {
          dbg_printf(P_DBG, "Error code 409, session ID: %s", gSessionID);
        } else {
          dbg_printf(P_ERROR, "Error code 409, no session ID");
        }

        closeCURLSession( curl_handle );
        curl_slist_free_all( headers );
        headers = NULL;
        curl_handle = NULL;
      } else {
        resp = HTTPResponse_new();
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &resp->responseCode);

        //copy data if present
        if(response_data->response->data) {
          resp->size = response_data->response->buffer_pos;
          resp->data = am_strndup(response_data->response->data, resp->size);
        }
        //copy filename if present
        if(response_data->content_filename) {
          resp->content_filename = am_strdup(response_data->content_filename);
        }
        break;
      }
    }
  } while(tries > 0);

  /* cleanup */
  closeCURLSession(curl_handle);

  if(headers) {
    curl_slist_free_all(headers);
  }

  WebData_free(response_data);

  return resp;
}

PUBLIC void closeCURLSession(CURL* curl_handle) {
  if(curl_handle) {
    dbg_printf(P_INFO2, "[closeCURLSession] Closing curl session %p", (void*)curl_handle);
    curl_easy_cleanup(curl_handle);
    curl_handle = NULL;
  }
}
