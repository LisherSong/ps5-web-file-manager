#pragma once

/* Safe RAR extraction engine used by the /api/extract task.
   Wraps the vendored dmc_unrar library (https://github.com/DrMcCoy/dmc_unrar).

   Reuses the zip_extract types so the dispatch layer can call either engine
   through the same status / limits / progress protocol.

   See zip_extract.h for the shared limits, conflict, progress and result types.

   Constraints of v1.8 (dmc_unrar 1.7.0 backend):
     * Single-volume RAR archives only. Multi-volume (.partNN.rar) archives
       are rejected with ZIPX_ERR_UNSUPPORTED — extract them on a PC first.
     * Unencrypted RAR only. Encrypted headers / files are rejected with
       ZIPX_ERR_UNSUPPORTED. There is no password argument for the same reason.

   See third_party/unrar/VENDORED.md for the upgrade path to rarlab UnRAR
   (which does support both) when / if it becomes worth the C++ integration. */

#include "zip_extract.h"

#include <stdint.h>
#include <stddef.h>

/* Extract rar_path into dst_dir using the same protocol as zipx_extract().
   Returns ZIPX_OK or an error code; *result is always filled in.
   On any failure the staging directory is removed and dst_dir is left as it
   was, except for objects already published with the overwrite policy. */
zipx_status_t rar_extract(const char *rar_path, const char *dst_dir,
                          zipx_conflict_t conflict,
                          const zipx_limits_t *limits,
                          zipx_cancel_fn cancel,
                          zipx_progress_fn progress,
                          void *userdata,
                          zipx_result_t *result);