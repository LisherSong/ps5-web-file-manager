/* Safe RAR extraction engine used by the /api/extract task.
   Wraps the vendored dmc_unrar library (https://github.com/DrMcCoy/dmc_unrar).

   The publish / staging / rollback / normalize-name / dedup machinery is
   mirrored from zip_extract.c so that any consumer of zipx_result_t gets a
   consistent error and progress contract regardless of archive format.

   See third_party/unrar/VENDORED.md for what dmc_unrar does and does not
   support (single-volume RAR only; multi-volume and encrypted are
   rejected up front). */

#include "rar_extract.h"
#include "zip_extract.h"  /* for the shared status / progress / limits API */

#include "dmc_unrar_api.h"  /* facade — links against dmc_unrar.o at build time */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* (intentionally no `#include "dmc_unrar.c"` here — that would pull the
   library into this translation unit, where the host test build's
   tests/posix_compat.h renames `open` / `close` to `wfm_open` / `wfm_close`
   and breaks dmc_unrar's `dmc_unrar_io_handler` struct member access.
   dmc_unrar.c is compiled as its own translation unit and joined at link.) */

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define RARX_TMP_ATTEMPTS 64
#define RARX_STAGING_PREFIX ".wfm-extract-"
#define RARX_PUBLISH_MAX_DEPTH 128
#define RARX_SPACE_SLACK_PER_ENTRY 512

/**************************************************************************
 * context + small helpers (mirror of zipx_ctx_*; names prefixed rarx_)
 **************************************************************************/

typedef struct {
  zipx_conflict_t conflict;
  zipx_limits_t limits;
  zipx_cancel_fn cancel;
  zipx_progress_fn progress;
  void *userdata;
  zipx_result_t *result;
  uint64_t entries_total;
  uint64_t entries_done;
  uint64_t bytes_total;
  uint64_t bytes_done;
  uint64_t files_created;
  uint64_t dirs_created;
  uint64_t progress_floor;
  struct timespec last_report;
  char staging[ZIPX_PATH_MAX];
  char **created;
  size_t created_count;
  size_t created_cap;
  unsigned char *created_is_dir;
  int staging_created;
} rarx_ctx_t;

static void
rarx_set_detail(rarx_ctx_t *c, const char *path) {
  if(path) {
    snprintf(c->result->detail, sizeof(c->result->detail), "%s", path);
  }
}

static int
rarx_fail(rarx_ctx_t *c, zipx_status_t status, const char *detail,
          const char *fmt, ...) {
  va_list ap;

  c->result->status = status;
  c->result->sys_errno = errno;
  rarx_set_detail(c, detail);
  va_start(ap, fmt);
  if(fmt) {
    vsnprintf(c->result->message, sizeof(c->result->message), fmt, ap);
  }
  va_end(ap);
  if(!c->result->message[0]) {
    snprintf(c->result->message, sizeof(c->result->message), "%s",
             zipx_status_string(status));
  }
  return (int)status;
}

static long long
mono_ms(void) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
report(rarx_ctx_t *c, int phase, const char *current, int force) {
  zipx_progress_t p;
  long long now;

  if(!c->progress) {
    return;
  }
  now = mono_ms();
  if(!force && c->last_report.tv_sec &&
     now - ((long long)c->last_report.tv_sec * 1000 +
             0) < 200 &&
     c->bytes_done - c->progress_floor < (1024 * 1024)) {
    return;
  }
  clock_gettime(CLOCK_MONOTONIC, &c->last_report);
  c->progress_floor = c->bytes_done;

  p.phase = phase;
  p.entries_total = c->entries_total;
  p.entries_done = c->entries_done;
  p.bytes_total = c->bytes_total;
  p.bytes_done = c->bytes_done;
  p.current = current;
  c->progress(c->userdata, &p);
}

static int
canceled(rarx_ctx_t *c) {
  return c->cancel && c->cancel(c->userdata);
}

static int
remember_published(rarx_ctx_t *c, const char *path, int is_dir) {
  if(c->created_count == c->created_cap) {
    size_t cap = c->created_cap ? c->created_cap * 2 : 64;
    char **paths = realloc(c->created, cap * sizeof(*paths));
    unsigned char *flags = realloc(c->created_is_dir, cap * sizeof(*flags));

    if(!paths || !flags) {
      free(paths);
      free(flags);
      return -1;
    }
    c->created = paths;
    c->created_is_dir = flags;
    c->created_cap = cap;
  }
  if(!(c->created[c->created_count] = strdup(path))) {
    return -1;
  }
  c->created_is_dir[c->created_count] = is_dir ? 1 : 0;
  c->created_count++;
  return 0;
}

static void
free_published(rarx_ctx_t *c) {
  size_t i;

  for(i = 0; i < c->created_count; i++) {
    free(c->created[i]);
  }
  free(c->created);
  free(c->created_is_dir);
  c->created = NULL;
  c->created_is_dir = NULL;
  c->created_count = c->created_cap = 0;
}

/**************************************************************************
 * duplicate detection (FNV-1a hash table; same trick as zip_extract.c)
 **************************************************************************/

typedef struct {
  uint64_t *hash;
  unsigned char *is_dir;
  size_t cap;
  size_t count;
} rarx_nameset_t;

