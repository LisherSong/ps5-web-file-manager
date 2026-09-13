/* Host test shim: lets the POSIX extraction engine build and run on MinGW.
   Injected with gcc -include for the test build only; never compiled into the
   PS5 payload. It maps the *at() calls onto plain paths and fakes the few
   POSIX bits Windows lacks (symlinks and O_NOFOLLOW have no Windows
   equivalent, which is why the symlink tests are skipped there). */

#ifndef WFM_TEST_POSIX_COMPAT_H
#define WFM_TEST_POSIX_COMPAT_H

/* _wopendir / struct _wdirent require Vista+; pull the SDK level up before any
   system header touches the type definitions. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#if defined(__MINGW32__) || defined(_WIN32)

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utime.h>
#include <time.h>
#include <wchar.h>
#include <windows.h>
#include <dirent.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define O_NOFOLLOW 0
#define O_CLOEXEC 0
/* Windows cannot open a directory with _open(); a non-zero sentinel lets the
   shim detect directory opens and hand back a synthetic dirfd. */
#define O_DIRECTORY 0x10000
#define AT_SYMLINK_NOFOLLOW 0
#define AT_REMOVEDIR 0x0200
#ifndef S_IFLNK
#define S_IFLNK 0xA000
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#define WFM_FD_SLOTS 512

#define open(...) wfm_open(__VA_ARGS__)

static struct {
  int fd;
  char path[PATH_MAX];
} wfm_fd_slots[WFM_FD_SLOTS];

static void __attribute__((unused))
wfm_fd_set(int fd, const char *path) {
  int i;

  if(fd < 0) {
    return;
  }
  for(i = 0; i < WFM_FD_SLOTS; i++) {
    if(wfm_fd_slots[i].fd == fd || !wfm_fd_slots[i].path[0]) {
      wfm_fd_slots[i].fd = fd;
      snprintf(wfm_fd_slots[i].path, PATH_MAX, "%s", path);
      return;
    }
  }
}

static void __attribute__((unused))
wfm_fd_clear(int fd) {
  int i;

  for(i = 0; i < WFM_FD_SLOTS; i++) {
    if(wfm_fd_slots[i].fd == fd) {
      wfm_fd_slots[i].fd = -1;
      wfm_fd_slots[i].path[0] = 0;
      return;
    }
  }
}

static const char *
wfm_fd_path(int fd) {
  int i;

  for(i = 0; i < WFM_FD_SLOTS; i++) {
    if(wfm_fd_slots[i].fd == fd) {
      return wfm_fd_slots[i].path;
    }
  }
  return NULL;
}

