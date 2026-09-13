/*
 * Test driver for the 7z extraction facade (src/sevenz_extract.c).
 *
 *   test_sevenz_extract <archive.7z> <out-dir> [password]
 *       Extract one archive. Exit status 0 means ZIPX_OK; the shell compares
 *       the result against the fixture's source tree byte for byte.
 *
 *   test_sevenz_extract --cases <fixtures-dir> <work-dir>
 *       Exercise the error and policy paths that need no byte comparison:
 *       a missing/wrong password, an encrypted header, conflicts under each
 *       policy, cancellation, limits, a missing destination parent, and the
 *       guarantee that nothing is published and no staging tree survives a
 *       failure.
 *
 * The host build injects tests/posix_compat.h (see run-sevenz-tests.sh), so
 * the engine can stay plain POSIX.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sevenz_extract.h"

#define PASSWORD "Secret123"
#define PATH_MAX_LOCAL 4096

static int g_checks = 0;
static int g_failures = 0;

static void
check(int ok, const char *what) {
  g_checks++;
  if(!ok) {
    g_failures++;
    printf("  FAIL %s\n", what);
  }
}

static int
cancel_always(void *userdata) {
  (void)userdata;
  return 1;
}

static int
has_staging_leftover(const char *dir) {
  DIR *d = opendir(dir);
  struct dirent *ent;
  int found = 0;

  if(!d) {
    return 0;
  }
  while((ent = readdir(d))) {
    if(!strncmp(ent->d_name, ".wfm-extract-", 13)) {
      found = 1;
      break;
    }
  }
  closedir(d);
  return found;
}

static int
extract_one(const char *archive, const char *dst, const char *password) {
  zipx_result_t r;
  zipx_status_t st = sevenz_extract(archive, dst, ZIPX_CONFLICT_FAIL,
                                    zipx_default_limits(), NULL, NULL, NULL,
                                    password, &r);

  if(st != ZIPX_OK) {
    fprintf(stderr, "  %s: %s: %s\n", archive, zipx_status_string(st),
            r.message);
    return 1;
  }
  return 0;
}

/* Runs a case that is expected to fail, and checks the status and message. */
static void
expect_fail(const char *label, const char *archive, const char *dst,
            const char *password, zipx_conflict_t conflict,
            const zipx_limits_t *limits, zipx_cancel_fn cancel,
            zipx_status_t want, const char *needle) {
  zipx_result_t r;
  zipx_status_t st;
  char buf[256];

  st = sevenz_extract(archive, dst, conflict, limits, cancel, NULL, NULL,
                      password, &r);
  snprintf(buf, sizeof(buf), "%s: status is %s, not %s", label,
           zipx_status_string(st), zipx_status_string(want));
  check(st == want, buf);
  if(needle) {
    snprintf(buf, sizeof(buf), "%s: message mentions \"%s\" (got \"%s\")",
             label, needle, r.message);
    check(strstr(r.message, needle) != NULL, buf);
  }
}

/* Extracts store.7z into `dst` under the given policy, expecting `want`. */
static void
policy_case(const char *label, const char *archive, const char *dst,
            zipx_conflict_t conflict, int run, zipx_status_t want) {
  zipx_result_t r;
  zipx_status_t st;
  char buf[256];

  st = sevenz_extract(archive, dst, conflict, zipx_default_limits(), NULL, NULL,
                      NULL, NULL, &r);
  snprintf(buf, sizeof(buf), "%s (run %d): status is %s, not %s", label, run,
           zipx_status_string(st), zipx_status_string(want));
  check(st == want, buf);
}

