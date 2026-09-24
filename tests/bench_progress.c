/* Prints every extraction progress report, with a monotonic timestamp.
 *
 *   bench_progress <archive> <out-dir> [--mode print|empty|none]
 *
 * bench_extract answers "how long"; this answers "did the UI move while it
 * took that long". A report that sits at entries=0/N for minutes reads to the
 * user as a hang even though bytes are still flowing, so being able to see the
 * phase/entries/bytes sequence is what turns "the progress bar freezes" into a
 * named phase. Format is picked from the suffix, same as bench_extract.
 *
 * --mode exists to answer a second question: "does reporting itself cost
 * time?". The engine's report() helper runs on the hot path -- unrar calls it
 * once per decompressed chunk -- and it reads the clock before it decides
 * whether to throttle, so the cost is paid even when no report goes out.
 *   print  a callback that formats and prints every report (diagnostic)
 *   empty  a callback that returns immediately (engine cost, no consumer)
 *   none   no callback at all (the engine skips report() entirely)
 * Comparing wall time across the three separates "our bookkeeping" from
 * "the file I/O we cannot avoid".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zip_extract.h"
#include "rar_extract.h"
#include "sevenz_extract.h"

static double g_t0;

static int
has_suffix(const char *path, const char *suffix) {
  size_t path_len, suffix_len, i;

  if(!path || !suffix) return 0;
  path_len = strlen(path);
  suffix_len = strlen(suffix);
  if(path_len < suffix_len) return 0;
  for(i = 0; i < suffix_len; i++) {
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

static const char *
phase_name(int phase) {
  switch(phase) {
  case ZIPX_PHASE_SCAN: return "scan";
  case ZIPX_PHASE_EXTRACT: return "extract";
  case ZIPX_PHASE_PUBLISH: return "publish";
  case ZIPX_PHASE_CLEANUP: return "cleanup";
  default: return "?";
  }
}

/* Costs exactly what the engine's report() costs, without a consumer. */
static void
on_progress_empty(void *userdata, const zipx_progress_t *p) {
  (void)userdata;
  (void)p;
}

static void
on_progress(void *userdata, const zipx_progress_t *p) {
  (void)userdata;
  printf("%8.3f  %-7s entries=%llu/%llu  bytes=%llu/%llu  %s\n",
         now_seconds() - g_t0, phase_name(p->phase),
         (unsigned long long)p->entries_done,
         (unsigned long long)p->entries_total,
         (unsigned long long)p->bytes_done,
         (unsigned long long)p->bytes_total,
         p->current ? p->current : "");
  fflush(stdout);
}

int
main(int argc, char **argv) {
  zipx_result_t result;
  zipx_status_t status;
  const char *archive;
  const char *out_dir;
  const char *format;
  const char *mode = "print";
  zipx_progress_fn on_report = on_progress;
  double elapsed;
  int i;

  if(argc < 3) {
    fprintf(stderr, "usage: %s <archive> <out-dir> [--mode print|empty|none]\n",
            argv[0]);
    return 2;
  }
  archive = argv[1];
  out_dir = argv[2];

  for(i = 3; i < argc; i++) {
    if(!strcmp(argv[i], "--mode") && i + 1 < argc) {
      mode = argv[++i];
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      return 2;
    }
  }
  if(!strcmp(mode, "empty")) {
    on_report = on_progress_empty;
  } else if(!strcmp(mode, "none")) {
    on_report = NULL;
  } else if(strcmp(mode, "print")) {
    fprintf(stderr, "unknown --mode: %s\n", mode);
    return 2;
  }

  if(has_suffix(archive, ".rar") || has_suffix(archive, ".part1.rar") ||
     has_suffix(archive, ".r00")) {
    format = "rar";
  } else if(has_suffix(archive, ".zip") || has_suffix(archive, ".z01") ||
            has_suffix(archive, ".zip.001")) {
    format = "zip";
  } else {
    format = "7z";
  }

  memset(&result, 0, sizeof(result));
  g_t0 = now_seconds();

  if(!strcmp(format, "rar")) {
    status = rar_extract(archive, out_dir, ZIPX_CONFLICT_OVERWRITE,
                         zipx_default_limits(), NULL, on_report, NULL, NULL,
                         &result);
  } else if(!strcmp(format, "zip")) {
    status = zipx_extract(archive, out_dir, ZIPX_CONFLICT_OVERWRITE,
                          zipx_default_limits(), NULL, on_report, NULL, NULL,
                          &result);
  } else {
    status = sevenz_extract(archive, out_dir, ZIPX_CONFLICT_OVERWRITE,
                            zipx_default_limits(), NULL, on_report, NULL,
                            NULL, &result);
  }

  elapsed = now_seconds() - g_t0;
  printf("---- done: format=%s mode=%s status=%s entries=%llu bytes=%llu wall=%.3fs\n",
         format, mode, zipx_status_string(status),
         (unsigned long long)result.entries_total,
         (unsigned long long)result.bytes_total, elapsed);
  return status == ZIPX_OK ? 0 : 1;
}
