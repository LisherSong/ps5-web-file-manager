/* Host test shim: provides <sys/statvfs.h> for MinGW hosts.
   Only used when building the test suite (see run-tests.sh). */

#ifndef WFM_TEST_SYS_STATVFS_H
#define WFM_TEST_SYS_STATVFS_H

#include <windows.h>

struct statvfs {
  unsigned long f_bsize;
  unsigned long f_frsize;
  unsigned long f_blocks;
  unsigned long f_bfree;
  unsigned long f_bavail;
};

static int
statvfs(const char *path, struct statvfs *buf) {
  ULARGE_INTEGER total;
  ULARGE_INTEGER free_bytes;
  char root[8];

  snprintf(root, sizeof(root), "%.3s", path);
  if(!GetDiskFreeSpaceExA(root, &free_bytes, &total, NULL)) {
    return -1;
  }
  memset(buf, 0, sizeof(*buf));
  buf->f_bsize = 1;
  buf->f_frsize = 1;
  buf->f_bavail = free_bytes.QuadPart;
  return 0;
}

#endif
