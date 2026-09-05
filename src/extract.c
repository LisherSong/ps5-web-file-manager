#include "filemgr.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "extract.h"
#include "filemgr_internal.h"
#include "json_util.h"
#include "path_util.h"
#include "rar_extract.h"
#include "zip_extract.h"

/* Cancellation callback: stop when the task is asked to cancel. */
static int
extract_cancel(void *userdata) {
  file_task_t *task = userdata;

  return task_cancel_requested(task);
}

/* Case-insensitive suffix check. Returns 1 if path ends in suffix
   (the comparison ignores trailing slashes, so "x.rar/" is still .rar). */
static int
ends_with_ci(const char *path, const char *suffix) {
  size_t path_len = strlen(path);
  size_t suf_len = strlen(suffix);

  if(path_len < suf_len) {
    return 0;
  }
  /* Trim trailing path separators (defensive — the API rejects them but
     we get here with whatever path the caller passed). */
  while(path_len && path[path_len - 1] == '/') {
    path_len--;
  }
  if(path_len < suf_len) {
    return 0;
  }
  return !strcasecmp(path + path_len - suf_len, suffix);
}

/* Progress callback. The engine already throttles reports (200 ms / 1 MiB),
   so we can forward each report straight into the shared task state.
   Defined before extract_dispatch() so the dispatcher's call site compiles
   cleanly under -Werror=implicit-function-declaration. */
static void
extract_progress(void *userdata, const zipx_progress_t *p) {
  file_task_t *task = userdata;
  unsigned long long prev_done;
  unsigned long long delta;

  pthread_mutex_lock(&g_tasks_lock);
  task->entries_total = p->entries_total;
  task->entries_done = p->entries_done;
  task->total = p->bytes_total;
  prev_done = task->done;
  pthread_mutex_unlock(&g_tasks_lock);

  delta = p->bytes_done > prev_done ? p->bytes_done - prev_done : 0;
  task_update(task, TASK_RUNNING, p->current ? p->current : task->src,
              delta, NULL);
}

/* Pick the right engine by the archive file name. Returns ZIPX_ERR_FORMAT
   for anything that does not look like a supported archive. */
static zipx_status_t
extract_dispatch(file_task_t *task, zipx_conflict_t conflict,
                const zipx_limits_t *limits, zipx_result_t *result) {
  if(ends_with_ci(task->src, ".zip")) {
    return zipx_extract(task->src, task->dst, conflict, limits,
                        extract_cancel, extract_progress, task, result);
  }
  if(ends_with_ci(task->src, ".rar")) {
    return rar_extract(task->src, task->dst, conflict, limits,
                       extract_cancel, extract_progress, task, result);
  }
  {
    size_t len = strlen(task->src);
    if(len > sizeof(result->detail) - 1) {
      len = sizeof(result->detail) - 1;
    }
    memcpy(result->detail, task->src, len);
    result->detail[len] = 0;
    snprintf(result->message, sizeof(result->message),
             "unsupported archive format (only .zip and .rar are accepted)");
    return ZIPX_ERR_UNSUPPORTED;
  }
}

static const char *
extract_error_code(zipx_status_t status) {
  switch(status) {
  case ZIPX_ERR_OPEN: return "extract_open_failed";
  case ZIPX_ERR_FORMAT: return "extract_corrupt";
  case ZIPX_ERR_UNSUPPORTED: return "extract_unsupported";
  case ZIPX_ERR_UNSAFE_NAME: return "extract_unsafe_name";
  case ZIPX_ERR_SPECIAL: return "extract_special_entry";
  case ZIPX_ERR_DUPLICATE: return "extract_duplicate";
  case ZIPX_ERR_LIMIT_ENTRIES: return "extract_too_many_entries";
  case ZIPX_ERR_LIMIT_FILE: return "extract_entry_too_large";
  case ZIPX_ERR_LIMIT_TOTAL: return "extract_too_large";
  case ZIPX_ERR_LIMIT_RATIO: return "extract_ratio";
  case ZIPX_ERR_LIMIT_DEPTH: return "extract_too_deep";
  case ZIPX_ERR_LIMIT_NAME: return "extract_name_too_long";
  case ZIPX_ERR_CONFLICT: return "extract_conflict";
  case ZIPX_ERR_SPACE: return "no_space";
  case ZIPX_ERR_IO: return "extract_io";
  case ZIPX_ERR_CRC: return "extract_crc";
  default: return "extract_failed";
  }
}

static void
extract_set_error(file_task_t *task, zipx_status_t status,
                  const zipx_result_t *result) {
  const char *code = extract_error_code(status);
  const char *detail = result->detail[0] ? result->detail : NULL;
  const char *msg = result->message[0] ? result->message :
                    zipx_status_string(status);

  pthread_mutex_lock(&g_tasks_lock);
  snprintf(task->error_code, sizeof(task->error_code), "%s", code);
  if(detail) {
    size_t n = strlen(detail);
    if(n >= sizeof(task->error_arg)) {
      n = sizeof(task->error_arg) - 1;
    }
    memcpy(task->error_arg, detail, n);
    task->error_arg[n] = 0;
  }
  task->updated_at = time(NULL);
  pthread_mutex_unlock(&g_tasks_lock);

  task_update(task, TASK_FAILED, detail ? detail : task->src, 0, msg);
}