static uint64_t
rarx_fnv1a(const char *s, size_t len) {
  uint64_t h = 1469598103934665603ULL;
  size_t i;

  for(i = 0; i < len; i++) {
    h ^= (unsigned char)s[i];
    h *= 1099511628211ULL;
  }
  return h ? h : 1;
}

static int
nameset_init(rarx_nameset_t *set, size_t hint) {
  size_t cap = 1024;

  while(cap < hint * 2) {
    cap *= 2;
  }
  set->hash = calloc(cap, sizeof(*set->hash));
  set->is_dir = calloc(cap, sizeof(*set->is_dir));
  if(!set->hash || !set->is_dir) {
    free(set->hash);
    free(set->is_dir);
    set->hash = NULL;
    set->is_dir = NULL;
    return -1;
  }
  set->cap = cap;
  set->count = 0;
  return 0;
}

static void
nameset_free(rarx_nameset_t *set) {
  free(set->hash);
  free(set->is_dir);
  set->hash = NULL;
  set->is_dir = NULL;
  set->cap = set->count = 0;
}

static size_t
nameset_slot(rarx_nameset_t *set, uint64_t h) {
  size_t mask = set->cap - 1;
  size_t i = (size_t)(h & mask);

  while(set->hash[i] && set->hash[i] != h) {
    i = (i + 1) & mask;
  }
  return i;
}

static int
nameset_grow(rarx_nameset_t *set) {
  rarx_nameset_t bigger;
  size_t i;

  if(nameset_init(&bigger, set->cap)) {
    return -1;
  }
  for(i = 0; i < set->cap; i++) {
    if(!set->hash[i]) {
      continue;
    }
    bigger.hash[nameset_slot(&bigger, set->hash[i])] = set->hash[i];
    bigger.is_dir[nameset_slot(&bigger, set->hash[i])] = set->is_dir[i];
    bigger.count++;
  }
  nameset_free(set);
  *set = bigger;
  return 0;
}

static int
nameset_get(rarx_nameset_t *set, uint64_t h) {
  size_t i = nameset_slot(set, h);
  if(!set->hash[i]) {
    return 0;
  }
  return set->is_dir[i] ? 1 : 2;
}

static int
nameset_put(rarx_nameset_t *set, uint64_t h, int is_dir) {
  size_t i;

  if(set->count * 4 >= set->cap * 3 && nameset_grow(set)) {
    return -1;
  }
  i = nameset_slot(set, h);
  if(!set->hash[i]) {
    set->hash[i] = h;
    set->count++;
  }
  set->is_dir[i] = is_dir ? 1 : 0;
  return 0;
}

/**************************************************************************
 * entry name validation (mirrors zip_extract.c::normalize_name but the
 * RAR side does not have a trailing separator, so dir entries are only
 * detected via dmc_unrar_file_is_directory()).
 **************************************************************************/

static int
normalize_name(const char *in, char *out, size_t out_size, int *is_dir,
               uint32_t *depth, rarx_ctx_t *c) {
  size_t in_len = strlen(in);
  size_t out_len = 0;
  size_t i = 0;
  size_t seg_len = 0;
  uint32_t levels = 0;

  *is_dir = 0;
  *depth = 0;
  if(!in_len) {
    return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, in, "empty entry name");
  }
  if(in_len > c->limits.max_path_len) {
    return rarx_fail(c, ZIPX_ERR_LIMIT_NAME, in, "entry name is too long");
  }
  if(in[0] == '/' || in[0] == '\\') {
    return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, in, "absolute entry name");
  }
  if(in_len >= 2 && ((in[0] >= 'A' && in[0] <= 'Z') ||
                     (in[0] >= 'a' && in[0] <= 'z')) && in[1] == ':') {
    return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, in, "drive letter in entry name");
  }

  while(i < in_len) {
    char ch = in[i];

    if((unsigned char)ch < 0x20 || (unsigned char)ch == 0x7f) {
      return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, in,
                       "control character in entry name");
    }
    if(ch == '\\') {
      /* RAR technically permits '\' as a separator on Windows archives. We
         normalize to '/' so the staging tree is consistent. */
      ch = '/';
    }
    if(ch == '/') {
      if(!seg_len) {
        return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, in, "empty path segment");
      }
      if(seg_len == 1 && out[out_len - 1] == '.') {
        return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, in, "'.' path segment");
      }
      if(seg_len == 2 && !memcmp(out + out_len - 2, "..", 2)) {
        return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, in, "'..' path segment");
      }
      if(seg_len > c->limits.max_name_len) {
        return rarx_fail(c, ZIPX_ERR_LIMIT_NAME, in, "path component is too long");
      }
      out[out_len++] = '/';
      levels++;
      seg_len = 0;
      i++;
      continue;
    }
    if(out_len + 2 >= out_size) {
      return rarx_fail(c, ZIPX_ERR_LIMIT_NAME, in, "entry name is too long");
    }
    out[out_len++] = ch;
    seg_len++;
    i++;
  }

  if(!seg_len) {
    return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, in, "empty entry name");
  }
  if(seg_len == 1 && out[out_len - 1] == '.') {
    return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, in, "'.' path segment");
  }
  if(seg_len == 2 && !memcmp(out + out_len - 2, "..", 2)) {
    return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, in, "'..' path segment");
  }
  if(seg_len > c->limits.max_name_len) {
    return rarx_fail(c, ZIPX_ERR_LIMIT_NAME, in, "path component is too long");
  }
  out[out_len] = 0;
  levels++;

  if(levels > c->limits.max_depth) {
    return rarx_fail(c, ZIPX_ERR_LIMIT_DEPTH, in, "entry path is too deep");
  }
  *depth = levels;
  return 0;
}

