/* Host test suite for the ZIP extraction engine.
   Build: see run-tests.sh (uses gcc + the MinGW POSIX shim). */

#include "../src/zip_extract.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Pulled in by the test build only (see run-tests.sh). */
#include "posix_compat.h"

static const char *g_fixtures;
static char g_work[4096];
static int g_failures;
static int g_checks;

static void
fixture_path(char *out, size_t size, const char *name) {
  snprintf(out, size, "%s/%s", g_fixtures, name);
}

static void
work_path(char *out, size_t size, const char *name) {
  snprintf(out, size, "%s/%s", g_work, name);
}

static void
check(int ok, const char *what) {
  g_checks++;
  if(!ok) {
    g_failures++;
    printf("  FAIL %s\n", what);
  }
}

static int
exists(const char *path) {
  struct stat st;

  return !stat(path, &st);
}

static int
read_text(const char *path, char *buf, size_t size) {
  FILE *f = fopen(path, "rb");
  size_t n;

  if(!f) {
    return -1;
  }
  n = fread(buf, 1, size - 1, f);
  buf[n] = 0;
  fclose(f);
  return 0;
}

/* Reads binary content (entries may contain NUL bytes, so read_text cannot be
   used to verify them). Returns the number of bytes read, or -1. */
static long
read_bytes(const char *path, unsigned char *buf, size_t size) {
  FILE *f = fopen(path, "rb");
  size_t n;

  if(!f) {
    return -1;
  }
  n = fread(buf, 1, size, f);
  fclose(f);
  return (long)n;
}

static int
remove_dir(const char *path) {  DIR *dir = opendir(path);
  struct dirent *ent;

  if(!dir) {
    return rmdir(path);
  }
  while((ent = readdir(dir))) {
    char child[4096];
    struct stat st;

    if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
      continue;
    }
    snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
    if(!stat(child, &st) && S_ISDIR(st.st_mode)) {
      remove_dir(child);
    } else {
      unlink(child);
    }
  }
  closedir(dir);
  return rmdir(path);
}

static int
make_dirs(const char *path) {
  char buf[4096];
  char *slash;
  struct stat st;

  if(!*path || !stat(path, &st)) {
    return 0;
  }
  snprintf(buf, sizeof(buf), "%s", path);
  for(slash = buf + 1; *slash; slash++) {
    if(*slash != '/') {
      continue;
    }
    *slash = 0;
    if(mkdir(buf, 0777) && errno != EEXIST) {
      return -1;
    }
    *slash = '/';
  }
  return mkdir(buf, 0777) && errno != EEXIST ? -1 : 0;
}

static int
write_text(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");

  if(!f) {
    return -1;
  }
  fputs(content, f);
  fclose(f);
  return 0;
}

static int
count_staging_leftovers(const char *dir) {
  DIR *d = opendir(dir);
  struct dirent *ent;
  int count = 0;

  if(!d) {
    return 0;
  }
  while((ent = readdir(d))) {
    if(!strncmp(ent->d_name, ".wfm-extract-", 13)) {
      count++;
    }
  }
  closedir(d);
  return count;
}

/**************************************************************************/

typedef struct {
  int cancel_after_progress;
  zipx_progress_t last;
  int reports;
  char last_current[512];
} test_ctx_t;

static int
cb_cancel(void *userdata) {
  test_ctx_t *t = userdata;

  return t->cancel_after_progress && t->reports >= t->cancel_after_progress;
}

static void
cb_progress(void *userdata, const zipx_progress_t *p) {
  test_ctx_t *t = userdata;

  t->reports++;
  t->last = *p;
  if(p->current) {
    snprintf(t->last_current, sizeof(t->last_current), "%s", p->current);
  }
}

static zipx_status_t
run(const char *fixture, const char *dst_name, zipx_conflict_t conflict,
    const zipx_limits_t *limits, test_ctx_t *t, zipx_result_t *res) {
  char zip[4096];
  char dst[4096];

  fixture_path(zip, sizeof(zip), fixture);
  work_path(dst, sizeof(dst), dst_name);
  return zipx_extract(zip, dst, conflict, limits, cb_cancel, cb_progress,
                      t, res);
}

