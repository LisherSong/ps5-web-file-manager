/* Host test shim: lets the POSIX extraction engine build and run on MinGW.
   Injected with gcc -include for the test build only; never compiled into the
   PS5 payload. It maps the *at() calls onto plain paths and fakes the few
   POSIX bits Windows lacks (symlinks and O_NOFOLLOW have no Windows
   equivalent, which is why the symlink tests are skipped there). */

#ifndef WFM_TEST_POSIX_COMPAT_H
#define WFM_TEST_POSIX_COMPAT_H

#if defined(__MINGW32__) || defined(_WIN32)

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <windows.h>

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
     translate LF -> CRLF on write and corrupt binary payloads. */
  fd = _open(path, (flags & ~(O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC)) | O_BINARY,
             mode);
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
  return mkdir(full);
}

static int __attribute__((unused))
wfm_renameat(int from_fd, const char *from, int to_fd, const char *to) {
  char src[PATH_MAX];
  char dst[PATH_MAX];

  if(wfm_join(from_fd, from, src, sizeof(src)) ||
     wfm_join(to_fd, to, dst, sizeof(dst))) {
    return -1;
  }
  /* Windows rename() refuses to replace an existing file. */
  if(_access(dst, 0) == 0) {
    if(remove(dst)) {
      return -1;
    }
  }
  return rename(src, dst);
}

static int __attribute__((unused))
wfm_rename(const char *from, const char *to) {
  /* Windows rename() refuses to replace an existing file, unlike POSIX. */
  if(_access(to, 0) == 0) {
    if(remove(to)) {
      return -1;
    }
  }
  return rename(from, to);
}

static int __attribute__((unused))
wfm_unlinkat(int dirfd, const char *path, int flags) {
  char full[PATH_MAX];

  if(wfm_join(dirfd, path, full, sizeof(full))) {
    return -1;
  }
  return (flags & AT_REMOVEDIR) ? rmdir(full) : unlink(full);
}

static int __attribute__((unused))
wfm_mkdir1(const char *path) {
  return mkdir(path); /* MinGW's mkdir() takes a single argument. */
}

static int __attribute__((unused))
wfm_fstatat(int dirfd, const char *path, struct stat *st, int flags) {
  char full[PATH_MAX];

  (void)flags;
  if(wfm_join(dirfd, path, full, sizeof(full))) {
    return -1;
  }
  return stat(full, st);
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

static int __attribute__((unused))
wfm_close(int fd) {
  wfm_fd_clear(fd);
  if(fd >= 0x10000) {
    return 0; /* synthetic dirfd, nothing to close */
  }
  return _close(fd);
}

#define mkdir(p, ...) wfm_mkdir1(p)
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
#define lstat(p, s) stat(p, s)

#endif /* _WIN32 */

#endif /* WFM_TEST_POSIX_COMPAT_H */
