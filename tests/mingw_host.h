/* Host-only shim used to build minizip-ng's POSIX backend on MinGW.
   mz_os_posix.c needs utime(), lstat(), symlink() and readlink(); Windows has
   none of them. The stubs are only compiled into the host test binary. */

#ifndef WFM_TEST_MINGW_HOST_H
#define WFM_TEST_MINGW_HOST_H

#include "posix_compat.h"

#if defined(__MINGW32__) || defined(_WIN32)

#include <sys/utime.h>

static int __attribute__((unused))
symlink(const char *target, const char *linkpath) {
  (void)target;
  (void)linkpath;
  errno = ENOSYS;
  return -1;
}

static ssize_t __attribute__((unused))
readlink(const char *path, char *buf, size_t size) {
  (void)path;
  (void)buf;
  (void)size;
  errno = ENOSYS;
  return -1;
}

#endif

#endif
