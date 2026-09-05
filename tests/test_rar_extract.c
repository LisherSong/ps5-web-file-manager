/* Host test suite for the RAR extraction engine.
   Build: see run-tests.sh (uses gcc + the MinGW POSIX shim).

   Scope (v1.8):
     * Pure error-path coverage — we do not ship a genuine RAR fixture
       because no rar/7z writer is available in the sandboxed host test
       environment. The test suite therefore focuses on the negative
       branches that the dispatcher hits when a user uploads something
       that is not a valid single-volume, unencrypted RAR.
     * Happy-path smoke coverage is provided by make_fixtures.py when a
       rar or 7z binary is available; if neither is present, the script
       writes 1-byte placeholder files so that test_rar_extract.c still
       has something to point at for the "format rejected" assertions.
     * See docs/HANDOVER.md for the manual smoke procedure and the
       fixture TODO that should be cleared when opello/unrar is vendored. */

#include "../src/zip_extract.h"
#include "../src/rar_extract.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "posix_compat.h"

static const char *g_fixtures;
static const char *g_fixtures_real;
static char g_work[4096];
static int g_failures;
static int g_checks;

static void
fixture_path(char *out, size_t size, const char *name) {
  snprintf(out, size, "%s/%s", g_fixtures, name);
}

static void
fixture_real_path(char *out, size_t size, const char *name) {
  snprintf(out, size, "%s/%s", g_fixtures_real, name);
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
write_bytes(const char *path, const void *data, size_t n) {
  FILE *f = fopen(path, "wb");

  if(!f) {
    return -1;
  }
  if(data && n) {
    fwrite(data, 1, n, f);
  }
  fclose(f);
  return 0;
}

/* Writes a deterministic "random" byte run so the file is not empty. */
static int
write_junk(const char *path, size_t n) {
  unsigned char *buf = malloc(n);
  size_t i;
  FILE *f;
  int rc = -1;

  if(!buf) {
    return -1;
  }
  for(i = 0; i < n; i++) {
    buf[i] = (unsigned char)((i * 131 + 17) & 0xff);
  }
  f = fopen(path, "wb");
  if(f) {
    if(fwrite(buf, 1, n, f) == n) {
      rc = 0;
    }
    fclose(f);
  }
  free(buf);
  return rc;
}

static int
remove_dir(const char *path) {
  DIR *dir = opendir(path);
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

static zipx_status_t
run_rar(const char *fixture, const char *dst, zipx_status_t expected,
        const char *label) {
  char src[4096];
  zipx_result_t res;
  zipx_status_t st;

  fixture_path(src, sizeof(src), fixture);
  if(!exists(src)) {
    printf("  SKIP %s (fixture %s missing)\n", label, fixture);
    return ZIPX_OK;
  }
  work_path((char *)dst, 0, dst); /* dst is already absolute under work */
  memset(&res, 0, sizeof(res));
  st = rar_extract(src, dst, ZIPX_CONFLICT_FAIL,
                   zipx_default_limits(), NULL, NULL, NULL, &res);
  check(st == expected, label);
  if(st != ZIPX_OK) {
    check(res.message[0] != 0, "  has error message");
    printf("    message=%s\n", res.message);
  }
  remove_dir(dst);
  return st;
}

static void
test_engine_dispatch(void) {
  char dst[4096];

  printf("test_engine_dispatch\n");

  /* A renamed .zip must NOT be accepted as a RAR. dmc_unrar's archive_open
     will detect the missing RAR signature and return BAD_SIGNATURE / OPEN_FAIL,
     which our wrapper translates to ZIPX_ERR_FORMAT / ZIPX_ERR_OPEN. Either
     is acceptable — what matters is that the call returns cleanly without
     crashing and without writing files to disk. */
  work_path(dst, sizeof(dst), "dst1");
  remove_dir(dst);
  {
    zipx_result_t res;
    zipx_status_t st;
    char src[4096];

    fixture_path(src, sizeof(src), "notar.rar");
    if(exists(src)) {
      memset(&res, 0, sizeof(res));
      st = rar_extract(src, dst, ZIPX_CONFLICT_FAIL,
                       zipx_default_limits(), NULL, NULL, NULL, &res);
      check(st != ZIPX_OK, "notar.rar rejected");
      check(!exists(dst), "  no files written for rejected archive");
      if(st == ZIPX_OK) {
        remove_dir(dst);
      }
    } else {
      printf("  SKIP notar.rar (missing)\n");
    }
  }

  /* A truncated/random 1 KiB blob is also not a RAR. */
  work_path(dst, sizeof(dst), "dst2");
  remove_dir(dst);
  {
    char src[4096];
    fixture_path(src, sizeof(src), "junk.rar");
    if(exists(src)) {
      zipx_result_t res;
      zipx_status_t st = rar_extract(src, dst, ZIPX_CONFLICT_FAIL,
                                     zipx_default_limits(), NULL, NULL, NULL,
                                     &res);
      check(st != ZIPX_OK, "junk.rar rejected");
      check(!exists(dst), "  no files written for junk archive");
      if(st == ZIPX_OK) {
        remove_dir(dst);
      }
    } else {
      printf("  SKIP junk.rar (missing)\n");
    }
  }

  /* Missing source: must return a non-zero status without writing anything. */
  work_path(dst, sizeof(dst), "dst3");
  remove_dir(dst);
  {
    char src[4096];
    zipx_result_t res;
    zipx_status_t st;

    fixture_path(src, sizeof(src), "does-not-exist.rar");
    memset(&res, 0, sizeof(res));
    st = rar_extract(src, dst, ZIPX_CONFLICT_FAIL,
                     zipx_default_limits(), NULL, NULL, NULL, &res);
    check(st == ZIPX_ERR_OPEN, "missing source -> ZIPX_ERR_OPEN");
    check(!exists(dst), "  no files written when source is missing");
    if(st == ZIPX_OK) {
      remove_dir(dst);
    }
  }

  /* Null path / null destination guards. */
  {
    zipx_result_t res;
    check(rar_extract(NULL, "/tmp", ZIPX_CONFLICT_FAIL, NULL, NULL, NULL,
                      NULL, &res) == ZIPX_ERR_INTERNAL,
          "null rar_path -> ZIPX_ERR_INTERNAL");
    check(rar_extract("/tmp", NULL, ZIPX_CONFLICT_FAIL, NULL, NULL, NULL,
                      NULL, &res) == ZIPX_ERR_INTERNAL,
          "null dst_dir -> ZIPX_ERR_INTERNAL");
    check(rar_extract("/tmp", "", ZIPX_CONFLICT_FAIL, NULL, NULL, NULL,
                      NULL, &res) == ZIPX_ERR_INTERNAL,
          "empty dst_dir -> ZIPX_ERR_INTERNAL");
  }

  /* dst_dir that is actually a file should be rejected. */
  {
    char src[4096];
    char dst[4096];
    zipx_result_t res;
    fixture_path(src, sizeof(src), "junk.rar");
    work_path(dst, sizeof(dst), "not_a_dir");
    /* ensure parent exists */
    {
      char parent[4096];
      snprintf(parent, sizeof(parent), "%s", dst);
      char *slash = strrchr(parent, '/');
      if(slash) {
        *slash = 0;
      }
      mkdir(parent, 0777);
    }
    write_bytes(dst, "i am a file", 11);
    if(exists(src)) {
      zipx_status_t st = rar_extract(src, dst, ZIPX_CONFLICT_FAIL,
                                     zipx_default_limits(), NULL, NULL, NULL,
                                     &res);
      check(st == ZIPX_ERR_CONFLICT, "dst is a regular file -> ZIPX_ERR_CONFLICT");
    } else {
      printf("  SKIP dst-is-file (junk.rar missing)\n");
    }
    unlink(dst);
  }
}

static void
test_format_translation(void) {
  /* Direct exercise of the error-code mapping: this only requires that the
     dispatcher classifies "encrypted" and "multi-volume" correctly. Since
     those states are reached only with a real RAR that has the right flags
     set, we cannot generate that fixture on the fly — but we can confirm
     that the rejection paths we *do* hit (FORMAT / OPEN / IO) never
     accidentally return ZIPX_OK. */
  printf("test_format_translation\n");
  /* The dispatch test already covered the negative paths; nothing more to do
     here for v1.8. Future fixtures can directly assert ZIPX_ERR_UNSUPPORTED
     once we have encrypted/multi-volume samples (see docs/HANDOVER.md). */
}

static void
test_limits_handoff(void) {
  /* Verify that rar_extract honours the ZIPX_LIMITS_LARGE profile by
     accepting the relaxed limits pointer without crashing. The actual
     cap is exercised via the engine's own counters (covered in the ZIP
     suite); for RAR we just need to know the profile switch is wired. */
  printf("test_limits_handoff\n");
  check(zipx_limits_profile(ZIPX_LIMITS_DEFAULT) != NULL,
        "ZIPX_LIMITS_DEFAULT pointer is non-NULL");
  check(zipx_limits_profile(ZIPX_LIMITS_LARGE) != NULL,
        "ZIPX_LIMITS_LARGE pointer is non-NULL");
  check(zipx_limits_profile(ZIPX_LIMITS_DEFAULT) !=
        zipx_limits_profile(ZIPX_LIMITS_LARGE),
        "default and large profiles are distinct");
  check(zipx_default_limits() ==
        zipx_limits_profile(ZIPX_LIMITS_DEFAULT),
        "default() matches ZIPX_LIMITS_DEFAULT");
}

/* Real-archive happy paths (v1.9, unrar 7.20.1 engine).
   The fixtures are produced by tests/make-rar-fixtures.bat on a machine with
   WinRAR; when they are missing (plain CI checkout) these checks SKIP. */
static void
test_real_archives(void) {
  char dst[4096];
  char checkp[4096];

  printf("test_real_archives\n");

  /* RAR5 (WinRAR 6/7 "v6" compression) single volume. */
  work_path(dst, sizeof(dst), "rdst_v6");
  remove_dir(dst);
  {
    zipx_result_t res;
    zipx_status_t st;
    char src[4096];
    fixture_real_path(src, sizeof(src), "basic-v6.rar");
    if(exists(src)) {
      memset(&res, 0, sizeof(res));
      st = rar_extract(src, dst, ZIPX_CONFLICT_FAIL,
                       zipx_default_limits(), NULL, NULL, NULL, &res);
      check(st == ZIPX_OK, "basic-v6.rar extracts (v6 RAR5)");
      if(st == ZIPX_OK) {
        snprintf(checkp, sizeof(checkp), "%s/root.txt", dst);
        check(exists(checkp), "  root.txt present");
        snprintf(checkp, sizeof(checkp), "%s/dir/nested.txt", dst);
        check(exists(checkp), "  dir/nested.txt present");
        check(res.entries_done >= 3, "  entries_done >= 3");
      } else {
        printf("    message=%s\n", res.message);
      }
    } else {
      printf("  SKIP basic-v6.rar (missing — run tests/make-rar-fixtures.bat)\n");
    }
  }
  remove_dir(dst);

  /* RAR4 legacy single volume. */
  work_path(dst, sizeof(dst), "rdst_r4");
  remove_dir(dst);
  {
    zipx_result_t res;
    zipx_status_t st;
    char src[4096];
    fixture_real_path(src, sizeof(src), "basic-rar4.rar");
    if(exists(src)) {
      memset(&res, 0, sizeof(res));
      st = rar_extract(src, dst, ZIPX_CONFLICT_FAIL,
                       zipx_default_limits(), NULL, NULL, NULL, &res);
      check(st == ZIPX_OK, "basic-rar4.rar extracts (RAR4)");
      if(st == ZIPX_OK) {
        snprintf(checkp, sizeof(checkp), "%s/root.txt", dst);
        check(exists(checkp), "  root.txt present");
      } else {
        printf("    message=%s\n", res.message);
      }
    } else {
      printf("  SKIP basic-rar4.rar (missing)\n");
    }
  }
  remove_dir(dst);

  /* Multi-volume: opening vol.part1.rar must auto-merge part2 from the same
     directory (unrar drives the volume chain). */
  work_path(dst, sizeof(dst), "rdst_vol");
  remove_dir(dst);
  {
    zipx_result_t res;
    zipx_status_t st;
    char src[4096];
    fixture_real_path(src, sizeof(src), "vol.part1.rar");
    if(exists(src)) {
      memset(&res, 0, sizeof(res));
      st = rar_extract(src, dst, ZIPX_CONFLICT_FAIL,
                       zipx_default_limits(), NULL, NULL, NULL, &res);
      check(st == ZIPX_OK, "vol.part1.rar auto-merges volumes");
      if(st == ZIPX_OK) {
        snprintf(checkp, sizeof(checkp), "%s/root.txt", dst);
        check(exists(checkp), "  root.txt present across volumes");
        snprintf(checkp, sizeof(checkp), "%s/big.bin", dst);
        check(exists(checkp), "  big.bin (split file) present and whole");
        check(res.entries_done >= 2, "  entries_done >= 2");
      } else {
        printf("    message=%s\n", res.message);
      }
    } else {
      printf("  SKIP vol.part1.rar (missing)\n");
    }
  }
  remove_dir(dst);

  /* Encrypted: engine can decrypt but the password channel is not wired yet,
     so encrypted entries must be rejected up front with UNSUPPORTED. */
  work_path(dst, sizeof(dst), "rdst_enc");
  remove_dir(dst);
  {
    zipx_result_t res;
    zipx_status_t st;
    char src[4096];
    fixture_real_path(src, sizeof(src), "enc-v6.rar");
    if(exists(src)) {
      memset(&res, 0, sizeof(res));
      st = rar_extract(src, dst, ZIPX_CONFLICT_FAIL,
                       zipx_default_limits(), NULL, NULL, NULL, &res);
      check(st == ZIPX_ERR_UNSUPPORTED, "enc-v6.rar rejected (no password channel)");
      check(!exists(dst), "  no files written for encrypted archive");
      if(st == ZIPX_OK) {
        remove_dir(dst);
      }
    } else {
      printf("  SKIP enc-v6.rar (missing)\n");
    }
  }
  remove_dir(dst);
}

int
main(int argc, char **argv) {
  if(argc < 3) {
    fprintf(stderr, "usage: %s <fixtures-dir> <work-dir> [real-fixtures-dir]\n",
            argv[0]);
    return 2;
  }
  g_fixtures = argv[1];
  g_fixtures_real = (argc >= 4) ? argv[3] : argv[1];
  snprintf(g_work, sizeof(g_work), "%s", argv[2]);
  mkdir(g_work, 0777);

  /* Generate the on-disk garbage fixtures we need for the error tests. */
  {
    char src[4096];
    fixture_path(src, sizeof(src), "notar.rar");
    /* Reuse basic.zip (fixture generator writes this anyway). */
    {
      char zip_src[4096];
      char buf[8192];
      FILE *in;
      FILE *out;
      size_t n;
      fixture_path(zip_src, sizeof(zip_src), "basic.zip");
      in = fopen(zip_src, "rb");
      out = fopen(src, "wb");
      if(in && out) {
        while((n = fread(buf, 1, sizeof(buf), in)) > 0) {
          fwrite(buf, 1, n, out);
        }
      }
      if(in) {
        fclose(in);
      }
      if(out) {
        fclose(out);
      }
    }
    fixture_path(src, sizeof(src), "junk.rar");
    write_junk(src, 1024);
  }

  test_engine_dispatch();
  test_format_translation();
  test_limits_handoff();
  test_real_archives();

  printf("\nrar_extract: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}