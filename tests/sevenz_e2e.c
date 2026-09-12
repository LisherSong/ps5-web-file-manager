/*
 * Standalone 7z driver for host end-to-end checks.
 *
 *   sevenz_e2e <archive.7z> <out-dir>
 *
 * Lists every entry, extracts the archive into <out-dir> and prints the
 * per-entry result.  Exit status is non-zero if any entry fails.
 *
 * This is a *diagnostic* tool: it exercises the same container layer as the
 * real extraction path, so it is the quickest way to tell whether a failure
 * comes from the container/codec layer or from the web server around it.
 *
 * Entry names are UTF-16 inside a 7z archive; on a Windows host the file APIs
 * must therefore be called with the wide-character variants or non-ASCII names
 * silently fail to be created.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <wchar.h>
#else
#include <unistd.h>
#endif

#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include "7zFile.h"

#define INPUT_BUF_SIZE (1u << 18)
#define MAX_PATH_LEN 4096

static ISzAlloc g_alloc = { SzAlloc, SzFree };
static ISzAlloc g_temp = { SzAlloc, SzFree };

static int g_failures = 0;

/* ---------------------------------------------------------------- paths */

#if defined(_WIN32)

/* MinGW's wchar_t is 16 bit, matching UTF-16. */
static void utf8_to_utf16(const char *src, wchar_t *dst, size_t cap)
{
  size_t out = 0;

  while (*src && out + 2 < cap) {
    unsigned char c = (unsigned char)*src++;
    UInt32 cp;

    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0 && (src[0] & 0xC0) == 0x80) {
      cp = ((UInt32)(c & 0x1F) << 6) | (UInt32)(*src++ & 0x3F);
    } else if ((c & 0xF0) == 0xE0 && (src[0] & 0xC0) == 0x80 && (src[1] & 0xC0) == 0x80) {
      cp = ((UInt32)(c & 0x0F) << 12) | ((UInt32)(src[0] & 0x3F) << 6) | (UInt32)(src[1] & 0x3F);
      src += 2;
    } else if ((c & 0xF8) == 0xF0 && (src[0] & 0xC0) == 0x80 && (src[1] & 0xC0) == 0x80 && (src[2] & 0xC0) == 0x80) {
      cp = ((UInt32)(c & 0x07) << 18) | ((UInt32)(src[0] & 0x3F) << 12) |
           ((UInt32)(src[1] & 0x3F) << 6) | (UInt32)(src[2] & 0x3F);
      src += 3;
    } else {
      cp = '?';
    }

    if (cp >= 0x10000) {
      cp -= 0x10000;
      dst[out++] = (wchar_t)(0xD800 | (cp >> 10));
      dst[out++] = (wchar_t)(0xDC00 | (cp & 0x3FF));
    } else {
      dst[out++] = (wchar_t)cp;
    }
  }
  dst[out] = 0;
}

static void path_mkdir(const char *path)
{
  wchar_t wide[MAX_PATH_LEN];
  utf8_to_utf16(path, wide, MAX_PATH_LEN);
  _wmkdir(wide);
}

static FILE *path_fopen_write(const char *path)
{
  wchar_t wide[MAX_PATH_LEN];
  utf8_to_utf16(path, wide, MAX_PATH_LEN);
  return _wfopen(wide, L"wb");
}

#else /* POSIX: byte paths are UTF-8 already */

static void path_mkdir(const char *path) { mkdir(path, 0755); }
static FILE *path_fopen_write(const char *path) { return fopen(path, "wb"); }

#endif

static void make_dirs(const char *path)
{
  char tmp[MAX_PATH_LEN];
  size_t i, n = strlen(path);

  if (n + 1 > sizeof(tmp))
    return;
  memcpy(tmp, path, n + 1);

  for (i = 1; i < n; i++) {
    if (tmp[i] == '/' || tmp[i] == '\\') {
      char c = tmp[i];
      tmp[i] = 0;
      path_mkdir(tmp);
      tmp[i] = c;
    }
  }
  /* The final component counts too: callers hand us either a directory entry
     or the parent directory of a file. */
  path_mkdir(tmp);
}

static int write_file(const char *path, const Byte *data, size_t size)
{
  FILE *fh = path_fopen_write(path);
  if (!fh)
    return -1;
  if (size && fwrite(data, 1, size, fh) != size) {
    fclose(fh);
    return -1;
  }
  return fclose(fh) == 0 ? 0 : -1;
}

