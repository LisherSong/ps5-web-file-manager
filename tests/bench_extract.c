/* Wall-clock benchmark for the extraction paths, ZIP and 7z.
 *
 *   bench_extract <archive> <out-dir> [password]
 *
 * Reports how long the facade takes end to end -- decode, staging writes,
 * fsync, publish -- which is exactly what a PS5 user waits for. It is
 * deliberately separate from the correctness drivers: those assert on bytes,
 * this one only prints numbers, and it is not part of the test matrix.
 *
 * Keeping it in-tree matters because "is our engine fast?" is a question that
 * will come up again, and the answer should be a command anyone can rerun
 * rather than a number somebody remembers. The format is picked from the
 * suffix so the same binary covers both engines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sevenz_extract.h"

static int
has_suffix(const char *path, const char *suffix) {
  size_t path_len;
  size_t suffix_len;

  if(!path || !suffix) return 0;
  path_len = strlen(path);
  suffix_len = strlen(suffix);
  if(path_len < suffix_len) return 0;
  for(size_t i = 0; i < suffix_len; i++) {
    char a = path[path_len - suffix_len + i];
    char b = suffix[i];
    if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if(b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if(a != b) return 0;
  }
  return 1;
}

static double
now_seconds(void) {
  struct timespec ts;

  if(timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0.0;
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

int
main(int argc, char **argv) {
  zipx_result_t result;
  zipx_status_t status;
  const char *archive;
  const char *out_dir;
  const char *password;
  const char *format;
  double started;
  double elapsed;
  double mebibytes;

  if(argc < 3) {
    fprintf(stderr, "usage: %s <archive> <out-dir> [password]\n", argv[0]);
    return 2;
  }
  archive = argv[1];
  out_dir = argv[2];
  password = argc > 3 ? argv[3] : NULL;

  if(has_suffix(archive, ".7z") || has_suffix(archive, ".7z.001") ||
     has_suffix(archive, ".001")) {
    format = "7z";
  }
#ifndef BENCH_SEVENZ_ONLY
  else if(has_suffix(archive, ".zip") || has_suffix(archive, ".zip.001") ||
            has_suffix(archive, ".z01")) {
    format = "zip";
  }
#endif
  else {
    fprintf(stderr, "unsupported benchmark format: %s\n", archive);
    return 2;
  }

  memset(&result, 0, sizeof(result));
  started = now_seconds();
#ifndef BENCH_SEVENZ_ONLY
  if(!strcmp(format, "zip")) {
    status = zipx_extract(archive, out_dir, ZIPX_CONFLICT_OVERWRITE,
                          zipx_default_limits(), NULL, NULL, NULL, &result);
  } else
#endif
  {
    status = sevenz_extract(archive, out_dir, ZIPX_CONFLICT_OVERWRITE,
                            zipx_default_limits(), NULL, NULL, NULL,
                            password, &result);
  }
  elapsed = now_seconds() - started;

  mebibytes = (double)result.bytes_total / (1024.0 * 1024.0);

  printf("format   : %s\n", format);
  printf("status   : %s\n", zipx_status_string(status));
  printf("entries  : %llu\n", (unsigned long long)result.entries_total);
  printf("unpacked : %.1f MiB\n", mebibytes);
  printf("wall     : %.3f s\n", elapsed);
  if(elapsed > 0.0) {
    printf("through  : %.1f MiB/s (single thread)\n", mebibytes / elapsed);
  }
  if(status != ZIPX_OK) {
    printf("detail   : %s\n", result.detail[0] ? result.detail : "(none)");
    printf("message  : %s\n", result.message[0] ? result.message : "(none)");
  }
  return status == ZIPX_OK ? 0 : 1;
}