static int
nameset_add_path(rarx_nameset_t *set, const char *name, int is_dir,
                 rarx_ctx_t *c) {
  char buf[ZIPX_PATH_MAX];
  size_t len = strlen(name);
  size_t i;

  if(len >= sizeof(buf)) {
    return rarx_fail(c, ZIPX_ERR_LIMIT_NAME, name, "entry name is too long");
  }
  memcpy(buf, name, len + 1);

  for(i = 0; i < len; i++) {
    if(buf[i] != '/') {
      continue;
    }
    buf[i] = 0;
    if(nameset_get(set, rarx_fnv1a(buf, i)) == 2) {
      return rarx_fail(c, ZIPX_ERR_DUPLICATE, name,
                       "entry uses a file as a directory");
    }
    if(nameset_put(set, rarx_fnv1a(buf, i), 1)) {
      return rarx_fail(c, ZIPX_ERR_INTERNAL, name, "out of memory");
    }
    buf[i] = '/';
  }

  if(nameset_put(set, rarx_fnv1a(name, len), is_dir)) {
    return rarx_fail(c, ZIPX_ERR_INTERNAL, name, "out of memory");
  }
  return 0;
}

static int
nameset_check_duplicate(rarx_nameset_t *set, const char *name, int is_dir,
                        rarx_ctx_t *c) {
  int existing = nameset_get(set, rarx_fnv1a(name, strlen(name)));

  if(existing == 2 || (existing == 1 && !is_dir)) {
    return rarx_fail(c, ZIPX_ERR_DUPLICATE, name, "duplicate entry name");
  }
  return 0;
}

/**************************************************************************
 * dmc_unrar -> zipx error translation
 **************************************************************************/

static int
rar_translate_error(dmc_unrar_return code, const char *detail,
                    rarx_ctx_t *c) {
  switch(code) {
  case DMC_UNRAR_OK:
    return ZIPX_OK;

  /* Archive-open level (the user will see this when they upload a
     multi-volume or fully-encrypted RAR — let them know it is on purpose). */
  case DMC_UNRAR_ARCHIVE_UNSUPPORTED_VOLUMES:
    return rarx_fail(c, ZIPX_ERR_UNSUPPORTED, detail,
                     "multi-volume RAR archives are not supported; "
                     "please extract on a PC first");

  case DMC_UNRAR_ARCHIVE_UNSUPPORTED_ENCRYPTED:
    return rarx_fail(c, ZIPX_ERR_UNSUPPORTED, detail,
                     "encrypted RAR archives are not supported; "
                     "please extract on a PC first");

  /* Per-file (still surfaced up-front during scan). */
  case DMC_UNRAR_FILE_UNSUPPORTED_ENCRYPTED:
    return rarx_fail(c, ZIPX_ERR_UNSUPPORTED, detail,
                     "encrypted RAR entries are not supported");

  case DMC_UNRAR_FILE_UNSUPPORTED_SPLIT:
    return rarx_fail(c, ZIPX_ERR_UNSUPPORTED, detail,
                     "split RAR entries are not supported");

  case DMC_UNRAR_FILE_UNSUPPORTED_LINK:
    return rarx_fail(c, ZIPX_ERR_SPECIAL, detail, "symbolic link entry");

  case DMC_UNRAR_FILE_UNSUPPORTED_VERSION:
  case DMC_UNRAR_FILE_UNSUPPORTED_METHOD:
    return rarx_fail(c, ZIPX_ERR_UNSUPPORTED, detail,
                     "RAR entry uses an unsupported compression method");

  case DMC_UNRAR_FILE_UNSUPPORTED_LARGE:
    return rarx_fail(c, ZIPX_ERR_LIMIT_FILE, detail,
                     "RAR entry is larger than the supported maximum");

  case DMC_UNRAR_ARCHIVE_EMPTY:
    return rarx_fail(c, ZIPX_ERR_FORMAT, detail, "empty RAR archive");

  case DMC_UNRAR_ARCHIVE_UNSUPPORTED_ANCIENT:
    return rarx_fail(c, ZIPX_ERR_UNSUPPORTED, detail,
                     "RAR 1.4 / 1.5 archives are not supported");

  case DMC_UNRAR_ARCHIVE_NOT_RAR:
    return rarx_fail(c, ZIPX_ERR_FORMAT, detail,
                     "not a RAR archive");

  case DMC_UNRAR_OPEN_FAIL:
    return rarx_fail(c, ZIPX_ERR_OPEN, detail, "%s", strerror(errno));

  case DMC_UNRAR_READ_FAIL:
  case DMC_UNRAR_WRITE_FAIL:
  case DMC_UNRAR_SEEK_FAIL:
    return rarx_fail(c, ZIPX_ERR_IO, detail, "%s", strerror(errno));

  case DMC_UNRAR_FILE_CRC32_FAIL:
    return rarx_fail(c, ZIPX_ERR_CRC, detail, "CRC32 mismatch");

  case DMC_UNRAR_INVALID_DATA:
  case DMC_UNRAR_NO_ALLOC:
  case DMC_UNRAR_ALLOC_FAIL:
  case DMC_UNRAR_ARCHIVE_IS_NULL:
  case DMC_UNRAR_ARCHIVE_NOT_CLEARED:
  case DMC_UNRAR_ARCHIVE_MISSING_FIELDS:
  default:
    return rarx_fail(c, ZIPX_ERR_FORMAT, detail,
                     "invalid or corrupt RAR archive");
  }
}

