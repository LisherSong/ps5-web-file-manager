#pragma once

#include <microhttpd.h>

/* Conflict policy for ZIP extraction, mirrors the zipx_conflict_t values. */
typedef enum extract_conflict {
  EXTRACT_CONFLICT_FAIL = 0,
  EXTRACT_CONFLICT_OVERWRITE = 1,
  EXTRACT_CONFLICT_MERGE = 2
} extract_conflict_t;

/* POST /api/extract handler.
   Accepts a form-encoded body (matching the other filemgr endpoints):
     path           - ZIP path on the device (required)
     dst_dir        - target directory (required)
     conflict       - "fail" (default) | "overwrite" | "merge"
     remove_source  - "1" deletes the source ZIP after a successful extraction
                      (used by the "upload and extract" flow; never set for a
                      pre-existing user archive).
   Returns {"ok":true,"task_id":N} or a JSON error. */
enum MHD_Result api_extract(struct MHD_Connection *conn, const char *body,
                            size_t body_size);