static void *
extract_worker(void *arg) {
  file_task_t *task = arg;
  zipx_result_t result = {0};
  zipx_conflict_t conflict;
  zipx_status_t status;

  switch(task->extract_conflict) {
  case EXTRACT_CONFLICT_OVERWRITE:
    conflict = ZIPX_CONFLICT_OVERWRITE;
    break;
  case EXTRACT_CONFLICT_MERGE:
    conflict = ZIPX_CONFLICT_MERGE;
    break;
  default:
    conflict = ZIPX_CONFLICT_FAIL;
    break;
  }

  task_update(task, TASK_RUNNING, "scanning archive", 0, NULL);

  status = extract_dispatch(task, conflict,
                            zipx_limits_profile(task->extract_large),
                            &result);

  if(status == ZIPX_OK) {
    time_t completed_at = time(NULL);

    /* Only delete the source archive when this task owns it (upload flow). */
    if(task->extract_remove_source && task->src[0]) {
      unlink(task->src);
    }
    pthread_mutex_lock(&g_tasks_lock);
    task->state = TASK_DONE;
    if(task->total) {
      task->done = task->total;
    }
    task->entries_done = task->entries_total;
    task->updated_at = completed_at;
    record_task_completion_locked(task, completed_at);
    pthread_mutex_unlock(&g_tasks_lock);
  } else if(status == ZIPX_ERR_CANCELED) {
    task_update(task, TASK_CANCELED,
                task->current[0] ? task->current : task->src, 0, "canceled");
  } else {
    extract_set_error(task, status, &result);
  }

  return NULL;
}

enum MHD_Result
api_extract(struct MHD_Connection *conn, const char *body, size_t body_size) {
  char *path = fs_path_value(body_form_value(body, body_size, "path"));
  char *dst_dir = fs_path_value(body_form_value(body, body_size, "dst_dir"));
  char *conflict_str = body_form_value(body, body_size, "conflict");
  char *remove_str = body_form_value(body, body_size, "remove_source");
  char *large_str = body_form_value(body, body_size, "large");
  extract_conflict_t conflict = EXTRACT_CONFLICT_FAIL;
  int remove_source = remove_str && !strcmp(remove_str, "1");
  int large = large_str && !strcmp(large_str, "1");
  file_task_t *task;
  strbuf_t b = {0};
  struct stat st;

  if(!path || !dst_dir || !path[0] || !dst_dir[0]) {
    free(path); free(dst_dir); free(conflict_str); free(remove_str); free(large_str);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
  }
  if(conflict_str) {
    if(!strcmp(conflict_str, "overwrite")) {
      conflict = EXTRACT_CONFLICT_OVERWRITE;
    } else if(!strcmp(conflict_str, "merge")) {
      conflict = EXTRACT_CONFLICT_MERGE;
    } else if(strcmp(conflict_str, "fail")) {
      free(path); free(dst_dir); free(conflict_str); free(remove_str); free(large_str);
      return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid conflict");
    }
  }
  if(stat(path, &st) || !S_ISREG(st.st_mode)) {
    free(path); free(dst_dir); free(conflict_str); free(remove_str); free(large_str);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "file not found");
  }
  if(stat(dst_dir, &st) || !S_ISDIR(st.st_mode)) {
    free(path); free(dst_dir); free(conflict_str); free(remove_str); free(large_str);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST,
                           "destination must be a directory");
  }

  task = calloc(1, sizeof(*task));
  if(!task) {
    free(path); free(dst_dir); free(conflict_str); free(remove_str); free(large_str);
    return send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                           "out of memory");
  }

  task->op = TASK_EXTRACT;
  task->state = TASK_QUEUED;
  task->extract_conflict = (int)conflict;
  task->extract_remove_source = remove_source;
  task->extract_large = large;
  snprintf(task->src, sizeof(task->src), "%s", path);
  snprintf(task->dst, sizeof(task->dst), "%s", dst_dir);
  task->created_at = time(NULL);
  task->updated_at = task->created_at;

  pthread_mutex_lock(&g_tasks_lock);
  remove_finished_tasks_locked();
  if(has_active_task_locked()) {
    pthread_mutex_unlock(&g_tasks_lock);
    free_task(task);
    free(path); free(dst_dir); free(conflict_str); free(remove_str); free(large_str);
    return send_json_error(conn, MHD_HTTP_CONFLICT, "another task is running");
  }
  task->id = g_next_task_id++;
  task->next = g_tasks;
  g_tasks = task;
  pthread_mutex_unlock(&g_tasks_lock);

  if(pthread_create(&task->thread, NULL, extract_worker, task)) {
    task_update(task, TASK_FAILED, NULL, 0, "pthread_create failed");
  } else {
    pthread_detach(task->thread);
  }

  free(path); free(dst_dir); free(conflict_str); free(remove_str); free(large_str);
  strbuf_printf(&b, "{\"ok\":true,\"task_id\":%lu}", task->id);
  return send_buffer(conn, MHD_HTTP_OK, b.data, "application/json");
}