static int
run_cases(const char *fx, const char *work) {
  char arc[PATH_MAX_LOCAL];
  char dst[PATH_MAX_LOCAL];
  zipx_limits_t tight;

  mkdir(work, 0777);

  /* --- passwords ----------------------------------------------------- */
  snprintf(arc, sizeof(arc), "%s/aes.7z", fx);
  snprintf(dst, sizeof(dst), "%s/pw-missing", work);
  mkdir(dst, 0777);
  expect_fail("aes with no password", arc, dst, NULL, ZIPX_CONFLICT_FAIL,
              zipx_default_limits(), NULL, ZIPX_ERR_PASSWORD, "encrypted");

  snprintf(dst, sizeof(dst), "%s/pw-wrong", work);
  mkdir(dst, 0777);
  expect_fail("aes with the wrong password", arc, dst, "NotThePassword",
              ZIPX_CONFLICT_FAIL, zipx_default_limits(), NULL,
              ZIPX_ERR_PASSWORD, "7zAES");
  check(!has_staging_leftover(work), "no staging tree survives a wrong password");

  snprintf(dst, sizeof(dst), "%s/pw-ok", work);
  mkdir(dst, 0777);
  check(extract_one(arc, dst, PASSWORD) == 0,
        "aes extracts with the right password");

  /* --- encrypted header ---------------------------------------------- */
  snprintf(arc, sizeof(arc), "%s/aeshe.7z", fx);
  snprintf(dst, sizeof(dst), "%s/he", work);
  mkdir(dst, 0777);
  expect_fail("aeshe (encrypted header)", arc, dst, PASSWORD, ZIPX_CONFLICT_FAIL,
              zipx_default_limits(), NULL, ZIPX_ERR_UNSUPPORTED, "-mhe=on");

  /* --- open failures -------------------------------------------------- */
  snprintf(dst, sizeof(dst), "%s/missing-file", work);
  mkdir(dst, 0777);
  expect_fail("a missing archive", "/no/such/archive.7z", dst, NULL,
              ZIPX_CONFLICT_FAIL, zipx_default_limits(), NULL, ZIPX_ERR_OPEN,
              "cannot open");

  snprintf(arc, sizeof(arc), "%s/store.7z", fx);
  snprintf(dst, sizeof(dst), "%s/no-parent/deeper", work);
  expect_fail("a destination whose parent is missing", arc, dst, NULL,
              ZIPX_CONFLICT_FAIL, zipx_default_limits(), NULL, ZIPX_ERR_IO,
              "destination parent is missing");

  /* --- conflict policies ---------------------------------------------- */
  snprintf(dst, sizeof(dst), "%s/policy-fail", work);
  mkdir(dst, 0777);
  policy_case("fail", arc, dst, ZIPX_CONFLICT_FAIL, 1, ZIPX_OK);
  policy_case("fail", arc, dst, ZIPX_CONFLICT_FAIL, 2, ZIPX_ERR_CONFLICT);

  snprintf(dst, sizeof(dst), "%s/policy-overwrite", work);
  mkdir(dst, 0777);
  policy_case("overwrite", arc, dst, ZIPX_CONFLICT_OVERWRITE, 1, ZIPX_OK);
  policy_case("overwrite", arc, dst, ZIPX_CONFLICT_OVERWRITE, 2, ZIPX_OK);

  snprintf(dst, sizeof(dst), "%s/policy-merge", work);
  mkdir(dst, 0777);
  policy_case("merge", arc, dst, ZIPX_CONFLICT_MERGE, 1, ZIPX_OK);
  policy_case("merge", arc, dst, ZIPX_CONFLICT_MERGE, 2, ZIPX_OK);

  /* --- cancellation --------------------------------------------------- */
  snprintf(dst, sizeof(dst), "%s/cancel", work);
  mkdir(dst, 0777);
  expect_fail("a canceled extraction", arc, dst, NULL, ZIPX_CONFLICT_FAIL,
              zipx_default_limits(), cancel_always, ZIPX_ERR_CANCELED, NULL);
  check(!has_staging_leftover(work), "no staging tree survives a cancellation");

  /* --- limits --------------------------------------------------------- */
  tight = *zipx_default_limits();
  tight.max_entries = 1;
  snprintf(dst, sizeof(dst), "%s/limits", work);
  mkdir(dst, 0777);
  expect_fail("an archive over the entry limit", arc, dst, NULL,
              ZIPX_CONFLICT_FAIL, &tight, NULL, ZIPX_ERR_LIMIT_ENTRIES,
              "more than 1 entries");

  printf("  cases: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

int
main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if(argc >= 4 && !strcmp(argv[1], "--cases")) {
    return run_cases(argv[2], argv[3]);
  }
  if(argc < 3) {
    fprintf(stderr,
            "usage: %s <archive.7z> <out-dir> [password]\n"
            "       %s --cases <fixtures-dir> <work-dir>\n",
            argv[0], argv[0]);
    return 2;
  }
  return extract_one(argv[1], argv[2], argc > 3 ? argv[3] : NULL);
}