/**************************************************************************
 * staging helpers (same as zip_extract.c)
 **************************************************************************/

static int
path_parent(const char *path, char *out, size_t out_size) {
  size_t len = strlen(path);
  size_t i;

  if(!len) {
    return -1;
  }
  i = len;
  while(i && path[i - 1] == '/') {
    i--;
  }
  while(i && path[i - 1] != '/') {
    i--;
  }
  while(i > 1 && path[i - 1] == '/') {
    i--;
  }
  if(!i) {
    return -1;
  }
  if(i >= out_size) {
    return -1;
  }
  memcpy(out, path, i);
  out[i] = 0;
  return 0;
}

static void
chmod_0777_fd(int fd) {
  int err = errno;

  if(fchmod(fd, 0777)) {
    errno = err;
  }
}

static int
make_staging(rarx_ctx_t *c, const char *parent) {
  unsigned int attempt;

  for(attempt = 0; attempt < RARX_TMP_ATTEMPTS; attempt++) {
    int n = snprintf(c->staging, sizeof(c->staging), "%s/%s%ld-%lld-%u",
                     parent, RARX_STAGING_PREFIX, (long)getpid(),
                     (long long)time(NULL), attempt);

    if(n < 0 || (size_t)n >= sizeof(c->staging)) {
      return rarx_fail(c, ZIPX_ERR_INTERNAL, NULL, "staging path is too long");
    }
    if(!mkdir(c->staging, 0777)) {
      c->staging_created = 1;
      return 0;
    }
    if(errno != EEXIST) {
      return rarx_fail(c, ZIPX_ERR_IO, c->staging, "cannot create staging: %s",
                       strerror(errno));
    }
  }
  return rarx_fail(c, ZIPX_ERR_IO, parent, "cannot create staging directory");
}

static void
remove_tree(const char *path) {
  DIR *dir = opendir(path);
  struct dirent *ent;

  if(!dir) {
    rmdir(path);
    return;
  }
  while((ent = readdir(dir))) {
    char child[ZIPX_PATH_MAX];
    struct stat st;

    if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
      continue;
    }
    if(snprintf(child, sizeof(child), "%s/%s", path, ent->d_name) >=
         (int)sizeof(child)) {
      continue;
    }
    if(!lstat(child, &st) && S_ISDIR(st.st_mode)) {
      remove_tree(child);
    } else {
      unlink(child);
    }
  }
  closedir(dir);
  rmdir(path);
}

static void
cleanup_staging(rarx_ctx_t *c) {
  if(c->staging_created) {
    remove_tree(c->staging);
    c->staging_created = 0;
  }
}

static void
rollback_published(rarx_ctx_t *c) {
  size_t i = c->created_count;

  while(i) {
    i--;
    if(c->created_is_dir[i]) {
      rmdir(c->created[i]);
    } else {
      unlink(c->created[i]);
    }
  }
}

/* mkdir -p: ensures every segment below root_fd exists. Returns the open
   descriptor for the deepest directory. */
