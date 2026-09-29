#include "pkg_installer.h"

#ifndef __linux__

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Native sceAppInstUtil MetaInfo ABI is 6 pointers (0x30). The old 0x38 layout
   carried two extra Mono-managed fields (slot, is_playgo_enabled) that do not
   exist in the firmware's native struct; passing the oversized struct shifts
   every subsequent argument and makes InstallByPackage fail or misbehave. */
typedef struct pkg_metadata {
  const char *uri;
  const char *ex_uri;
  const char *playgo_scenario_id;
  const char *content_id;
  const char *content_name;
  const char *icon_url;
} pkg_metadata_t;

_Static_assert(sizeof(pkg_metadata_t) == 0x30,
               "sceAppInstUtil metadata ABI mismatch");

/* The stock process lacks the privilege sceAppInstUtil needs. kstuff/etaHEN
   expose kernel_set_ucred_authid through libkernel_sys; raising the authid to
   the debug value before install is what lets the call succeed on a real
   console. Declared here (PS5 build only) and resolved by -lkernel_sys. */
int kernel_set_ucred_authid(uint64_t authid);

#ifndef DEBUG_AUTHID
#define DEBUG_AUTHID 0x4800000000000006ULL
#endif

#define PKG_INSTALL_PRIV_FAILED 0x80000001

typedef struct pkg_info {
  char content_id[48];
  int type;
  int platform;
} pkg_info_t;

typedef struct playgo_info {
  char languages[30][8];
  char scenario_ids[64][3];
  char content_ids[64][48];
  long unknown[810];
} playgo_info_t;

_Static_assert(sizeof(playgo_info_t) == 0x2700,
               "sceAppInstUtil PlayGoInfo ABI mismatch");

int sceAppInstUtilInitialize(void);
int sceAppInstUtilInstallByPackage(const pkg_metadata_t *, pkg_info_t *,
                                   playgo_info_t *);

static pthread_mutex_t installer_lock = PTHREAD_MUTEX_INITIALIZER;
static int installer_initialized;

static int
initialize_locked(void) {
  int result;

  if(!installer_initialized) {
    result = sceAppInstUtilInitialize();
    if(result) return result;
    installer_initialized = 1;
  }
  return 0;
}

int
pkg_installer_initialize(void) {
  int result;

  pthread_mutex_lock(&installer_lock);
  result = initialize_locked();
  pthread_mutex_unlock(&installer_lock);
  return result;
}

int
pkg_installer_install(const char *path) {
  char install_path[PATH_MAX + sizeof("/user")];
  const char *uri = path;
  pkg_metadata_t metadata = {
    .uri = NULL,
    .ex_uri = "",
    .playgo_scenario_id = "",
    .content_id = "",
    .content_name = "",
    .icon_url = "",
  };
  pkg_info_t pkg_info = {0};
  playgo_info_t playgo_info = {0};
  int result;

  if(!path) return -1;
  if(!strncmp(path, "/data/", 6)) {
    snprintf(install_path, sizeof(install_path), "/user%s", path);
    uri = install_path;
  }
  metadata.uri = uri;

  pthread_mutex_lock(&installer_lock);
  result = initialize_locked();
  if(result) {
    pthread_mutex_unlock(&installer_lock);
    return result;
  }
  /* Raise the process authid to the debug value so sceAppInstUtil is allowed
     to install. Failure here means the kernel privilege was not granted
     (no kstuff/etaHEN present or not patched) -- report it distinctly rather
     than handing a privileged call to an unprivileged process. */
  if(kernel_set_ucred_authid(DEBUG_AUTHID)) {
    printf("pkg_installer: kernel_set_ucred_authid failed (0x%016llx)\n",
           (unsigned long long)DEBUG_AUTHID);
    pthread_mutex_unlock(&installer_lock);
    return PKG_INSTALL_PRIV_FAILED;
  }
  result = sceAppInstUtilInstallByPackage(&metadata, &pkg_info, &playgo_info);
  pthread_mutex_unlock(&installer_lock);
  return result;
}

#else

int
pkg_installer_initialize(void) {
  return PKG_INSTALL_UNSUPPORTED;
}

int
pkg_installer_install(const char *path) {
  (void)path;
  return PKG_INSTALL_UNSUPPORTED;
}

#endif
