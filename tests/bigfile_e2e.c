/* Standalone big-file e2e: extract one large zip64 archive on the host and
 * report the engine result. Verification (size + sha256/cmp) is done by the
 * caller with shell tools.
 *
 *   cc -O2 -Isrc -Ithird_party/minizip-ng/include \
 *      -include tests/posix_compat.h \
 *      -o bigfile_e2e bigfile_e2e.c zip_extract.o <minizip+zlib objs>
 *
 *   ./bigfile_e2e <archive.zip> <out-dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zip_extract.h"

int
main(int argc, char **argv) {
  zipx_result_t res;
  zipx_status_t st;

  if(argc != 3) {
    fprintf(stderr, "usage: %s <archive.zip> <out-dir>\n", argv[0]);
    return 2;
  }
  st = zipx_extract(argv[1], argv[2], ZIPX_CONFLICT_FAIL,
                    zipx_limits_profile(ZIPX_LIMITS_LARGE),
                    NULL, NULL, NULL, NULL, &res);
  printf("status=%d (%s)\n", (int)st, zipx_status_string(st));
  printf("sys_errno=%d entries=%llu/%llu files=%llu dirs=%llu\n",
         res.sys_errno, (unsigned long long)res.entries_done,
         (unsigned long long)res.entries_total,
         (unsigned long long)res.files_created,
         (unsigned long long)res.dirs_created);
  printf("bytes_total=%llu detail=%s\n",
         (unsigned long long)res.bytes_total, res.detail);
  if(res.message[0]) {
    printf("message=%s\n", res.message);
  }
  return st == ZIPX_OK ? 0 : 1;
}
