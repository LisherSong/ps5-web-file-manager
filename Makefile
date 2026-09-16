ifneq ($(filter-out linux linux-deps clean,$(MAKECMDGOALS)),)
  ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
  else
    $(error PS5_PAYLOAD_SDK is undefined)
  endif
endif
ifeq ($(MAKECMDGOALS),)
  ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
  else
    $(error PS5_PAYLOAD_SDK is undefined)
  endif
endif

# Bump this together with the git tag -- it is baked into the binary (the PS5
# notification and `--version` print it) AND into the output filename, so a
# stale value silently mislabels everything. Override per-build with:
#   make VERSION_TAG=v1.9.2
VERSION_TAG ?= v1.9.1
TITLE_ID    := FMGR88888
PYTHON      ?= python3
STRIP       ?= $(PS5_PAYLOAD_SDK)/bin/prospero-strip
PKG_CONFIG  ?= $(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config
HOST_CC     ?= cc
HOST_STRIP  ?= strip
HOST_PKG_CONFIG ?= pkg-config

# Output filename carries the version so two builds never overwrite each other
# and you can tell at a glance which ELF is on the USB stick.
BIN        := web-file-mgr-$(VERSION_TAG).elf
LINUX_BIN  := web-file-mgr-linux-$(VERSION_TAG)
COMMON_SRCS := src/main.c src/websrv.c src/filemgr.c src/file_response.c src/task.c src/upload.c src/download.c src/text.c src/list.c src/space.c src/version.c src/fs_util.c src/json_util.c src/path_util.c src/asset.c src/mime.c src/notify.c src/pkg_installer.c src/pkg_info.c src/extract.c src/zip_extract.c src/rar_extract.c src/zipx_volume.c src/zipx_volstream.c src/zipx_common.c src/sevenz_extract.c src/sevenz_chain.c src/sevenz_volstream.c src/sevenz_mt.c
PS5_SRCS    := $(COMMON_SRCS) src/app_installer.c src/cpu_support_stub.c
LINUX_SRCS  := $(COMMON_SRCS)
BASE_ASSETS := $(filter-out %.dds,$(wildcard assets/*))
ifneq ($(filter linux,$(MAKECMDGOALS)),)
ASSETS      := $(BASE_ASSETS)
else
ASSETS      := $(filter-out assets/icon0.png,$(BASE_ASSETS))
endif
GEN_SRCS    := $(patsubst assets/%,gen/%, $(ASSETS:=.c))

# Vendored third-party: zlib + minizip-ng (ZIP, C) and unrar 7.20.1 (RAR,
# C++). unrar sources are compiled as a static library in RARDLL mode (no
# main()); the project talks to it through the extern "C" DLL API in
# third_party/unrar7/unrar_c_api.h. Compiled with relaxed warnings (-w) —
# these are not our code and we do not want to chase upstream style updates.
#
# C++ compilers: PS5 uses prospero-clang++ (FreeBSD-style sysroot; the
# toolchain defaults to -stdlib=libc++, driver links libc++ automatically);
# host builds use the plain host C++ compiler (libstdc++).
CXX            ?= $(dir $(CC))prospero-clang++
HOST_CXX       ?= c++

# Source set mirrors UnRARDll.vcxproj's ClCompile list (49 files) MINUS the
# Windows-only isnt.cpp / motw.cpp (they need windows.h; the official unrar
# UNIX makefile omits them, and PS5/linux both use the _UNIX branch where
# their symbols are #ifdef'd out).
UNRAR7_SRCS := \
  third_party/unrar7/archive.cpp third_party/unrar7/arcread.cpp third_party/unrar7/blake2s.cpp \
  third_party/unrar7/cmddata.cpp third_party/unrar7/consio.cpp third_party/unrar7/crc.cpp \
  third_party/unrar7/crypt.cpp third_party/unrar7/dll.cpp third_party/unrar7/encname.cpp \
  third_party/unrar7/errhnd.cpp third_party/unrar7/extinfo.cpp third_party/unrar7/extract.cpp \
  third_party/unrar7/filcreat.cpp third_party/unrar7/file.cpp third_party/unrar7/filefn.cpp \
  third_party/unrar7/filestr.cpp third_party/unrar7/find.cpp third_party/unrar7/getbits.cpp \
  third_party/unrar7/global.cpp third_party/unrar7/hash.cpp third_party/unrar7/headers.cpp \
  third_party/unrar7/largepage.cpp third_party/unrar7/match.cpp \
  third_party/unrar7/options.cpp third_party/unrar7/pathfn.cpp \
  third_party/unrar7/qopen.cpp third_party/unrar7/rar.cpp third_party/unrar7/rarpch.cpp \
  third_party/unrar7/rarvm.cpp third_party/unrar7/rawread.cpp third_party/unrar7/rdwrfn.cpp \
  third_party/unrar7/rijndael.cpp third_party/unrar7/rs.cpp third_party/unrar7/rs16.cpp \
  third_party/unrar7/scantree.cpp third_party/unrar7/secpassword.cpp third_party/unrar7/sha1.cpp \
  third_party/unrar7/sha256.cpp third_party/unrar7/smallfn.cpp third_party/unrar7/strfn.cpp \
  third_party/unrar7/strlist.cpp third_party/unrar7/system.cpp third_party/unrar7/threadpool.cpp \
  third_party/unrar7/timefn.cpp third_party/unrar7/ui.cpp third_party/unrar7/unicode.cpp \
  third_party/unrar7/unpack.cpp third_party/unrar7/volume.cpp

THIRD_PARTY_C_SRCS   := $(wildcard third_party/zlib/src/*.c) $(wildcard third_party/minizip-ng/src/*.c) $(wildcard third_party/7z/*.c)
# AesOpt.c hard-codes x86 AES-NI / AVX / VAES intrinsics and guards them with
# a compiler-version check that lets clang 18 in unconditionally. The plain
# intrinsics (`_mm256_aesenc_epi128`) live behind <wmmintrin_aes.h>, which
# clang only declares after `+mvaes +mavx2` (or higher). PS5 is Zen 2 and has
# every one of these, so we just enable them for the 7z TU family instead of
# dropping AesOpt.c (Aes.c references those HW symbol names via AesGenTables).
SEVENZ_C_FLAGS        := -maes -mavx2 -mvaes
THIRD_PARTY_C_FLAGS  := -O2 -w -Ithird_party/zlib/include -Ithird_party/minizip-ng/include -Ithird_party/7z \
  -DHAVE_ZLIB -DZLIB_COMPAT -DHAVE_UNISTD_H=1 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
  -DHAVE_FSEEKO -DZ7_PPMD_SUPPORT
THIRD_PARTY_C_FLAGS_7Z := $(THIRD_PARTY_C_FLAGS) $(SEVENZ_C_FLAGS)

# Assembly-optimised LZMA decoder (optional, on when jwasm is present).
#
# LzmaDec.c carries a compile-time switch: with Z7_LZMA_DEC_OPT it calls an
# external LzmaDec_DecodeReal_3() and drops its own C implementation; without
# it, the C version is used. The asm version is measurably faster -- on a
# 330 MiB LZMA2 archive, 1.10 s vs 1.39 s, i.e. most of the gap to the
# official 7-Zip binary, which builds with this switch on.
#
# LzmaDecOpt.asm is MASM syntax, so it needs a MASM-compatible assembler
# (jwasm). That is not something we can assume the host has, so the whole
# optimisation is conditional: no jwasm, no asm, and the build still works.
# ABI_LINUX is load-bearing -- 7zAsm.asm keys its calling convention off it
# (SysV rdi/rsi/rdx vs Win64 rcx/rdx/r8); assembling without it links cleanly
# and then segfaults on the first call.
JWASM             ?= jwasm
LZMA_DEC_ASM_DIR  := third_party/7z/Asm/x86
LZMA_DEC_ASM_SRC  := $(LZMA_DEC_ASM_DIR)/LzmaDecOpt.asm
ifneq ($(shell command -v $(JWASM) 2>/dev/null),)
  LZMA_DEC_OPT_FLAG := -DZ7_LZMA_DEC_OPT
  PS5_ASM_OBJS      := ps5-obj/$(LZMA_DEC_ASM_DIR)/LzmaDecOpt.o
  LINUX_ASM_OBJS    := linux-obj/$(LZMA_DEC_ASM_DIR)/LzmaDecOpt.o
endif
UNRAR7_CXX_FLAGS     := -O2 -w -std=c++17 -DRARDLL -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
# prospero-clang++ defaults to -stdlib=libc++; state it explicitly for clarity.
UNRAR7_CXX_FLAGS_PS5 := $(UNRAR7_CXX_FLAGS) -stdlib=libc++
UNRAR7_CXX_FLAGS_HOST:= $(UNRAR7_CXX_FLAGS)

PS5_TP_OBJS   := $(patsubst %.c,ps5-obj/%.o,$(THIRD_PARTY_C_SRCS)) \
                 $(patsubst %.cpp,ps5-obj/%.o,$(UNRAR7_SRCS))
LINUX_TP_OBJS := $(patsubst %.c,linux-obj/%.o,$(THIRD_PARTY_C_SRCS)) \
                 $(patsubst %.cpp,linux-obj/%.o,$(UNRAR7_SRCS))

CFLAGS := -Oz -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Werror -ffunction-sections -fdata-sections -Isrc -Ithird_party/minizip-ng/include -Ithird_party/unrar7 -Ithird_party/7z -DVERSION_TAG=\"$(VERSION_TAG)\" -DTITLE_ID=\"$(TITLE_ID)\"
CFLAGS += `$(PKG_CONFIG) libmicrohttpd --cflags`
LDFLAGS := -Wl,--gc-sections
LDADD  := `$(PKG_CONFIG) libmicrohttpd --libs`
LDADD  += -lSceIpmi -lSceAppInstUtil -lSceUserService
LINUX_CFLAGS := -O2 -flto -Wall -Werror -Isrc -Ithird_party/minizip-ng/include -Ithird_party/unrar7 -Ithird_party/7z -DVERSION_TAG=\"$(VERSION_TAG)\" -DTITLE_ID=\"$(TITLE_ID)\"
LINUX_CFLAGS += `$(HOST_PKG_CONFIG) libmicrohttpd --cflags`
LINUX_LDADD := `$(HOST_PKG_CONFIG) libmicrohttpd --libs` -pthread

.PHONY: all linux deps linux-deps clean

all: deps $(BIN)

linux: linux-deps $(LINUX_BIN)

deps:
	@$(PKG_CONFIG) --exists libmicrohttpd || ./install-libmicrohttpd.sh

linux-deps:
	@$(HOST_PKG_CONFIG) --exists libmicrohttpd || \
	  (echo "libmicrohttpd development package is required for make linux" >&2; exit 1)

gen:
	mkdir gen

clean:
	rm -rf $(BIN) $(LINUX_BIN) gen ps5-obj linux-obj

gen/%.c: assets/% gen-asset-module.py | gen
	$(PYTHON) gen-asset-module.py --path $* $< > $@

# Only LzmaDec.c changes behaviour under the switch: it stops defining its own
# decoder and declares the external symbol instead. Everything else in the 7z
# TU family is unaffected.
ifneq ($(LZMA_DEC_OPT_FLAG),)
ps5-obj/third_party/7z/LzmaDec.o:   THIRD_PARTY_C_FLAGS_7Z += $(LZMA_DEC_OPT_FLAG)
linux-obj/third_party/7z/LzmaDec.o: THIRD_PARTY_C_FLAGS_7Z += $(LZMA_DEC_OPT_FLAG)
endif

# make does not track flag changes, and installing or removing jwasm flips the
# switch above. Without this, an existing LzmaDec.o silently keeps the old
# decoder and the asm object just sits in the link line unreferenced (the
# binary comes out byte-identical, which is how the problem was noticed).
ps5-obj/third_party/7z/LzmaDec.o:   Makefile
linux-obj/third_party/7z/LzmaDec.o: Makefile

ps5-obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(if $(findstring third_party/7z,$<),$(THIRD_PARTY_C_FLAGS_7Z),$(THIRD_PARTY_C_FLAGS)) -c -o $@ $<

linux-obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(if $(findstring third_party/7z,$<),$(THIRD_PARTY_C_FLAGS_7Z),$(THIRD_PARTY_C_FLAGS)) -c -o $@ $<

# The assembler emits a plain ELF64 relocatable object, which both linkers
# (prospero-clang++ for PS5, cc for linux) accept as-is.
ps5-obj/$(LZMA_DEC_ASM_DIR)/LzmaDecOpt.o: $(LZMA_DEC_ASM_SRC)
	@mkdir -p $(dir $@)
	$(JWASM) -elf64 -q -DABI_LINUX -I$(LZMA_DEC_ASM_DIR) -Fo$@ $<

linux-obj/$(LZMA_DEC_ASM_DIR)/LzmaDecOpt.o: $(LZMA_DEC_ASM_SRC)
	@mkdir -p $(dir $@)
	$(JWASM) -elf64 -q -DABI_LINUX -I$(LZMA_DEC_ASM_DIR) -Fo$@ $<

ps5-obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(UNRAR7_CXX_FLAGS_PS5) -c -o $@ $<

linux-obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(UNRAR7_CXX_FLAGS_HOST) -c -o $@ $<

# Link with the C++ driver so libc++ (PS5) / libstdc++ (host) is pulled in
# automatically for the unrar objects. The project's own C sources are passed
# through -x c (clang++ would otherwise compile .c files as C++ and trip
# -Wdeprecated); -x none restores extension-based handling for the .o files.
$(BIN): $(PS5_SRCS) $(GEN_SRCS) $(PS5_TP_OBJS) $(PS5_ASM_OBJS)
	$(CXX) $(CFLAGS) $(LDFLAGS) -o $@ -x c $(filter %.c,$^) -x none $(PS5_TP_OBJS) $(PS5_ASM_OBJS) $(LDADD)
	$(STRIP) $@

$(LINUX_BIN): $(LINUX_SRCS) $(GEN_SRCS) $(LINUX_TP_OBJS) $(LINUX_ASM_OBJS)
	$(HOST_CXX) $(LINUX_CFLAGS) -o $@ -x c $(filter %.c,$^) -x none $(LINUX_TP_OBJS) $(LINUX_ASM_OBJS) $(LINUX_LDADD)
	$(HOST_STRIP) $@
