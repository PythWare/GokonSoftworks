#ifndef PROTO_H
#define PROTO_H
#include "json.h"
#include "util.h"
#define PROGRESS_STRIDE 100

typedef struct {
    int64_t id;
    volatile long cancel;
} job_ctx;

typedef void (*proto_hook_fn)(job_ctx *job, const char *kind, int64_t done, int64_t total,
                              const char *text);

void proto_set_hook(proto_hook_fn hook);
void proto_init(void);
void proto_shutdown(void);
void proto_emit(const char *text, size_t len);

void job_reset(job_ctx *job, int64_t id);
void job_cancel(job_ctx *job);
int job_cancelled(const job_ctx *job);

void emit_progress(job_ctx *job, int64_t done, int64_t total, const char *msg);
void emit_log(job_ctx *job, const char *level, const char *fmt, ...);
void emit_err(job_ctx *job, const char *fmt, ...);
void emit_err_text(job_ctx *job, const char *text);

void emit_ok_open(job_ctx *job, json_writer *w);
void emit_ok_close(job_ctx *job, json_writer *w);
void emit_ok_empty(job_ctx *job);

#endif