static void
expect_ok(zipx_result_t *res, zipx_status_t status, const char *label) {
  check(status == ZIPX_OK, label);
  if(status != ZIPX_OK) {
    printf("       status=%s (%d) %s / %s\n", zipx_status_string(status),
           status, res->message, res->detail);
  }
}

static void
expect_status(zipx_result_t *res, zipx_status_t status,
              zipx_status_t expected, const char *label) {
  check(status == expected, label);
  if(status != expected) {
    printf("       expected %s, got %s (%d) %s / %s\n",
           zipx_status_string(expected), zipx_status_string(status), status,
           res->message, res->detail);
  }
}

/**************************************************************************/

/* Verifies the entries all three split layouts share: the same archive was
   sliced three ways, so every extraction has to produce identical content. */
static void
check_split_content(const char *dst_name, const char *label) {
  static unsigned char data[16384];
  char dst[4096];
  char file[4224];
  char buf[512];
  long n;
  size_t i;
  int ok = 1;

  work_path(dst, sizeof(dst), dst_name);
  check(exists(dst), label);

  snprintf(file, sizeof(file), "%s/readme.txt", dst);
  n = read_bytes(file, (unsigned char *)buf, sizeof(buf));
  check(n == 440 && !memcmp(buf, "split archive fixture\n", 22),
        "  readme.txt content");

  snprintf(file, sizeof(file), "%s/sub/data.bin", dst);
  n = read_bytes(file, data, 10240);
  if(n != 10240) {
    ok = 0;
  } else {
    for(i = 0; i < 10240; i++) {
      if(data[i] != (unsigned char)(i % 256)) {
        ok = 0;
        break;
      }
    }
  }
  check(ok, "  sub/data.bin binary content (10 KiB across volumes)");

  snprintf(file, sizeof(file), "%s/sub/deep/more.bin", dst);
  n = read_bytes(file, data, 12000);
  check(n == 12000 && data[0] == 'A' && data[3] == 'D',
        "  sub/deep/more.bin content");
}

static void
test_volumes(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  char dst[4096];

  printf("split volumes\n");

  /* name.zip.001 style (7-Zip byte split), picked from the first volume. */
  expect_ok(&res, run("plain.zip.001", "out_vol_plain_a", ZIPX_CONFLICT_FAIL,
                      NULL, &t, &res),
            "byte split: extract from part 001");
  check_split_content("out_vol_plain_a", "  output tree exists");

  /* Same set, but the user clicked the last volume this time. */
  expect_ok(&res, run("plain.zip.003", "out_vol_plain_b", ZIPX_CONFLICT_FAIL,
                      NULL, &t, &res),
            "byte split: extract from part 003");
  check_split_content("out_vol_plain_b", "  output tree exists");

  /* name.partN.zip style, picked from the middle volume. */
  expect_ok(&res, run("parts.part2.zip", "out_vol_parts", ZIPX_CONFLICT_FAIL,
                      NULL, &t, &res),
            "partN split: extract from part2");
  check_split_content("out_vol_parts", "  output tree exists");

  /* name.z01 ... name.zip: offsets are relative to each disk, so this one
     exercises the disk-aware volume stream. */
  expect_ok(&res, run("disks.z01", "out_vol_disks_a", ZIPX_CONFLICT_FAIL,
                      NULL, &t, &res),
            "split disks: extract from z01");
  check_split_content("out_vol_disks_a", "  output tree exists");

  expect_ok(&res, run("disks.zip", "out_vol_disks_b", ZIPX_CONFLICT_FAIL,
                      NULL, &t, &res),
            "split disks: extract from the final volume");
  check_split_content("out_vol_disks_b", "  output tree exists");

  /* A plain archive must still take the ordinary path. */
  expect_ok(&res, run("split_single.zip", "out_vol_single", ZIPX_CONFLICT_FAIL,
                      NULL, &t, &res),
            "plain archive still extracts (no volume set detected)");
  check_split_content("out_vol_single", "  output tree exists");

  /* Broken sets must fail with the exact missing volume, not a vague error. */
  expect_status(&res, run("gap.zip.001", "out_vol_gap", ZIPX_CONFLICT_FAIL,
                          NULL, &t, &res),
                ZIPX_ERR_OPEN, "incomplete set: reported as an open failure");
  check(strstr(res.message, "gap.zip.002") != NULL,
        "  error names the missing volume");
  printf("       message: %s\n", res.message);

  expect_status(&res, run("broken.zip.001", "out_vol_broken", ZIPX_CONFLICT_FAIL,
                          NULL, &t, &res),
                ZIPX_ERR_OPEN, "lone first volume: reported as an open failure");
  check(strstr(res.message, "no other volumes") != NULL,
        "  error says the remaining volumes are missing");
  printf("       message: %s\n", res.message);

  work_path(dst, sizeof(dst), "out_vol_gap");
  check(!exists(dst), "nothing was written for the incomplete set");
}

