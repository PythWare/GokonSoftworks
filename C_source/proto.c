#define WIN32_LEAN_AND_MEAN
#include "proto.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static CRITICAL_SECTION emit_lock;
static int emit_ready;
static proto_hook_fn emit_hook;

void proto_set_hook(proto_hook_fn hook) {
    emit_hook = hook;
}

void proto_init(void) {
    InitializeCriticalSection(&emit_lock);
    emit_ready = 1;
}

void proto_shutdown(void) {
    if (emit_ready) {
        DeleteCriticalSection(&emit_lock);
        emit_ready = 0;
    }
}

void proto_emit(const char *text, size_t len) {
    EnterCriticalSection(&emit_lock);
    fwrite(text, 1, len, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    LeaveCriticalSection(&emit_lock);
}

void job_reset(job_ctx *job, int64_t id) {
    job->id = id;
    InterlockedExchange(&job->cancel, 0);
}

void job_cancel(job_ctx *job) {
    InterlockedExchange(&job->cancel, 1);
}

int job_cancelled(const job_ctx *job) {
    return job->cancel != 0;
}

static void emit_writer(json_writer *w) {
    if (jw_finish(w)) {
        proto_emit(w->out.data, w->out.len);
    }
    jw_free(w);
}

void emit_progress(job_ctx *job, int64_t done, int64_t total, const char *msg) {
    if (emit_hook != NULL) {
        emit_hook(job, "progress", done, total, msg == NULL ? "" : msg);
        return;
    }
    json_writer w;
    jw_init(&w, NULL);
    jw_obj_open(&w);
    jw_kv_i64(&w, "id", job->id);
    jw_kv_str(&w, "ev", "progress");
    jw_kv_i64(&w, "done", done);
    jw_kv_i64(&w, "total", total);
    jw_kv_str(&w, "msg", msg == NULL ? "" : msg);
    jw_obj_close(&w);
    emit_writer(&w);
}

void emit_log(job_ctx *job, const char *level, const char *fmt, ...) {
    char text[ERR_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (emit_hook != NULL) {
        emit_hook(job, level, 0, 0, text);
        return;
    }

    json_writer w;
    jw_init(&w, NULL);
    jw_obj_open(&w);
    jw_kv_i64(&w, "id", job->id);
    jw_kv_str(&w, "ev", "log");
    jw_kv_str(&w, "level", level);
    jw_kv_str(&w, "msg", text);
    jw_obj_close(&w);
    emit_writer(&w);
}

void emit_err_text(job_ctx *job, const char *text) {
    if (emit_hook != NULL) {
        emit_hook(job, "err", 0, 0, text);
        return;
    }
    json_writer w;
    jw_init(&w, NULL);
    jw_obj_open(&w);
    jw_kv_i64(&w, "id", job->id);
    jw_kv_str(&w, "ev", "err");
    jw_kv_str(&w, "msg", text);
    jw_obj_close(&w);
    emit_writer(&w);
}

void emit_err(job_ctx *job, const char *fmt, ...) {
    char text[ERR_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    emit_err_text(job, text);
}

void emit_ok_open(job_ctx *job, json_writer *w) {
    jw_init(w, NULL);
    jw_obj_open(w);
    jw_kv_i64(w, "id", job->id);
    jw_kv_str(w, "ev", "ok");
    jw_key(w, "result");
}

void emit_ok_close(job_ctx *job, json_writer *w) {
    (void)job;
    jw_obj_close(w);
    emit_writer(w);
}

void emit_ok_empty(job_ctx *job) {
    json_writer w;
    emit_ok_open(job, &w);
    jw_obj_open(&w);
    jw_obj_close(&w);
    emit_ok_close(job, &w);
}