static int
open_parent_dirs(int root_fd, const char *rel, rarx_ctx_t *c) {
  char buf[ZIPX_PATH_MAX];
  int fd = root_fd;
  char *seg;
  char *save = NULL;

  if(strlen(rel) >= sizeof(buf)) {
    rarx_fail(c, ZIPX_ERR_LIMIT_NAME, rel, "path is too long");
    return -1;
  }
  strcpy(buf, rel);

  for(seg = strtok_r(buf, "/", &save); seg; seg = strtok_r(NULL, "/", &save)) {
    int next;

    if(!mkdirat(fd, seg, 0777)) {
      next = openat(fd, seg, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      if(next >= 0) {
        chmod_0777_fd(next);
        c->dirs_created++;
      }
    } else if(errno == EEXIST) {
      next = openat(fd, seg, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    } else {
      rarx_fail(c, ZIPX_ERR_IO, rel, "cannot create directory '%s': %s", seg,
               strerror(errno));
      if(fd != root_fd) {
        close(fd);
      }
      return -1;
    }

    if(next < 0) {
      rarx_fail(c, ZIPX_ERR_IO, rel, "cannot open directory '%s': %s", seg,
               strerror(errno));
      if(fd != root_fd) {
        close(fd);
      }
      return -1;
    }
    if(fd != root_fd) {
      close(fd);
    }
    fd = next;
  }
  return fd;
}

/**************************************************************************
 * scan phase
 **************************************************************************/

static int
check_space(rarx_ctx_t *c, const char *target) {
  struct statvfs vfs;
  unsigned long long available;
  unsigned long long block;
  unsigned long long required = c->bytes_total +
                                c->entries_total * RARX_SPACE_SLACK_PER_ENTRY;

  if(statvfs(target, &vfs)) {
    return rarx_fail(c, ZIPX_ERR_SPACE, target, "cannot read free space: %s",
                     strerror(errno));
  }
  block = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
  available = (unsigned long long)vfs.f_bavail * block;
  if(available < required) {
    return rarx_fail(c, ZIPX_ERR_SPACE, target,
                     "not enough space, required %llu bytes, available %llu bytes",
                     required, available);
  }
  return 0;
}

static int
scan_archive(dmc_unrar_archive *rar, rarx_ctx_t *c) {
  rarx_nameset_t set;
  dmc_unrar_size_t i;
  dmc_unrar_size_t total;
  int ret = 0;

  if(nameset_init(&set, 4096)) {
    return rarx_fail(c, ZIPX_ERR_INTERNAL, NULL, "out of memory");
  }

  total = dmc_unrar_get_file_count(rar);
  for(i = 0; i < total; i++) {
    char name[ZIPX_PATH_MAX];
    char *name_buf;
    dmc_unrar_size_t name_size;
    int is_dir;
    uint32_t depth = 0;
    const dmc_unrar_file *info;
    dmc_unrar_return supported;

    info = dmc_unrar_get_file_stat(rar, i);
    if(!info) {
      ret = rarx_fail(c, ZIPX_ERR_FORMAT, NULL, "cannot read entry header");
      break;
    }
    supported = dmc_unrar_file_is_supported(rar, i);
    if(supported != DMC_UNRAR_OK) {
      /* is_supported() already returns ZIPX-mapped error via a temporary */
      char detail[64];
      snprintf(detail, sizeof(detail), "entry %llu",
               (unsigned long long)i);
      rarx_fail(c, ZIPX_ERR_UNSUPPORTED, detail, "%s",
                dmc_unrar_strerror(supported));
      ret = (int)c->result->status;
      break;
    }
    is_dir = dmc_unrar_file_is_directory(rar, i) ? 1 : 0;

    /* First call to learn the required size, second to fill the buffer. */
    name_size = dmc_unrar_get_filename(rar, i, NULL, 0);
    if(!name_size) {
      ret = rarx_fail(c, ZIPX_ERR_FORMAT, NULL,
                      "cannot read entry name");
      break;
    }
    name_buf = malloc(name_size);
    if(!name_buf) {
      ret = rarx_fail(c, ZIPX_ERR_INTERNAL, NULL, "out of memory");
      break;
    }
    if(dmc_unrar_get_filename(rar, i, name_buf, name_size) != name_size) {
      free(name_buf);
      ret = rarx_fail(c, ZIPX_ERR_FORMAT, NULL,
                      "truncated entry name");
      break;
    }
    /* dmc_unrar does not guarantee NUL termination; force it. */
    name_buf[name_size - 1] = 0;
    /* RAR stores UTF-8 encoded names. Replace any non-UTF-8 sequence with
       '?' to keep the downstream string well-formed. */
    dmc_unrar_unicode_make_valid_utf8(name_buf);

    if(normalize_name(name_buf, name, sizeof(name), &is_dir, &depth, c)) {
      free(name_buf);
      ret = -1;
      break;
    }
    free(name_buf);

    if(nameset_check_duplicate(&set, name, is_dir, c) ||
       nameset_add_path(&set, name, is_dir, c)) {
      ret = -1;
      break;
    }

    if(!is_dir) {
      uint64_t uncomp = info->uncompressed_size;

      if(uncomp > c->limits.max_file_bytes) {
        ret = rarx_fail(c, ZIPX_ERR_LIMIT_FILE, name,
                       "entry is larger than %llu bytes",
                       (unsigned long long)c->limits.max_file_bytes);
        break;
      }
      /* RAR headers do not reliably expose compressed_size for all formats,
         so the safe compression-ratio check is skipped. */
      if(uncomp >= c->limits.max_total_bytes ||
         c->bytes_total > c->limits.max_total_bytes - uncomp) {
        ret = rarx_fail(c, ZIPX_ERR_LIMIT_TOTAL, name,
                       "archive contents are larger than %llu bytes",
                       (unsigned long long)c->limits.max_total_bytes);
        break;
      }
      c->bytes_total += uncomp;
    }

    c->entries_total++;
    if(c->entries_total > c->limits.max_entries) {
      ret = rarx_fail(c, ZIPX_ERR_LIMIT_ENTRIES, name,
                     "archive has more than %llu entries",
                     (unsigned long long)c->limits.max_entries);
      break;
    }
    if(c->entries_total % 4096 == 0) {
      if(canceled(c)) {
        ret = rarx_fail(c, ZIPX_ERR_CANCELED, name, NULL);
        break;
      }
      report(c, ZIPX_PHASE_SCAN, name, 0);
    }
  }

  nameset_free(&set);
  return ret;
}

/**************************************************************************
 * extract phase
 **************************************************************************/

/* dmc_unrar_extract_file_to_path() opens the destination file with fopen()
   without creating parent directories. We always call it against an
   already-prepared full path inside the staging tree. */
static int
extract_one(dmc_unrar_archive *rar, rarx_ctx_t *c, int root_fd,
            const char *name, dmc_unrar_size_t index, uint64_t declared) {
  char *full_path;
  char dir_part[ZIPX_PATH_MAX];
  char base[ZIPX_PATH_MAX];
  char *slash;
  int dir_fd;
  dmc_unrar_return dr;
  struct stat st;
  uint64_t before_bytes;
  int ret = 0;

  snprintf(dir_part, sizeof(dir_part), "%s", name);
  slash = strrchr(dir_part, '/');
  if(slash) {
    snprintf(base, sizeof(base), "%s", slash + 1);
    *slash = 0;
  } else {
    snprintf(base, sizeof(base), "%s", name);
    dir_part[0] = 0;
  }
  if(!base[0]) {
    return rarx_fail(c, ZIPX_ERR_UNSAFE_NAME, name, "empty file name");
  }

  dir_fd = open_parent_dirs(root_fd, dir_part, c);
  if(dir_fd < 0) {
    return -1;
  }

  full_path = malloc(ZIPX_PATH_MAX);
  if(!full_path) {
    rarx_fail(c, ZIPX_ERR_INTERNAL, name, "out of memory");
    if(dir_fd != root_fd) {
      close(dir_fd);
    }
    return -1;
  }
  if(dir_part[0]) {
    snprintf(full_path, ZIPX_PATH_MAX, "%s/%s/%s",
             c->staging, dir_part, base);
  } else {
    snprintf(full_path, ZIPX_PATH_MAX, "%s/%s", c->staging, base);
  }

  before_bytes = c->bytes_done;
  dr = dmc_unrar_extract_file_to_path(rar, index, full_path, NULL, true);
  if(dr != DMC_UNRAR_OK) {
    rar_translate_error(dr, name, c);
    ret = -1;
    goto done;
  }

  if(stat(full_path, &st)) {
    rarx_fail(c, ZIPX_ERR_IO, name, "cannot stat extracted file: %s",
              strerror(errno));
    ret = -1;
    goto done;
  }
  if((uint64_t)st.st_size > declared + declared + (64 * 1024)) {
    /* Defensive: the on-disk size should never wildly exceed the declared
       size. The cap is generous so PPMd + headers do not trip it. */
    rarx_fail(c, ZIPX_ERR_LIMIT_FILE, name,
              "extracted file is larger than declared");
    unlink(full_path);
    ret = -1;
    goto done;
  }
  if((uint64_t)st.st_size > c->limits.max_total_bytes - c->bytes_done) {
    rarx_fail(c, ZIPX_ERR_LIMIT_TOTAL, name,
              "archive contents are larger than %llu bytes",
              (unsigned long long)c->limits.max_total_bytes);
    unlink(full_path);
    ret = -1;
    goto done;
  }
  c->bytes_done += (uint64_t)st.st_size;
  /* If dmc_unrar did not advance bytes_done above (e.g. declared == 0 for a
     directory or zero-byte file), credit the bytes_total estimate so the
     progress bar keeps moving. */
  if(c->bytes_done == before_bytes && declared) {
    c->bytes_done += declared;
  }

done:
  free(full_path);
  if(dir_fd != root_fd) {
    close(dir_fd);
  }
  if(!ret) {
    c->files_created++;
  }
  return ret;
}

static int
extract_archive(dmc_unrar_archive *rar, rarx_ctx_t *c, int root_fd) {
  dmc_unrar_size_t i;
  dmc_unrar_size_t total = dmc_unrar_get_file_count(rar);
  int ret = 0;

  for(i = 0; i < total && !ret; i++) {
    char *name_buf = NULL;
    dmc_unrar_size_t name_size;
    char name[ZIPX_PATH_MAX];
    int is_dir;
    uint32_t depth = 0;
    const dmc_unrar_file *info;

    if(canceled(c)) {
      ret = rarx_fail(c, ZIPX_ERR_CANCELED, NULL, NULL);
      break;
    }

    info = dmc_unrar_get_file_stat(rar, i);
    if(!info) {
      ret = rarx_fail(c, ZIPX_ERR_FORMAT, NULL, "cannot read entry header");
      break;
    }
    is_dir = dmc_unrar_file_is_directory(rar, i) ? 1 : 0;

    name_size = dmc_unrar_get_filename(rar, i, NULL, 0);
    if(!name_size) {
      ret = rarx_fail(c, ZIPX_ERR_FORMAT, NULL, "cannot read entry name");
      break;
    }
    name_buf = malloc(name_size);
    if(!name_buf) {
      ret = rarx_fail(c, ZIPX_ERR_INTERNAL, NULL, "out of memory");
      break;
    }
    if(dmc_unrar_get_filename(rar, i, name_buf, name_size) != name_size) {
      free(name_buf);
      ret = rarx_fail(c, ZIPX_ERR_FORMAT, NULL, "truncated entry name");
      break;
    }
    name_buf[name_size - 1] = 0;
    dmc_unrar_unicode_make_valid_utf8(name_buf);

    if(normalize_name(name_buf, name, sizeof(name), &is_dir, &depth, c)) {
      free(name_buf);
      ret = -1;
      break;
    }
    free(name_buf);
    (void)depth;

    if(is_dir) {
      int dir_fd = open_parent_dirs(root_fd, name, c);
      if(dir_fd < 0) {
        ret = -1;
        break;
      }
      if(dir_fd != root_fd) {
        close(dir_fd);
      }
    } else {
      uint64_t declared = info->uncompressed_size;
      if(extract_one(rar, c, root_fd, name, i, declared)) {
        ret = -1;
        break;
      }
    }
    c->entries_done++;
    report(c, ZIPX_PHASE_EXTRACT, name, 0);
  }
  return ret;
}

/**************************************************************************
 * publish phase (identical to zip_extract.c — kept inline so that the
 * two engines stay self-contained)
 **************************************************************************/

static int publish_dir(const char *src, const char *dst, int depth,
                       rarx_ctx_t *c);

static int
publish_entry(const char *src, const char *dst, const char *name, int depth,
              rarx_ctx_t *c) {
  char src_child[ZIPX_PATH_MAX];
  char dst_child[ZIPX_PATH_MAX];
  struct stat st;
  struct stat src_st;
  int src_is_dir;

  if(snprintf(src_child, sizeof(src_child), "%s/%s", src, name) >=
       (int)sizeof(src_child) ||
     snprintf(dst_child, sizeof(dst_child), "%s/%s", dst, name) >=
       (int)sizeof(dst_child)) {
    return rarx_fail(c, ZIPX_ERR_LIMIT_NAME, name, "path is too long");
  }
  if(lstat(src_child, &src_st)) {
    return rarx_fail(c, ZIPX_ERR_IO, src_child, "cannot read staging: %s",
                     strerror(errno));
  }
  src_is_dir = S_ISDIR(src_st.st_mode) ? 1 : 0;

  if(lstat(dst_child, &st)) {
    if(errno != ENOENT) {
      return rarx_fail(c, ZIPX_ERR_IO, dst_child, "cannot check target: %s",
                       strerror(errno));
    }
    if(rename(src_child, dst_child)) {
      return rarx_fail(c, ZIPX_ERR_IO, dst_child, "cannot publish: %s",
                       strerror(errno));
    }
    return remember_published(c, dst_child, src_is_dir);
  }

  if(S_ISDIR(st.st_mode)) {
    if(!src_is_dir) {
      return rarx_fail(c, ZIPX_ERR_CONFLICT, dst_child,
                       "file collides with an existing directory");
    }
    if(c->conflict != ZIPX_CONFLICT_MERGE) {
      return rarx_fail(c, ZIPX_ERR_CONFLICT, dst_child,
                       "directory already exists");
    }
    if(depth >= RARX_PUBLISH_MAX_DEPTH) {
      return rarx_fail(c, ZIPX_ERR_LIMIT_DEPTH, dst_child, "path is too deep");
    }
    if(publish_dir(src_child, dst_child, depth + 1, c)) {
      return -1;
    }
    rmdir(src_child);
    return 0;
  }

  if(S_ISREG(st.st_mode)) {
    if(!src_is_dir) {
      if(c->conflict == ZIPX_CONFLICT_OVERWRITE) {
        if(rename(src_child, dst_child)) {
          return rarx_fail(c, ZIPX_ERR_IO, dst_child, "cannot replace: %s",
                           strerror(errno));
        }
        return remember_published(c, dst_child, 0);
      }
      if(c->conflict == ZIPX_CONFLICT_MERGE) {
        if(unlink(src_child)) {
          return rarx_fail(c, ZIPX_ERR_IO, dst_child,
                          "cannot drop staged file: %s", strerror(errno));
        }
        return 0;
      }
    }
    return rarx_fail(c, ZIPX_ERR_CONFLICT, dst_child, "target already exists");
  }
  return rarx_fail(c, ZIPX_ERR_CONFLICT, dst_child, "target already exists");
}

static int
publish_dir(const char *src, const char *dst, int depth, rarx_ctx_t *c) {
  DIR *dir = opendir(src);
  struct dirent *ent;
  char **names = NULL;
  size_t count = 0;
  size_t cap = 0;
  size_t i;
  int ret = 0;

  if(!dir) {
    return rarx_fail(c, ZIPX_ERR_IO, src, "cannot read staging: %s",
                     strerror(errno));
  }
  while((ent = readdir(dir))) {
    if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
      continue;
    }
    if(count == cap) {
      size_t next = cap ? cap * 2 : 64;
      char **grown_arr = realloc(names, next * sizeof(*grown_arr));
      if(!grown_arr) {
        ret = rarx_fail(c, ZIPX_ERR_INTERNAL, src, "out of memory");
        break;
      }
      names = grown_arr;
      cap = next;
    }
    if(!(names[count] = strdup(ent->d_name))) {
      ret = rarx_fail(c, ZIPX_ERR_INTERNAL, src, "out of memory");
      break;
    }
    count++;
  }
  closedir(dir);

  for(i = 0; i < count && !ret; i++) {
    report(c, ZIPX_PHASE_PUBLISH, names[i], 0);
    ret = publish_entry(src, dst, names[i], depth, c);
  }
  for(i = 0; i < count; i++) {
    free(names[i]);
  }
  free(names);
  return ret;
}

static int
publish_staging(rarx_ctx_t *c, const char *dst_dir, int dst_existed) {
  int ret;

  report(c, ZIPX_PHASE_PUBLISH, dst_dir, 1);
  if(!dst_existed) {
    if(rename(c->staging, dst_dir)) {
      return rarx_fail(c, ZIPX_ERR_IO, dst_dir, "cannot publish: %s",
                       strerror(errno));
    }
    c->staging_created = 0;
    return 0;
  }
  ret = publish_dir(c->staging, dst_dir, 0, c);
  if(ret) {
    rollback_published(c);
  }
  return ret;
}

/**************************************************************************
 * public entry point
 **************************************************************************/

zipx_status_t
rar_extract(const char *rar_path, const char *dst_dir,
            zipx_conflict_t conflict, const zipx_limits_t *limits,
            zipx_cancel_fn cancel, zipx_progress_fn progress,
            void *userdata, zipx_result_t *result) {
  rarx_ctx_t ctx;
  rarx_ctx_t *c = &ctx;
  char parent[ZIPX_PATH_MAX];
  char dst_copy[ZIPX_PATH_MAX];
  struct stat st;
  dmc_unrar_archive rar;
  dmc_unrar_return dr;
  int root_fd = -1;
  int dst_existed = 0;
  int status;

  if(!result || !rar_path || !dst_dir || !dst_dir[0]) {
    if(result) {
      memset(result, 0, sizeof(*result));
      result->status = ZIPX_ERR_INTERNAL;
      snprintf(result->message, sizeof(result->message), "invalid argument");
    }
    return ZIPX_ERR_INTERNAL;
  }

  memset(&ctx, 0, sizeof(ctx));
  memset(result, 0, sizeof(*result));
  c->result = result;
  c->conflict = conflict;
  c->limits = limits ? *limits : *zipx_default_limits();
  c->cancel = cancel;
  c->progress = progress;
  c->userdata = userdata;

  snprintf(dst_copy, sizeof(dst_copy), "%s", dst_dir);
  {
    size_t len = strlen(dst_copy);
    while(len > 1 && dst_copy[len - 1] == '/') {
      dst_copy[--len] = 0;
    }
  }
  if(path_parent(dst_copy, parent, sizeof(parent))) {
    status = rarx_fail(c, ZIPX_ERR_INTERNAL, dst_dir, "invalid destination");
    goto done;
  }
  if(stat(parent, &st) || !S_ISDIR(st.st_mode)) {
    status = rarx_fail(c, ZIPX_ERR_IO, parent, "destination parent is missing");
    goto done;
  }
  dst_existed = !lstat(dst_copy, &st);
  if(dst_existed && !S_ISDIR(st.st_mode)) {
    status = rarx_fail(c, ZIPX_ERR_CONFLICT, dst_copy,
                       "destination is not a directory");
    goto done;
  }

  dr = dmc_unrar_archive_init(&rar);
  if(dr != DMC_UNRAR_OK) {
    status = rarx_fail(c, ZIPX_ERR_INTERNAL, rar_path,
                       "cannot initialize RAR decoder");
    goto done;
  }
  dr = dmc_unrar_archive_open_path(&rar, rar_path);
  if(dr != DMC_UNRAR_OK) {
    status = rar_translate_error(dr, rar_path, c);
    goto done;
  }

  if(scan_archive(&rar, c)) {
    status = (int)c->result->status;
    goto done;
  }
  if(canceled(c)) {
    status = rarx_fail(c, ZIPX_ERR_CANCELED, NULL, NULL);
    goto done;
  }
  if(check_space(c, dst_existed ? dst_copy : parent)) {
    status = (int)c->result->status;
    goto done;
  }
  if(make_staging(c, parent)) {
    status = (int)c->result->status;
    goto done;
  }

  root_fd = open(c->staging, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if(root_fd < 0) {
    status = rarx_fail(c, ZIPX_ERR_IO, c->staging, "cannot open staging: %s",
                       strerror(errno));
    goto done;
  }

  if(extract_archive(&rar, c, root_fd)) {
    status = (int)c->result->status;
    goto done;
  }
  if(root_fd >= 0) {
    close(root_fd);
    root_fd = -1;
  }
  if(canceled(c)) {
    status = rarx_fail(c, ZIPX_ERR_CANCELED, NULL, NULL);
    goto done;
  }
  if(publish_staging(c, dst_copy, dst_existed)) {
    status = (int)c->result->status;
    goto done;
  }

  result->status = ZIPX_OK;
  status = ZIPX_OK;

done:
  if(root_fd >= 0) {
    close(root_fd);
  }
  cleanup_staging(c);
  dmc_unrar_archive_close(&rar);
  result->entries_total = c->entries_total;
  result->entries_done = c->entries_done;
  result->bytes_total = c->bytes_total;
  result->files_created = c->files_created;
  result->dirs_created = c->dirs_created;
  if(status != ZIPX_OK && !result->message[0]) {
    snprintf(result->message, sizeof(result->message), "%s",
             zipx_status_string(result->status));
  }
  if(status != ZIPX_OK) {
    result->status = (zipx_status_t)status;
  }
  report(c, ZIPX_PHASE_CLEANUP, NULL, 1);
  free_published(c);
  return result->status;
}