/* Facade header for dmc_unrar 1.7.0 (vendored at third_party/unrar/dmc_unrar.c).
 *
 * This file declares only the dmc_unrar entry points that src/rar_extract.c
 * actually uses. It exists so that rar_extract.c does NOT have to
 * `#include "dmc_unrar.c"` — that would otherwise pull the entire library
 * into the same translation unit, where the host test build's
 * tests/posix_compat.h renames `open` / `close` to `wfm_open` / `wfm_close`
 * and breaks dmc_unrar's `dmc_unrar_io_handler` struct member access.
 *
 * On the PS5 the build (Makefile) compiles dmc_unrar.c separately into
 * `ps5-obj/.../dmc_unrar.o` and links it with the rest. On the host test
 * runner, `tests/run-tests.sh` does the same with `dmc_unrar.o` (built
 * without the POSIX compat shim) and `rar_extract.o` (built with it). */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DMC_UNRAR_SIZE_T and DMC_UNRAR_OFFSET_T are normally typedef'd by
   dmc_unrar.c. Mirror them here so consumers do not need to include it. */
#ifndef DMC_UNRAR_SIZE_T
typedef uint64_t dmc_unrar_size_t;
typedef int64_t  dmc_unrar_offset_t;
#endif

typedef enum {
  DMC_UNRAR_OK = 0,

  DMC_UNRAR_NO_ALLOC,
  DMC_UNRAR_ALLOC_FAIL,

  DMC_UNRAR_OPEN_FAIL,
  DMC_UNRAR_READ_FAIL,
  DMC_UNRAR_WRITE_FAIL,
  DMC_UNRAR_SEEK_FAIL,

  DMC_UNRAR_INVALID_DATA,

  DMC_UNRAR_ARCHIVE_EMPTY,

  DMC_UNRAR_ARCHIVE_IS_NULL,
  DMC_UNRAR_ARCHIVE_NOT_CLEARED,
  DMC_UNRAR_ARCHIVE_MISSING_FIELDS,

  DMC_UNRAR_ARCHIVE_NOT_RAR,
  DMC_UNRAR_ARCHIVE_UNSUPPORTED_ANCIENT,

  DMC_UNRAR_ARCHIVE_UNSUPPORTED_VOLUMES,
  DMC_UNRAR_ARCHIVE_UNSUPPORTED_ENCRYPTED,

  DMC_UNRAR_FILE_IS_INVALID,
  DMC_UNRAR_FILE_IS_DIRECTORY,

  DMC_UNRAR_FILE_SOLID_BROKEN,
  DMC_UNRAR_FILE_CRC32_FAIL,

  DMC_UNRAR_FILE_UNSUPPORTED_VERSION,
  DMC_UNRAR_FILE_UNSUPPORTED_METHOD,
  DMC_UNRAR_FILE_UNSUPPORTED_ENCRYPTED,
  DMC_UNRAR_FILE_UNSUPPORTED_SPLIT,
  DMC_UNRAR_FILE_UNSUPPORTED_LINK,
  DMC_UNRAR_FILE_UNSUPPORTED_LARGE
  /* Values after DMC_UNRAR_FILE_UNSUPPORTED_LARGE (HUFF_*, PPMD_*, FILTERS_*,
     *_DISABLED_FEATURE_*, RAR15/20/30/50 specific codes) are intentionally
     omitted — rar_extract.c only translates the codes it knows about and
     groups the rest into ZIPX_ERR_FORMAT via the default case. */
} dmc_unrar_return;

typedef enum {
  DMC_UNRAR_HOSTOS_DOS   = 0,
  DMC_UNRAR_HOSTOS_OS2   = 1,
  DMC_UNRAR_HOSTOS_WIN32 = 2,
  DMC_UNRAR_HOSTOS_UNIX  = 3,
  DMC_UNRAR_HOSTOS_MACOS = 4,
  DMC_UNRAR_HOSTOS_BEOS  = 5
} dmc_unrar_host_os;

typedef struct dmc_unrar_file_tag {
  uint64_t compressed_size;
  uint64_t uncompressed_size;
  dmc_unrar_host_os host_os;
  bool has_crc;
  uint32_t crc;
  uint64_t unix_time;
  uint64_t attrs;
} dmc_unrar_file;

/* dmc_unrar_archive is an opaque struct in the vendored library. We expose
   it here so callers can stack-allocate one and pass &rar to the API. The
   field list is *not* required — the dmc_unrar API treats the struct as a
   token, but we need a complete type so callers can declare it as a local
   variable. The layout is irrelevant because the dmc_unrar API only uses
   pointers and the real definition lives inside dmc_unrar.c. */
typedef struct dmc_unrar_archive_tag {
  void *alloc;
  void *io;
  void *internal_state;
} dmc_unrar_archive;

const char *dmc_unrar_strerror(dmc_unrar_return code);

bool dmc_unrar_is_rar_path(const char *path);

dmc_unrar_return dmc_unrar_archive_init(dmc_unrar_archive *archive);
void dmc_unrar_archive_close(dmc_unrar_archive *archive);
dmc_unrar_return dmc_unrar_archive_open_path(dmc_unrar_archive *archive,
                                              const char *path);

dmc_unrar_size_t dmc_unrar_get_file_count(dmc_unrar_archive *archive);
const dmc_unrar_file *dmc_unrar_get_file_stat(dmc_unrar_archive *archive,
                                              dmc_unrar_size_t index);

dmc_unrar_size_t dmc_unrar_get_filename(dmc_unrar_archive *archive,
                                        dmc_unrar_size_t index,
                                        char *filename,
                                        dmc_unrar_size_t filename_size);
bool dmc_unrar_file_is_directory(dmc_unrar_archive *archive,
                                 dmc_unrar_size_t index);
dmc_unrar_return dmc_unrar_file_is_supported(dmc_unrar_archive *archive,
                                            dmc_unrar_size_t index);

bool dmc_unrar_unicode_make_valid_utf8(char *str);

dmc_unrar_return dmc_unrar_extract_file_to_path(dmc_unrar_archive *archive,
                                                dmc_unrar_size_t index,
                                                const char *path,
                                                dmc_unrar_size_t *uncompressed_size,
                                                bool validate_crc);

#ifdef __cplusplus
}
#endif