/**************************************************************************/

static void
test_basic(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  char dst[4096];
  char file[4224];
  char buf[64];

  printf("basic (deflate + nested dirs + empty dir)\n");
  expect_ok(&res, run("basic.zip", "out_basic", ZIPX_CONFLICT_FAIL, NULL, &t, &res),
            "extract succeeds");
  work_path(dst, sizeof(dst), "out_basic");
  check(exists(dst), "destination created");
  snprintf(file, sizeof(file), "%s/root.txt", dst);
  check(!read_text(file, buf, sizeof(buf)) && !strcmp(buf, "root content"),
        "root.txt content");
  snprintf(file, sizeof(file), "%s/dir/nested.txt", dst);
  check(!read_text(file, buf, sizeof(buf)) && !strcmp(buf, "nested content"),
        "nested.txt content");
  snprintf(file, sizeof(file), "%s/dir/deep/deeper.txt", dst);
  check(!read_text(file, buf, sizeof(buf)) && !strcmp(buf, "deeper content"),
        "deeper.txt content");
  snprintf(file, sizeof(file), "%s/empty_dir", dst);
  check(exists(file), "empty directory created");
  check(res.entries_total == 4, "entry count reported");
  check(res.bytes_total > 0, "byte total reported");
  check(count_staging_leftovers(dst) == 0, "no staging leftovers inside dst");
}

static void
test_stored(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  char dst[4096];
  char file[4224];
  char buf[2048];
  size_t i;
  int ok = 1;

  printf("stored (no compression)\n");
  expect_ok(&res, run("stored.zip", "out_stored", ZIPX_CONFLICT_FAIL, NULL, &t, &res),
            "extract succeeds");
  work_path(dst, sizeof(dst), "out_stored");
  snprintf(file, sizeof(file), "%s/stored.txt", dst);
  if(read_text(file, buf, sizeof(buf))) {
    ok = 0;
  } else {
    for(i = 0; i < 1400; i++) {
      if(buf[i] != "stored content"[i % 14]) {
        ok = 0;
        break;
      }
    }
  }
  check(ok, "stored content matches");
}

static void
test_zip64(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  char dst[4096];
  char file[4224];
  struct stat st;

  printf("zip64\n");
  expect_ok(&res, run("zip64.zip", "out_zip64", ZIPX_CONFLICT_FAIL, NULL, &t, &res),
            "extract succeeds");
  work_path(dst, sizeof(dst), "out_zip64");
  snprintf(file, sizeof(file), "%s/big.bin", dst);
  check(!stat(file, &st) && st.st_size == 4096, "zip64 size");
}

static void
test_unicode(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  char dst[4096];
  char file[4224];
  char buf[64];

  printf("utf-8 entry names\n");
  expect_ok(&res, run("unicode.zip", "out_unicode", ZIPX_CONFLICT_FAIL, NULL,
                      &t, &res), "extract succeeds");
  work_path(dst, sizeof(dst), "out_unicode");
  snprintf(file, sizeof(file), "%s/\xe4\xb8\xad\xe6\x96\x87\xe7\x9b\xae\xe5\xbd\x95/\xe6\x96\x87\xe4\xbb\xb6.txt", dst);
  check(!read_text(file, buf, sizeof(buf)) &&
        !strcmp(buf, "unicode content"), "chinese path content");
}