/* UTF-16 (LE, as stored by the 7z name table) to UTF-8. */
static void utf16_to_utf8(const UInt16 *src, char *dst, size_t dst_size)
{
  size_t out = 0;

  while (*src) {
    UInt32 c = *src++;

    if (c >= 0xD800 && c <= 0xDBFF && *src >= 0xDC00 && *src <= 0xDFFF)
      c = 0x10000 + ((c - 0xD800) << 10) + (*src++ - 0xDC00);

    if (out + 5 >= dst_size)
      break;

    if (c < 0x80) {
      dst[out++] = (char)c;
    } else if (c < 0x800) {
      dst[out++] = (char)(0xC0 | (c >> 6));
      dst[out++] = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      dst[out++] = (char)(0xE0 | (c >> 12));
      dst[out++] = (char)(0x80 | ((c >> 6) & 0x3F));
      dst[out++] = (char)(0x80 | (c & 0x3F));
    } else {
      dst[out++] = (char)(0xF0 | (c >> 18));
      dst[out++] = (char)(0x80 | ((c >> 12) & 0x3F));
      dst[out++] = (char)(0x80 | ((c >> 6) & 0x3F));
      dst[out++] = (char)(0x80 | (c & 0x3F));
    }
  }
  dst[out] = 0;
}

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv)
{
  CFileInStream archive_stream;
  CLookToRead2 look_stream;
  CSzArEx db;
  SRes res;
  UInt16 *name16 = NULL;
  size_t name16_cap = 0;
  UInt32 i;
  UInt32 block_index = 0xFFFFFFFF;
  Byte *out_buffer = NULL;
  size_t out_buffer_size = 0;
  const char *out_dir;

  if (argc < 3) {
    fprintf(stderr, "usage: %s <archive.7z> <out-dir>\n", argv[0]);
    return 2;
  }
  out_dir = argv[2];

  if (InFile_Open(&archive_stream.file, argv[1]) != 0) {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }
  FileInStream_CreateVTable(&archive_stream);
  archive_stream.wres = 0;

  LookToRead2_CreateVTable(&look_stream, 0);
  look_stream.buf = (Byte *)ISzAlloc_Alloc(&g_alloc, INPUT_BUF_SIZE);
  if (!look_stream.buf) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  look_stream.bufSize = INPUT_BUF_SIZE;
  look_stream.realStream = &archive_stream.vt;
  LookToRead2_INIT(&look_stream)

  CrcGenerateTable();
  SzArEx_Init(&db);

  res = SzArEx_Open(&db, &look_stream.vt, &g_alloc, &g_temp);
  if (res != SZ_OK) {
    fprintf(stderr, "SzArEx_Open failed: res=%d\n", (int)res);
    if (res == SZ_ERROR_UNSUPPORTED)
      fprintf(stderr, "  (unsupported coder - encrypted header or exotic method)\n");
    return 1;
  }

  printf("entries: %u\n", (unsigned)db.NumFiles);

  for (i = 0; i < db.NumFiles; i++) {
    const int is_dir = SzArEx_IsDir(&db, i);
    const size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL);
    char rel[MAX_PATH_LEN];
    char full[MAX_PATH_LEN];
    size_t offset = 0;
    size_t processed = 0;

    if (len + 1 > name16_cap) {
      UInt16 *grown = (UInt16 *)realloc(name16, (len + 1) * sizeof(UInt16));
      if (!grown) {
        fprintf(stderr, "out of memory\n");
        return 1;
      }
      name16 = grown;
      name16_cap = len + 1;
    }
    SzArEx_GetFileNameUtf16(&db, i, name16);
    utf16_to_utf8(name16, rel, sizeof(rel));

    if (snprintf(full, sizeof(full), "%s/%s", out_dir, rel) >= (int)sizeof(full)) {
      printf("  SKIP (path too long) %s\n", rel);
      g_failures++;
      continue;
    }

    if (is_dir) {
      make_dirs(full);
      printf("  dir  %s\n", rel);
      continue;
    }

    res = SzArEx_Extract(&db, &look_stream.vt, i, &block_index, &out_buffer,
                         &out_buffer_size, &offset, &processed, &g_alloc, &g_temp);
    if (res != SZ_OK) {
      printf("  FAIL %s (res=%d)\n", rel, (int)res);
      g_failures++;
      /* SzArEx_Extract leaves the block cache primed with a half-decoded
         buffer when the folder decode fails; drop it so the next entry
         reports its own error instead of a bogus CRC mismatch. */
      block_index = 0xFFFFFFFF;
      continue;
    }

    {
      char *slash = strrchr(full, '/');
      if (slash) {
        *slash = 0;
        make_dirs(full);
        *slash = '/';
      }
    }
    if (write_file(full, out_buffer + offset, processed) != 0) {
      printf("  FAIL %s (write error)\n", rel);
      g_failures++;
      continue;
    }

    {
      UInt32 crc = 0;
      if (SzBitWithVals_Check(&db.CRCs, i)) {
        crc = CrcCalc(out_buffer + offset, processed);
        if (crc != db.CRCs.Vals[i]) {
          printf("  FAIL %s (crc mismatch)\n", rel);
          g_failures++;
          continue;
        }
      }
    }
    printf("  ok   %s (%lu bytes)\n", rel, (unsigned long)processed);
  }

  ISzAlloc_Free(&g_alloc, out_buffer);
  ISzAlloc_Free(&g_alloc, look_stream.buf);
  SzArEx_Free(&db, &g_alloc);
  File_Close(&archive_stream.file);
  free(name16);

  printf("%s: %d failure(s)\n", argv[1], g_failures);
  return g_failures ? 1 : 0;
}
