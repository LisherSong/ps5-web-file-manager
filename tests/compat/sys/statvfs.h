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
  char full[MAX_PATH];
  char root[8];

  /* Resolve to an absolute path first: the drive-letter extraction below
     only works for "X:\..." style paths, and callers may pass relative
     paths (e.g. the standalone bigfile_e2e driver). */
  if(!GetFullPathNameA(path, (DWORD)sizeof(full), full, NULL)) {
    return -1;
  }
  snprintf(root, sizeof(root), "%.3s", full);
  if(!GetDiskFreeSpaceExA(root, &free_bytes, &total, NULL)) {
    return -1;
  }
  memset(buf, 0, sizeof(*buf));
  /* `unsigned long` is 32-bit on Windows: store free space scaled by 4096
     so archives up to 16 TiB don't overflow (real 64-bit hosts are LP64
     and unaffected; PS5 SDK is LP64 too). */
  buf->f_bsize = 4096;
  buf->f_frsize = 4096;
  buf->f_bavail = free_bytes.QuadPart / 4096;
  return 0;
}

#endif