static int
wfm_join(int dirfd, const char *rel, char *out, size_t out_size) {
  const char *base = wfm_fd_path(dirfd);

  if(!base) {
    errno = EBADF;
    return -1;
  }
  if(snprintf(out, out_size, "%s/%s", base, rel) >= (int)out_size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return 0;
}

/* UTF-8 to UTF-16, for the wide entry points below.  The engines speak UTF-8
   (that is what an archive stores), but MinGW's ANSI entry points decode their
   argument in the system code page: on a CP936 or CP1252 host a name such as
   "中文-テスト.txt" is either mangled or rejected outright with the
   unhelpful errno -1.  Going through the wide API keeps the on-disk name
   identical to the archive's. */
static void __attribute__((unused))
wfm_wide(const char *src, wchar_t *dst, size_t cap) {
  const unsigned char *p = (const unsigned char *)src;
  size_t out = 0;

  while(*p && out + 2 < cap) {
    unsigned long cp = *p++;

    if(cp >= 0x80) {
      unsigned extra = 0;
      unsigned i;

      if((cp & 0xE0) == 0xC0) {
        cp &= 0x1F;
        extra = 1;
      } else if((cp & 0xF0) == 0xE0) {
        cp &= 0x0F;
        extra = 2;
      } else if((cp & 0xF8) == 0xF0) {
        cp &= 0x07;
        extra = 3;
      } else {
        cp = '?';
        extra = 0;
      }
      for(i = 0; i < extra; i++) {
        if((*p & 0xC0) != 0x80) {
          cp = '?';
          break;
        }
        cp = (cp << 6) | (unsigned long)(*p++ & 0x3F);
      }
    }
    if(cp >= 0x10000) {
      cp -= 0x10000;
      dst[out++] = (wchar_t)(0xD800 | (cp >> 10));
      dst[out++] = (wchar_t)(0xDC00 | (cp & 0x3FF));
    } else {
      dst[out++] = (wchar_t)cp;
    }
  }
  dst[out] = 0;
}

/* Defined further down; the *at() shims above call them. */
static int wfm_mkdir1(const char *path);
static int wfm_unlink(const char *path);
static int wfm_rmdir(const char *path);
static int wfm_stat(const char *path, struct stat *st);
static int wfm_rename(const char *from, const char *to);

static int __attribute__((unused))
wfm_open(const char *path, int flags, ...) {
  int mode = 0;
  int fd;

  if(flags & O_CREAT) {
    va_list ap;

    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }
  /* Directory opens become synthetic fds so openat/mkdirat can resolve them
     to paths; _open() returns EACCES for directories on Windows. */
  if(flags & O_DIRECTORY) {
    static int next_dirfd = 0x10000;

    fd = next_dirfd++;
    wfm_fd_set(fd, path);
    return fd;
  }
  /* Force O_BINARY: MinGW's _open defaults to text mode, which would
     translate LF -> CRLF on write and corrupt binary payloads.  The wide
     call keeps a non-ASCII name intact (see wfm_wide). */
  {
    wchar_t wide[PATH_MAX];

    wfm_wide(path, wide, PATH_MAX);
    fd = _wopen(wide, (flags & ~(O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC)) |
                          O_BINARY,
                mode);
  }
  if(fd >= 0) {
    wfm_fd_set(fd, path);
  }
  return fd;
}

static int __attribute__((unused))
wfm_openat(int dirfd, const char *path, int flags, ...) {
  char full[PATH_MAX];
  int mode = 0;

  if(wfm_join(dirfd, path, full, sizeof(full))) {
    return -1;
  }
  if(flags & O_CREAT) {
    va_list ap;

    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }
  return wfm_open(full, flags, mode);
}

static int __attribute__((unused))
wfm_mkdirat(int dirfd, const char *path, mode_t mode) {
  char full[PATH_MAX];

  (void)mode;
  if(wfm_join(dirfd, path, full, sizeof(full))) {
    return -1;
  }
  return wfm_mkdir1(full);
}

static int __attribute__((unused))
wfm_renameat(int from_fd, const char *from, int to_fd, const char *to) {
  char src[PATH_MAX];
  char dst[PATH_MAX];

  if(wfm_join(from_fd, from, src, sizeof(src)) ||
     wfm_join(to_fd, to, dst, sizeof(dst))) {
    return -1;
  }
  return wfm_rename(src, dst);
}

static int __attribute__((unused))
wfm_rename(const char *from, const char *to) {
  wchar_t wfrom[PATH_MAX];
  wchar_t wto[PATH_MAX];

  wfm_wide(from, wfrom, PATH_MAX);
  wfm_wide(to, wto, PATH_MAX);
  /* Windows rename() refuses to replace an existing file, unlike POSIX. */
  if(_waccess(wto, 0) == 0) {
    if(_wremove(wto)) {
      return -1;
    }
  }
  return _wrename(wfrom, wto);
}

static int __attribute__((unused))
wfm_unlinkat(int dirfd, const char *path, int flags) {
  char full[PATH_MAX];

  if(wfm_join(dirfd, path, full, sizeof(full))) {
    return -1;
  }
  return (flags & AT_REMOVEDIR) ? wfm_rmdir(full) : wfm_unlink(full);
}

static int __attribute__((unused))
wfm_mkdir1(const char *path) {
  wchar_t wide[PATH_MAX];

  wfm_wide(path, wide, PATH_MAX);
  return _wmkdir(wide); /* MinGW's mkdir() takes a single argument. */
}

static int __attribute__((unused))
wfm_unlink(const char *path) {
  wchar_t wide[PATH_MAX];

  wfm_wide(path, wide, PATH_MAX);
  return _wunlink(wide);
}

static int __attribute__((unused))
wfm_rmdir(const char *path) {
  wchar_t wide[PATH_MAX];

  wfm_wide(path, wide, PATH_MAX);
  return _wrmdir(wide);
}

/* _wstati64 fills its own struct; the engines only ever read st_mode, st_size
   and st_mtime, so copying those across is safe and avoids depending on how
   this toolchain happens to alias `struct stat`. */
static int __attribute__((unused))
wfm_stat(const char *path, struct stat *st) {
  wchar_t wide[PATH_MAX];
  struct _stati64 wst;

  wfm_wide(path, wide, PATH_MAX);
  if(_wstati64(wide, &wst)) {
    return -1;
  }
  memset(st, 0, sizeof(*st));
  st->st_mode = (mode_t)wst.st_mode;
  st->st_size = (off_t)wst.st_size;
  st->st_mtime = (time_t)wst.st_mtime;
  return 0;
}

static int __attribute__((unused))
wfm_fstatat(int dirfd, const char *path, struct stat *st, int flags) {
  char full[PATH_MAX];

  (void)flags;
  if(wfm_join(dirfd, path, full, sizeof(full))) {
    return -1;
  }
  return wfm_stat(full, st);
}

static int __attribute__((unused))
wfm_fsync(int fd) {
  return _commit(fd);
}

static int __attribute__((unused))
wfm_fchmod(int fd, mode_t mode) {
  (void)fd;
  (void)mode;
  return 0; /* Windows has no Unix modes; the engine ignores this failure. */
}

/* MinGW has no utimes(); _utime() is the same thing with second precision,
   which is all the 7z engine asks for (it feeds the extractor both fields). */
static int __attribute__((unused))
wfm_utimes(const char *path, const struct timeval tv[2]) {
  struct _utimbuf ut;

  ut.actime = tv[0].tv_sec;
  ut.modtime = tv[1].tv_sec;
  return _utime(path, &ut);
}

static int __attribute__((unused))
wfm_close(int fd) {
  wfm_fd_clear(fd);
  if(fd >= 0x10000) {
    return 0; /* synthetic dirfd, nothing to close */
  }
  return _close(fd);
}

/* MinGW's fopen() decodes the path in the system code page, the same way
   stat() does.  Redirect to _wfopen so a UTF-8 path goes through the wide
   API and round-trips back to the on-disk name regardless of the host's
   ACP.  The translation units that include this shim may not call fopen()
   themselves, so mark the wrapper as unused to keep -Werror quiet. */
__attribute__((unused))
static FILE *wfm_fopen(const char *path, const char *mode) {
  wchar_t wide_path[PATH_MAX];
  wchar_t wide_mode[16];
  size_t i;

  wfm_wide(path, wide_path, PATH_MAX);
  for(i = 0; i + 1 < sizeof(wide_mode) && mode[i]; i++) {
    wide_mode[i] = (wchar_t)(unsigned char)mode[i];
  }
  wide_mode[i] = 0;
  return _wfopen(wide_path, wide_mode);
}

#define fopen(p, m) wfm_fopen(p, m)

#define mkdir(p, ...) wfm_mkdir1(p)
#define rmdir(p) wfm_rmdir(p)
#define unlink(p) wfm_unlink(p)
#define open(...) wfm_open(__VA_ARGS__)
#define openat(...) wfm_openat(__VA_ARGS__)
#define mkdirat(d, p, m) wfm_mkdirat(d, p, m)
#define rename(a, b) wfm_rename(a, b)
#define renameat(sd, sp, dd, dp) wfm_renameat(sd, sp, dd, dp)
#define unlinkat(d, p, f) wfm_unlinkat(d, p, f)
#define fstatat(d, p, s, f) wfm_fstatat(d, p, s, f)
#define fsync(fd) wfm_fsync(fd)
#define fchmod(fd, mode) wfm_fchmod(fd, mode)
#define close(fd) wfm_close(fd)
/* Direct lstat/stat to the wide-path shim so non-ASCII archive entries survive
   a CP936 or CP1252 host.  MinGW's stat() defaults to the ANSI entry point
   and silently truncates names it cannot represent. */
#define lstat(p, s) wfm_stat(p, s)
#define stat(p, s) wfm_stat(p, s)
#define utimes(p, tv) wfm_utimes(p, tv)
/* opendir/readdir/closedir go through the wide variants so the names we get
   back are real UTF-8; otherwise MinGW hands us whatever the system code page
   made of the filename, which round-trips through a non-ASCII UTF-8 entry as
   a garbage string that no later wfm_stat() call can resolve. */
typedef struct {
  _WDIR *wd;
  struct dirent de;
} WFM_DIR;

static DIR * __attribute__((unused))
wfm_opendir(const char *path) {
  wchar_t wide[PATH_MAX];
  WFM_DIR *wfm;

  wfm_wide(path, wide, PATH_MAX);
  wfm = (WFM_DIR *)malloc(sizeof(*wfm));
  if(!wfm) {
    return NULL;
  }
  wfm->wd = _wopendir(wide);
  if(!wfm->wd) {
    free(wfm);
    return NULL;
  }
  return (DIR *)wfm;
}

static struct dirent * __attribute__((unused))
wfm_readdir(DIR *d) {
  WFM_DIR *wfm = (WFM_DIR *)d;
  struct _wdirent *we;

  if(!wfm) {
    return NULL;
  }
  we = _wreaddir(wfm->wd);
  if(!we) {
    return NULL;
  }
  WideCharToMultiByte(CP_UTF8, 0, we->d_name, -1, wfm->de.d_name,
                      sizeof(wfm->de.d_name), NULL, NULL);
  wfm->de.d_ino = we->d_ino;
  wfm->de.d_reclen = (unsigned short)strlen(wfm->de.d_name);
  return &wfm->de;
}

static int __attribute__((unused))
wfm_closedir(DIR *d) {
  WFM_DIR *wfm = (WFM_DIR *)d;

  if(!wfm) {
    return -1;
  }
  _wclosedir(wfm->wd);
  free(wfm);
  return 0;
}

#define opendir(p) wfm_opendir(p)
#define readdir(d) wfm_readdir(d)
#define closedir(d) wfm_closedir(d)

#endif /* _WIN32 */

#endif /* WFM_TEST_POSIX_COMPAT_H */
