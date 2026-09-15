/*
 * /api/version -- hands the build's VERSION_TAG to the web UI.
 *
 * The footer in the browser shows a version string, and for a long time that
 * string was a literal in assets/main.js, so it drifted out of sync the moment
 * the Makefile moved on (v1.9 stayed on screen through the whole v1.9.1
 * release). Exposing it over the API keeps a single source of truth: bump
 * VERSION_TAG in the Makefile and every surface -- startup notification
 * (src/main.c), stdout banner, ELF file name and the UI footer -- follows.
 *
 * The response is tiny and immutable, so the client caches it for the session.
 */

#include "filemgr_internal.h"

#include <string.h>

#include "json_util.h"

#ifndef VERSION_TAG
#define VERSION_TAG "unknown"
#endif

enum MHD_Result
api_version(struct MHD_Connection *conn) {
  strbuf_t b = {0};

  strbuf_append(&b, "{\"ok\":true,\"version\":");
  json_escape(&b, VERSION_TAG);
  strbuf_append(&b, ",\"titleId\":");
  json_escape(&b, TITLE_ID);
  strbuf_append(&b, "}");
  return send_buffer(conn, MHD_HTTP_OK, b.data, "application/json");
}
