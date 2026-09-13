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

VERSION_TAG := v1.9
TITLE_ID    := FMGR88888
PYTHON      ?= python3
STRIP       ?= $(PS5_PAYLOAD_SDK)/bin/prospero-strip
PKG_CONFIG  ?= $(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config
HOST_CC     ?= cc
HOST_STRIP  ?= strip
HOST_PKG_CONFIG ?= pkg-config

BIN        := web-file-mgr.elf
LINUX_BIN  := web-file-mgr-linux
COMMON_SRCS := src/main.c src/websrv.c src/filemgr.c src/file_response.c src/task.c src/upload.c src/download.c src/text.c src/list.c src/space.c src/fs_util.c src/json_util.c src/path_util.c src/asset.c src/mime.c src/notify.c src/pkg_installer.c src/pkg_info.c src/extract.c src/zip_extract.c src/rar_extract.c src/zipx_volume.c src/zipx_volstream.c src/zipx_common.c src/sevenz_extract.c src/sevenz_chain.c src/sevenz_volstream.c
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

ps5-obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(if $(findstring third_party/7z,$<),$(THIRD_PARTY_C_FLAGS_7Z),$(THIRD_PARTY_C_FLAGS)) -c -o $@ $<

linux-obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(if $(findstring third_party/7z,$<),$(THIRD_PARTY_C_FLAGS_7Z),$(THIRD_PARTY_C_FLAGS)) -c -o $@ $<

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
$(BIN): $(PS5_SRCS) $(GEN_SRCS) $(PS5_TP_OBJS)
	$(CXX) $(CFLAGS) $(LDFLAGS) -o $@ -x c $(filter %.c,$^) -x none $(PS5_TP_OBJS) $(LDADD)
	$(STRIP) $@

$(LINUX_BIN): $(LINUX_SRCS) $(GEN_SRCS) $(LINUX_TP_OBJS)
	$(HOST_CXX) $(LINUX_CFLAGS) -o $@ -x c $(filter %.c,$^) -x none $(LINUX_TP_OBJS) $(LINUX_LDADD)
	$(HOST_STRIP) $@