static void
test_conflict_fail(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  char dst[4096];
  char file[4224];
  char buf[64];

  printf("conflict policy: fail\n");
  work_path(dst, sizeof(dst), "out_conflict_fail");
  remove_dir(dst);
  make_dirs(dst);
  snprintf(file, sizeof(file), "%s/shared.txt", dst);
  write_text(file, "original");
  expect_status(&res, run("conflict.zip", "out_conflict_fail",
                          ZIPX_CONFLICT_FAIL, NULL, &t, &res),
                ZIPX_ERR_CONFLICT, "existing file fails the task");
  snprintf(file, sizeof(file), "%s/shared.txt", dst);
  check(!read_text(file, buf, sizeof(buf)) && !strcmp(buf, "original"),
        "existing file untouched");
  snprintf(file, sizeof(file), "%s/shareddir", dst);
  check(!exists(file), "nothing published");
}

static void
test_conflict_overwrite(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  char dst[4096];
  char file[4224];
  char buf[64];

  printf("conflict policy: overwrite\n");
  work_path(dst, sizeof(dst), "out_overwrite");
  remove_dir(dst);
  make_dirs(dst);
  /* An existing directory where the archive has a file must still conflict,
     even under OVERWRITE (a directory is never replaced by a file). */
  snprintf(file, sizeof(file), "%s/shared.txt", dst);
  make_dirs(file);
  expect_status(&res, run("conflict.zip", "out_overwrite",
                          ZIPX_CONFLICT_OVERWRITE, NULL, &t, &res),
                ZIPX_ERR_CONFLICT, "existing directory still blocks overwrite");
  snprintf(file, sizeof(file), "%s/shared.txt", dst);
  check(exists(file), "directory untouched while it conflicts");

  remove_dir(dst);
  make_dirs(dst);
  snprintf(file, sizeof(file), "%s/shared.txt", dst);
  write_text(file, "original");
  snprintf(file, sizeof(file), "%s/shareddir", dst);
  remove_dir(file);
  expect_ok(&res, run("conflict.zip", "out_overwrite",
                      ZIPX_CONFLICT_OVERWRITE, NULL, &t, &res),
            "overwrite succeeds without a directory clash");
  snprintf(file, sizeof(file), "%s/shared.txt", dst);
  check(!read_text(file, buf, sizeof(buf)) && !strcmp(buf, "from zip"),
        "file replaced");
}

static void
test_conflict_merge(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  char dst[4096];
  char file[4224];
  char buf[64];

  printf("conflict policy: merge\n");
  work_path(dst, sizeof(dst), "out_merge");
  remove_dir(dst);
  make_dirs(dst);
  snprintf(file, sizeof(file), "%s/shareddir", dst);
  make_dirs(file);
  snprintf(file, sizeof(file), "%s/shareddir/inner.txt", dst);
  check(!write_text(file, "original inner"), "prepare merge destination");
  expect_ok(&res, run("conflict.zip", "out_merge", ZIPX_CONFLICT_MERGE, NULL,
                      &t, &res), "merge succeeds");
  snprintf(file, sizeof(file), "%s/shareddir/inner.txt", dst);
  check(!read_text(file, buf, sizeof(buf)) && !strcmp(buf, "original inner"),
        "existing file preserved by merge");
  snprintf(file, sizeof(file), "%s/shareddir/added.txt", dst);
  check(!read_text(file, buf, sizeof(buf)) &&
        !strcmp(buf, "added from zip"), "new sibling merged in");
  snprintf(file, sizeof(file), "%s/shared.txt", dst);
  check(!read_text(file, buf, sizeof(buf)) && !strcmp(buf, "from zip"),
        "top level file published");
}

