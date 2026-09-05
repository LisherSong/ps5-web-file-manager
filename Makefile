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

VERSION_TAG := v1.8
TITLE_ID    := FMGR88888
PYTHON      ?= python3
STRIP       ?= $(PS5_PAYLOAD_SDK)/bin/prospero-strip
PKG_CONFIG  ?= $(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config
HOST_CC     ?= cc
HOST_STRIP  ?= strip
HOST_PKG_CONFIG ?= pkg-config

BIN        := web-file-mgr.elf
LINUX_BIN  := web-file-mgr-linux
COMMON_SRCS := src/main.c src/websrv.c src/filemgr.c src/file_response.c src/task.c src/upload.c src/download.c src/text.c src/list.c src/space.c src/fs_util.c src/json_util.c src/path_util.c src/asset.c src/mime.c src/notify.c src/pkg_installer.c src/pkg_info.c src/extract.c src/zip_extract.c src/rar_extract.c
PS5_SRCS    := $(COMMON_SRCS) src/app_installer.c
LINUX_SRCS  := $(COMMON_SRCS)
BASE_ASSETS := $(filter-out %.dds,$(wildcard assets/*))
ifneq ($(filter linux,$(MAKECMDGOALS)),)
ASSETS      := $(BASE_ASSETS)
else
ASSETS      := $(filter-out assets/icon0.png,$(BASE_ASSETS))
endif
GEN_SRCS    := $(patsubst assets/%,gen/%, $(ASSETS:=.c))

# Vendored third-party: zlib + minizip-ng (ZIP), dmc_unrar (RAR). Compiled with
# relaxed warnings (-w) — these are not our code and we do not want to chase
# upstream style updates on every SDK upgrade.
THIRD_PARTY_SRCS   := $(wildcard third_party/zlib/src/*.c) $(wildcard third_party/minizip-ng/src/*.c) third_party/unrar/dmc_unrar.c
THIRD_PARTY_CFLAGS := -O2 -w -Ithird_party/zlib/include -Ithird_party/minizip-ng/include -Ithird_party/unrar \
  -DHAVE_ZLIB -DZLIB_COMPAT -DHAVE_UNISTD_H=1 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
  -DDMC_UNRAR_DISABLE_BE32TOH_BE64TOH=1
PS5_TP_OBJS   := $(patsubst %.c,ps5-obj/%.o,$(THIRD_PARTY_SRCS))
LINUX_TP_OBJS := $(patsubst %.c,linux-obj/%.o,$(THIRD_PARTY_SRCS))

CFLAGS := -Oz -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Werror -ffunction-sections -fdata-sections -Isrc -Ithird_party/minizip-ng/include -Ithird_party/unrar -DVERSION_TAG=\"$(VERSION_TAG)\" -DTITLE_ID=\"$(TITLE_ID)\"
CFLAGS += `$(PKG_CONFIG) libmicrohttpd --cflags`
LDFLAGS := -Wl,--gc-sections
LDADD  := `$(PKG_CONFIG) libmicrohttpd --libs`
LDADD  += -lSceIpmi -lSceAppInstUtil -lSceUserService
LINUX_CFLAGS := -O2 -flto -Wall -Werror -Isrc -Ithird_party/minizip-ng/include -DVERSION_TAG=\"$(VERSION_TAG)\" -DTITLE_ID=\"$(TITLE_ID)\"
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
	$(CC) $(THIRD_PARTY_CFLAGS) -c -o $@ $<

linux-obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(THIRD_PARTY_CFLAGS) -c -o $@ $<

$(BIN): $(PS5_SRCS) $(GEN_SRCS) $(PS5_TP_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter %.c,$^) $(PS5_TP_OBJS) $(LDADD)
	$(STRIP) $@

$(LINUX_BIN): $(LINUX_SRCS) $(GEN_SRCS) $(LINUX_TP_OBJS)
	$(HOST_CC) $(LINUX_CFLAGS) -o $@ $(filter %.c,$^) $(LINUX_TP_OBJS) $(LINUX_LDADD)
	$(HOST_STRIP) $@
