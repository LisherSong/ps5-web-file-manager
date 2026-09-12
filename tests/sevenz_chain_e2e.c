/*
 * End-to-end driver for the project's own 7z folder decoder (src/sevenz_chain.c).
 *
 *   sevenz_chain_e2e <archive.7z> <out-dir>
 *
 * Unlike tests/sevenz_e2e.c -- which drives the LZMA SDK's own CSzFolder based
 * path and therefore cannot handle BCJ2 or anything else the SDK caps at four
 * coders -- this one decodes *every* folder through src/sevenz_chain.c.
 *
 * It also mirrors the shape of the real extraction path: each folder is
 * decoded once, streamed to a sink, and the sink splits the byte stream across
 * the entries that live in that folder (that is what makes an archive solid).
 * Per-entry and per-folder CRCs are verified as the bytes go past.
 *
 * Exit status is non-zero if any folder or entry failed.
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

#include "sevenz_chain.h"

#define INPUT_BUF_SIZE (1u << 18)
#define MAX_PATH_LEN 4096

static ISzAlloc g_alloc = { SzAlloc, SzFree };
static ISzAlloc g_temp = { SzAlloc, SzFree };

static int g_failures = 0;

/* ---------------------------------------------------------------- paths */

#if defined(_WIN32)

static void utf8_to_utf16(const char *src, wchar_t *dst, size_t cap) {
  size_t out = 0;

  while(*src && out + 2 < cap) {
    unsigned char c = (unsigned char)*src++;
    UInt32 cp;

    if(c < 0x80) {
      cp = c;
    } else if((c & 0xE0) == 0xC0 && (src[0] & 0xC0) == 0x80) {
      cp = ((UInt32)(c & 0x1F) << 6) | (UInt32)(*src++ & 0x3F);
    } else if((c & 0xF0) == 0xE0 && (src[0] & 0xC0) == 0x80 &&
              (src[1] & 0xC0) == 0x80) {
      cp = ((UInt32)(c & 0x0F) << 12) | ((UInt32)(src[0] & 0x3F) << 6) |
           (UInt32)(src[1] & 0x3F);
      src += 2;
    } else if((c & 0xF8) == 0xF0 && (src[0] & 0xC0) == 0x80 &&
              (src[1] & 0xC0) == 0x80 && (src[2] & 0xC0) == 0x80) {
      cp = ((UInt32)(c & 0x07) << 18) | ((UInt32)(src[0] & 0x3F) << 12) |
           ((UInt32)(src[1] & 0x3F) << 6) | (UInt32)(src[2] & 0x3F);
      src += 3;
    } else {
      cp = '?';
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

static void path_mkdir(const char *path) {
  wchar_t wide[MAX_PATH_LEN];
  utf8_to_utf16(path, wide, MAX_PATH_LEN);
  _wmkdir(wide);
}

static FILE *path_fopen_write(const char *path) {
  wchar_t wide[MAX_PATH_LEN];
  utf8_to_utf16(path, wide, MAX_PATH_LEN);
  return _wfopen(wide, L"wb");
}

#else

static void path_mkdir(const char *path) { mkdir(path, 0755); }
static FILE *path_fopen_write(const char *path) { return fopen(path, "wb"); }

#endif

static void make_dirs(const char *path) {
  char tmp[MAX_PATH_LEN];
  size_t i, n = strlen(path);

  if(n + 1 > sizeof(tmp)) return;
  memcpy(tmp, path, n + 1);

  for(i = 1; i < n; i++) {
    if(tmp[i] == '/' || tmp[i] == '\\') {
      char c = tmp[i];
      tmp[i] = 0;
      path_mkdir(tmp);
      tmp[i] = c;
    }
  }
  path_mkdir(tmp);
}

/* UTF-16 (LE, as stored by the 7z name table) to UTF-8. */
static void utf16_to_utf8(const UInt16 *src, char *dst, size_t dst_size) {
  size_t out = 0;

  while(*src) {
    UInt32 c = *src++;

    if(c >= 0xD800 && c <= 0xDBFF && *src >= 0xDC00 && *src <= 0xDFFF)
      c = 0x10000 + ((c - 0xD800) << 10) + (*src++ - 0xDC00);
    if(out + 5 >= dst_size) break;

    if(c < 0x80) {
      dst[out++] = (char)c;
    } else if(c < 0x800) {
      dst[out++] = (char)(0xC0 | (c >> 6));
      dst[out++] = (char)(0x80 | (c & 0x3F));
    } else if(c < 0x10000) {
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

/* --------------------------------------------------------------- reader */

typedef struct {
  CSzFile *file;
  UInt64 base; /* db.dataPos: packed offsets are relative to it */
} reader_ctx;

static int reader_at(void *ctx, uint64_t offset, void *dst, size_t size) {
  reader_ctx *r = (reader_ctx *)ctx;
  UInt64 pos = r->base + offset;
  size_t done = 0;

  if(File_Seek(r->file, (Int64 *)&pos, SZ_SEEK_SET) != 0) return -1;
  while(done < size) {
    size_t want = size - done;
    if(File_Read(r->file, (Byte *)dst + done, &want) != 0) return -1;
    if(want == 0) return -1;
    done += want;
  }
  return 0;
}

/* ------------------------------------------------------ folder -> sink */

typedef struct {
  UInt32 file_index;
  uint64_t size;
} plan_entry;

typedef struct {
  const CSzArEx *db;
  char out_dir[MAX_PATH_LEN];
  char name[MAX_PATH_LEN];

  plan_entry *plan;
  size_t plan_len;
  size_t plan_pos;
  uint64_t written;
  FILE *fh;

  uint32_t entry_crc;
  uint64_t bytes_total;
  uint64_t bytes_ok;
  int failures;
} sink_ctx;

static void plan_free(sink_ctx *s) {
  free(s->plan);
  s->plan = NULL;
  s->plan_len = s->plan_pos = 0;
}

/* Builds the ordered list of non-empty entries living in folder `folder`. */
static int plan_build(sink_ctx *s, UInt32 folder, sz_chain_err_t *cerr) {
  const CSzArEx *db = s->db;
  UInt32 first = db->FolderToFile[folder];
  UInt32 last = db->FolderToFile[(size_t)folder + 1];
  UInt32 i;

  plan_free(s);
  /* A folder that failed mid-entry leaves `written` pointing into an entry it
     never finished; carrying that into the next folder makes every later entry
     look "already part written" and produces a cascade of bogus failures. */
  s->written = 0;
  if(last <= first) return 0;
  s->plan = (plan_entry *)malloc(sizeof(plan_entry) * (size_t)(last - first));
  if(!s->plan) return -1;

  for(i = first; i < last; i++) {
    UInt64 size = db->UnpackPositions[(size_t)i + 1] - db->UnpackPositions[i];
    if(db->FileToFolder[i] != folder) continue;
    if(size == 0) continue;
    s->plan[s->plan_len].file_index = i;
    s->plan[s->plan_len].size = (uint64_t)size;
    s->plan_len++;
  }
  return 0;
}

static void sink_close_entry(sink_ctx *s) {
  if(s->fh) {
    fclose(s->fh);
    s->fh = NULL;
  }
}

static int sink_open_entry(sink_ctx *s, UInt32 file_index) {
  const CSzArEx *db = s->db;
  UInt16 *name16 = NULL;
  size_t len = SzArEx_GetFileNameUtf16(db, file_index, NULL);
  char rel[MAX_PATH_LEN];
  char full[MAX_PATH_LEN];
  char *slash;

  name16 = (UInt16 *)malloc((len + 1) * sizeof(UInt16));
  if(!name16) return -1;
  SzArEx_GetFileNameUtf16(db, file_index, name16);
  utf16_to_utf8(name16, rel, sizeof(rel));
  free(name16);

  if(snprintf(full, sizeof(full), "%s/%s", s->out_dir, rel) >=
     (int)sizeof(full)) {
    printf("  FAIL (path too long) %s\n", rel);
    return -1;
  }
  slash = strrchr(full, '/');
  if(slash) {
    *slash = 0;
    make_dirs(full);
    *slash = '/';
  }
  s->fh = path_fopen_write(full);
  if(!s->fh) {
    printf("  FAIL (cannot create) %s\n", rel);
    return -1;
  }
  s->entry_crc = CRC_INIT_VAL;
  return 0;
}

static int sink_write(void *ctx, const void *data, size_t size) {
  sink_ctx *s = (sink_ctx *)ctx;
  const Byte *p = (const Byte *)data;

  while(size > 0) {
    plan_entry *e;
    uint64_t remain;
    size_t take;
    char rel[MAX_PATH_LEN];

    if(s->plan_pos >= s->plan_len) {
      printf("  FAIL folder produced %llu bytes more than its entries hold\n",
             (unsigned long long)size);
      s->failures++;
      return -1;
    }
    e = &s->plan[s->plan_pos];
    remain = e->size - s->written;
    take = (size_t)((uint64_t)size < remain ? (uint64_t)size : remain);

    if(!s->fh) {
      if(sink_open_entry(s, e->file_index) != 0) {
        s->failures++;
        return -1;
      }
    }
    if(take && fwrite(p, 1, take, s->fh) != take) {
      printf("  FAIL (write error)\n");
      s->failures++;
      return -1;
    }
    s->entry_crc = CrcUpdate(s->entry_crc, p, take);
    s->written += take;
    s->bytes_total += take;
    p += take;
    size -= take;

    if(s->written == e->size) {
      UInt16 *name16;
      size_t len;
      sink_close_entry(s);
      len = SzArEx_GetFileNameUtf16(s->db, e->file_index, NULL);
      name16 = (UInt16 *)malloc((len + 1) * sizeof(UInt16));
      if(name16) {
        SzArEx_GetFileNameUtf16(s->db, e->file_index, name16);
        utf16_to_utf8(name16, rel, sizeof(rel));
        free(name16);
        if(SzBitWithVals_Check(&s->db->CRCs, e->file_index) &&
           CRC_GET_DIGEST(s->entry_crc) != s->db->CRCs.Vals[e->file_index]) {
          printf("  FAIL %s (crc mismatch)\n", rel);
          s->failures++;
          return -1;
        }
      }
      s->bytes_ok += e->size;
      s->plan_pos++;
      s->written = 0;
    }
  }
  return 0;
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv) {
  CFileInStream archive_stream;
  CLookToRead2 look_stream;
  CSzArEx db;
  SRes res;
  UInt32 folder;
  reader_ctx reader;
  sink_ctx sink;
  int rc = 0;

  if(argc < 3) {
    fprintf(stderr, "usage: %s <archive.7z> <out-dir>\n", argv[0]);
    return 2;
  }

  if(InFile_Open(&archive_stream.file, argv[1]) != 0) {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }
  FileInStream_CreateVTable(&archive_stream);
  archive_stream.wres = 0;

  LookToRead2_CreateVTable(&look_stream, 0);
  look_stream.buf = (Byte *)ISzAlloc_Alloc(&g_alloc, INPUT_BUF_SIZE);
  if(!look_stream.buf) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  look_stream.bufSize = INPUT_BUF_SIZE;
  look_stream.realStream = &archive_stream.vt;
  LookToRead2_INIT(&look_stream)

  CrcGenerateTable();
  SzArEx_Init(&db);

  res = SzArEx_Open(&db, &look_stream.vt, &g_alloc, &g_temp);
  if(res != SZ_OK) {
    fprintf(stderr, "SzArEx_Open failed: res=%d\n", (int)res);
    return 1;
  }

  printf("entries: %u, folders: %u, packed streams: %u\n",
         (unsigned)db.NumFiles, (unsigned)db.db.NumFolders,
         (unsigned)db.db.NumPackStreams);

  memset(&sink, 0, sizeof(sink));
  sink.db = &db;
  snprintf(sink.out_dir, sizeof(sink.out_dir), "%s", argv[2]);

  reader.file = &archive_stream.file;
  reader.base = db.dataPos;

  for(folder = 0; folder < db.db.NumFolders; folder++) {
    const UInt32 pack_first = db.db.FoStartPackStreamIndex[folder];
    const UInt32 pack_count = db.db.FoStartPackStreamIndex[(size_t)folder + 1] -
                              pack_first;
    uint64_t pack_positions[SZ_CHAIN_MAX_STREAMS + 1];
    sz_chain *chain = NULL;
    sz_chain_err_t cerr;
    char desc[256];
    uint32_t folder_crc = 0;
    UInt32 k;
    uint64_t unpack_size = SzAr_GetFolderUnpackSize(&db.db, folder);
    const uint8_t *blob = db.db.CodersData + db.db.FoCodersOffsets[folder];
    size_t blob_size = db.db.FoCodersOffsets[(size_t)folder + 1] -
                       db.db.FoCodersOffsets[folder];
    const uint64_t *cu =
        &db.db.CoderUnpackSizes[db.db.FoToCoderUnpackSizes[folder]];

    for(k = 0; k <= pack_count; k++)
      pack_positions[k] = db.db.PackPositions[pack_first + k];

    sink.bytes_total = 0;
    sink.bytes_ok = 0;
    if(plan_build(&sink, folder, &cerr) != 0) {
      fprintf(stderr, "folder %u: cannot build the entry plan\n",
              (unsigned)folder);
      rc = 1;
      continue;
    }

    if(sz_chain_parse(&chain, blob, blob_size, pack_positions, pack_count, cu,
                      unpack_size, sz_chain_default_limits(), &cerr) != 0) {
      printf("folder %-2u FAIL parse: %s: %s\n", (unsigned)folder,
             sz_chain_status_string(cerr.status), cerr.message);
      g_failures++;
      rc = 1;
      plan_free(&sink);
      continue;
    }
    sz_chain_describe(chain, desc, sizeof(desc));

    {
      sz_chain_err_t derr;
      if(sz_chain_decode(chain, reader_at, &reader, sink_write, &sink, NULL,
                         NULL, &folder_crc, &derr) != 0) {
        printf("folder %-2u FAIL decode [%s]: %s: %s (offset %llu)\n",
               (unsigned)folder, desc, sz_chain_status_string(derr.status),
               derr.message, (unsigned long long)derr.offset);
        g_failures++;
        rc = 1;
      } else if(sink.bytes_total != unpack_size) {
        printf("folder %-2u FAIL size: %llu decoded, %llu declared\n",
               (unsigned)folder, (unsigned long long)sink.bytes_total,
               (unsigned long long)unpack_size);
        g_failures++;
        rc = 1;
      } else if(SzBitWithVals_Check(&db.db.FolderCRCs, folder) &&
                folder_crc != db.db.FolderCRCs.Vals[folder]) {
        printf("folder %-2u FAIL crc: got %08X, expected %08X\n",
               (unsigned)folder, (unsigned)folder_crc,
               (unsigned)db.db.FolderCRCs.Vals[folder]);
        g_failures++;
        rc = 1;
      } else {
        printf("folder %-2u ok   [%s] %llu bytes, %u entr%s\n",
               (unsigned)folder, desc, (unsigned long long)sink.bytes_total,
               (unsigned)sink.plan_len, sink.plan_len == 1 ? "y" : "ies");
      }
    }

    if(sink.plan_pos != sink.plan_len) {
      printf("folder %-2u FAIL only %u of %u entries were produced\n",
             (unsigned)folder, (unsigned)sink.plan_pos,
             (unsigned)sink.plan_len);
      g_failures++;
      rc = 1;
    }
    sink_close_entry(&sink);
    sz_chain_free(chain);
    plan_free(&sink);
  }

  ISzAlloc_Free(&g_alloc, look_stream.buf);
  SzArEx_Free(&db, &g_alloc);
  File_Close(&archive_stream.file);

  printf("%s: %d failure(s)\n", argv[1], g_failures);
  return rc ? 1 : 0;
}