static void
test_unsafe_names(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  char parent[4096];
  char file[4224];

  printf("path traversal variants\n");
  expect_status(&res, run("traversal.zip", "out_traversal",
                          ZIPX_CONFLICT_FAIL, NULL, &t, &res),
                ZIPX_ERR_UNSAFE_NAME, "../ entry rejected");
  expect_status(&res, run("traversal_bs.zip", "out_traversal_bs",
                          ZIPX_CONFLICT_FAIL, NULL, &t, &res),
                ZIPX_ERR_UNSAFE_NAME, "..\\ entry rejected");
  expect_status(&res, run("absolute.zip", "out_absolute",
                          ZIPX_CONFLICT_FAIL, NULL, &t, &res),
                ZIPX_ERR_UNSAFE_NAME, "absolute entry rejected");
  expect_status(&res, run("drive.zip", "out_drive", ZIPX_CONFLICT_FAIL, NULL,
                          &t, &res),
                ZIPX_ERR_UNSAFE_NAME, "drive letter entry rejected");

  work_path(parent, sizeof(parent), ".");
  snprintf(file, sizeof(file), "%s/evil.txt", parent);
  check(!exists(file), "no file escaped into the work directory");
  snprintf(file, sizeof(file), "%s/out_traversal", parent);
  check(!exists(file), "no destination created for a rejected archive");
}

static void
test_duplicates(void) {
  zipx_result_t res;
  test_ctx_t t = {0};

  printf("duplicate and clashing entries\n");
  expect_status(&res, run("duplicate.zip", "out_duplicate",
                          ZIPX_CONFLICT_FAIL, NULL, &t, &res),
                ZIPX_ERR_DUPLICATE, "duplicate file rejected");
  expect_status(&res, run("clash.zip", "out_clash", ZIPX_CONFLICT_FAIL, NULL,
                          &t, &res),
                ZIPX_ERR_DUPLICATE, "file used as a directory rejected");
}

static void
test_special_entries(void) {
  zipx_result_t res;
  test_ctx_t t = {0};

  printf("symlink and fifo entries\n");
  expect_status(&res, run("symlink.zip", "out_symlink", ZIPX_CONFLICT_FAIL,
                          NULL, &t, &res),
                ZIPX_ERR_SPECIAL, "symlink rejected");
  expect_status(&res, run("fifo.zip", "out_fifo", ZIPX_CONFLICT_FAIL, NULL,
                          &t, &res),
                ZIPX_ERR_SPECIAL, "fifo rejected");
}

static void
test_unsupported(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  const zipx_limits_t *base = zipx_default_limits();
  zipx_limits_t limits;

  printf("encrypted, corrupt and truncated archives\n");
  expect_status(&res, run("encrypted.zip", "out_encrypted",
                          ZIPX_CONFLICT_FAIL, NULL, &t, &res),
                ZIPX_ERR_UNSUPPORTED, "encrypted entry rejected");
  expect_status(&res, run("bad_crc.zip", "out_bad_crc", ZIPX_CONFLICT_FAIL,
                          NULL, &t, &res),
                ZIPX_ERR_CRC, "crc mismatch detected");
  check(run("truncated.zip", "out_truncated", ZIPX_CONFLICT_FAIL, NULL, &t, &res) !=
        ZIPX_OK, "truncated archive rejected");
  check(run("notazip.zip", "out_notazip", ZIPX_CONFLICT_FAIL, NULL, &t, &res) !=
        ZIPX_OK, "non-zip file rejected");

  printf("limits\n");
  limits = *base;
  limits.max_entries = 2;
  expect_status(&res, run("many.zip", "out_limit_entries",
                          ZIPX_CONFLICT_FAIL, &limits, &t, &res),
                ZIPX_ERR_LIMIT_ENTRIES, "entry limit enforced");

  limits = *base;
  limits.max_ratio = 10;
  limits.ratio_min_bytes = 1024; /* floor below bomb.zip's 4 MiB */
  expect_status(&res, run("bomb.zip", "out_limit_ratio", ZIPX_CONFLICT_FAIL,
                          &limits, &t, &res),
                ZIPX_ERR_LIMIT_RATIO, "compression ratio limit enforced");

  limits = *base;
  limits.max_file_bytes = 1024;
  expect_status(&res, run("zip64.zip", "out_limit_file", ZIPX_CONFLICT_FAIL,
                          &limits, &t, &res),
                ZIPX_ERR_LIMIT_FILE, "single file limit enforced");

  limits = *base;
  limits.max_total_bytes = 1000; /* many.zip totals ~1390 bytes */
  expect_status(&res, run("many.zip", "out_limit_total", ZIPX_CONFLICT_FAIL,
                          &limits, &t, &res),
                ZIPX_ERR_LIMIT_TOTAL, "total size limit enforced");

  limits = *base;
  limits.max_depth = 1;
  expect_status(&res, run("basic.zip", "out_limit_depth", ZIPX_CONFLICT_FAIL,
                          &limits, &t, &res),
                ZIPX_ERR_LIMIT_DEPTH, "depth limit enforced");
}

static void
test_cancel(void) {
  zipx_result_t res;
  test_ctx_t t;
  char dst[4096];

  printf("cancel leaves nothing behind\n");
  memset(&t, 0, sizeof(t));
  t.cancel_after_progress = 1;
  expect_status(&res, run("many.zip", "out_cancel", ZIPX_CONFLICT_FAIL, NULL,
                          &t, &res),
                ZIPX_ERR_CANCELED, "cancel honored");
  work_path(dst, sizeof(dst), "out_cancel");
  check(!exists(dst), "no destination after cancel");
  work_path(dst, sizeof(dst), ".");
  check(count_staging_leftovers(dst) == 0, "no staging left after cancel");
}

static void
test_many_files(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  char dst[4096];
  char file[4224];
  int missing = 0;
  int i;

  printf("500 file archive\n");
  expect_ok(&res, run("many.zip", "out_many", ZIPX_CONFLICT_FAIL, NULL, &t, &res),
            "extract succeeds");
  work_path(dst, sizeof(dst), "out_many");
  for(i = 0; i < 500; i++) {
    snprintf(file, sizeof(file), "%s/many/f%03d.txt", dst, i);
    if(!exists(file)) {
      missing++;
    }
  }
  check(!missing, "all 500 files present");
  check(res.entries_total == 500, "entry count reported");
  check(res.bytes_total > 0, "byte total reported");
}

static void
test_large_profile(void) {
  zipx_result_t res;
  test_ctx_t t = {0};
  const zipx_limits_t *base = zipx_default_limits();
  const zipx_limits_t *large = zipx_limits_profile(ZIPX_LIMITS_LARGE);
  zipx_limits_t tight;

  printf("large-file profile\n");

  /* The profile must exist and be a real struct, distinct from default. */
  check(large != NULL, "large profile returned");
  check(large != base, "large profile differs from default");
  check(zipx_limits_profile(ZIPX_LIMITS_DEFAULT) == base,
        "ZIPX_LIMITS_DEFAULT == zipx_default_limits()");

  /* Every cap in the large profile must be at least as large as the default
     cap. The profile is strictly an upper bound, never a tighter one. */
  check(large->max_entries > base->max_entries,
        "max_entries greater than default");
  check(large->max_total_bytes > base->max_total_bytes,
        "max_total_bytes greater than default");
  check(large->max_file_bytes > base->max_file_bytes,
        "max_file_bytes greater than default");
  check(large->max_ratio > base->max_ratio,
        "max_ratio greater than default");

  /* Concrete advertised numbers. If anyone ever changes the profile these
     assertions keep the public promise honest. */
  check(large->max_entries == 500000, "max_entries == 500000");
  check(large->max_file_bytes == 1ULL * 1024 * 1024 * 1024 * 1024,
        "max_file_bytes == 1 TiB");
  check(large->max_total_bytes == 4ULL * 1024 * 1024 * 1024 * 1024,
        "max_total_bytes == 4 TiB");
  check(large->max_ratio == 1000, "max_ratio == 1000");
  check(large->ratio_min_bytes == base->ratio_min_bytes,
        "ratio_min_bytes same in both profiles");
  check(base->ratio_min_bytes == 1ULL * 1024 * 1024 * 1024,
        "ratio_min_bytes == 1 GiB");

  /* Behaviour: medium_bomb.zip is 1 MiB of 0..255 cycled, compressing to
     ~4 KiB (ratio ~238). Default ratio cap 500 accepts it; large ratio
     cap 1000 also accepts it. This shows that real-world high-ratio
     archives (think raw image dumps, fat binaries) are not artificially
     blocked by the relaxed default. */
  printf("ratio cap\n");
  expect_ok(&res, run("medium_bomb.zip", "out_ratio_medium_default",
                      ZIPX_CONFLICT_FAIL, NULL, &t, &res),
            "default ratio cap (500) accepts medium_bomb.zip (~238:1)");
  expect_ok(&res, run("medium_bomb.zip", "out_ratio_medium_large",
                      ZIPX_CONFLICT_FAIL, large, &t, &res),
            "large ratio cap (1000) accepts medium_bomb.zip (~238:1)");

  /* bomb.zip is 4 MiB of identical 'A' bytes (~1026:1). Although its ratio
     exceeds both caps, it is far below the 1 GiB ratio_min_bytes floor, so
     it is accepted: small highly-compressible entries are common in
     legitimate archives (zero-filled placeholders, sparse blobs) and are
     harmless because writes are bounded by the declared size plus the real
     free-space check. A true bomb's claimed size is what check_space() and
     the total/file caps stop, not a small file's ratio. */
  expect_ok(&res, run("bomb.zip", "out_ratio_bomb_default",
                      ZIPX_CONFLICT_FAIL, NULL, &t, &res),
            "default accepts small high-ratio bomb.zip (below ratio_min_bytes)");
  expect_ok(&res, run("bomb.zip", "out_ratio_bomb_large",
                      ZIPX_CONFLICT_FAIL, large, &t, &res),
            "large accepts small high-ratio bomb.zip (below ratio_min_bytes)");

  /* The ratio screen still fires once the entry clears the floor: lowering
     the floor below bomb.zip's 4 MiB re-arms the caps. */
  tight = *base;
  tight.max_ratio = 500;
  tight.ratio_min_bytes = 1024;
  expect_status(&res, run("bomb.zip", "out_ratio_bomb_floor",
                          ZIPX_CONFLICT_FAIL, &tight, &t, &res),
                ZIPX_ERR_LIMIT_RATIO,
                "ratio enforced once entry clears ratio_min_bytes");

  /* Lowering the user's chosen ratio below the medium bomb's actual
     ratio still rejects the archive (floor disabled to keep the fixture
     small). The caps are still enforced; the profile just starts at a
     higher number. */
  tight = *large;
  tight.max_ratio = 200;
  tight.ratio_min_bytes = 0;
  expect_status(&res, run("medium_bomb.zip", "out_ratio_medium_tight",
                          ZIPX_CONFLICT_FAIL, &tight, &t, &res),
                ZIPX_ERR_LIMIT_RATIO,
                "user-lowered ratio (200) rejects medium_bomb.zip (~238:1)");

  /* Lowering the large profile's file cap below zip64.zip's 4 KiB still
     rejects the archive. */
  tight = *large;
  tight.max_file_bytes = 1024;
  expect_status(&res, run("zip64.zip", "out_large_file_cap",
                          ZIPX_CONFLICT_FAIL, &tight, &t, &res),
                ZIPX_ERR_LIMIT_FILE,
                "lowered large file cap still enforced");
}

int
main(int argc, char **argv) {
  if(argc < 3) {
    fprintf(stderr, "usage: %s <fixtures-dir> <work-dir>\n", argv[0]);
    return 2;
  }
  g_fixtures = argv[1];
  snprintf(g_work, sizeof(g_work), "%s", argv[2]);
  remove_dir(g_work);
  if(make_dirs(g_work)) {
    fprintf(stderr, "cannot create work dir\n");
    return 2;
  }

  test_basic();
  test_stored();
  test_zip64();
  test_unicode();
  test_conflict_fail();
  test_conflict_overwrite();
  test_conflict_merge();
  test_unsafe_names();
  test_duplicates();
  test_special_entries();
  test_unsupported();
  test_cancel();
  test_many_files();
  test_large_profile();
  test_volumes();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
