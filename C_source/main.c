#define WIN32_LEAN_AND_MEAN
#include "cmds.h"
#include "gui.h"
#include "json.h"
#include "proto.h"
#include "util.h"
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct request {
    struct request *next;
    arena a;
    json_value *req;
    const char *cmd;
    int64_t id;
} request;

static CRITICAL_SECTION queue_lock;
static HANDLE queue_ready;
static request *queue_head;
static request *queue_tail;
static volatile long stop_requested;
static job_ctx active_job;

static void request_release(request *r) {
    arena_free(&r->a);
    free(r);
}

static void queue_push(request *r) {
    r->next = NULL;
    EnterCriticalSection(&queue_lock);
    if (queue_tail == NULL) {
        queue_head = r;
        queue_tail = r;
    } else {
        queue_tail->next = r;
        queue_tail = r;
    }
    LeaveCriticalSection(&queue_lock);
    ReleaseSemaphore(queue_ready, 1, NULL);
}

static request *queue_pop(void) {
    EnterCriticalSection(&queue_lock);
    request *r = queue_head;
    if (r != NULL) {
        queue_head = r->next;
        if (queue_head == NULL) {
            queue_tail = NULL;
        }
    }
    LeaveCriticalSection(&queue_lock);
    return r;
}

static void queue_purge(void) {
    EnterCriticalSection(&queue_lock);
    request *r = queue_head;
    queue_head = NULL;
    queue_tail = NULL;
    LeaveCriticalSection(&queue_lock);
    while (r != NULL) {
        request *next = r->next;
        job_ctx temp;
        temp.id = r->id;
        temp.cancel = 0;
        emit_err_text(&temp, "Cancelled before it started");
        request_release(r);
        r = next;
    }
}

static void run_request(request *r) {
    err e;
    err_clear(&e);
    job_reset(&active_job, r->id);
    if (!cmd_dispatch(&active_job, r->req, r->cmd, &r->a, &e)) {
        emit_err_text(&active_job, e.set ? e.text : "Unknown failure");
    }
    job_reset(&active_job, 0);
    request_release(r);
}

static DWORD WINAPI job_thread(LPVOID param) {
    (void)param;
    for (;;) {
        WaitForSingleObject(queue_ready, INFINITE);
        request *r = queue_pop();
        if (r == NULL) {
            if (stop_requested) {
                break;
            }
            continue;
        }
        run_request(r);
    }
    return 0;
}

static void strip_bom(buf *line) {
    if (line->len >= 3 && (unsigned char)line->data[0] == 0xEF &&
        (unsigned char)line->data[1] == 0xBB && (unsigned char)line->data[2] == 0xBF) {
        memmove(line->data, line->data + 3, line->len - 3);
        line->len -= 3;
        line->data[line->len] = 0;
    }
}

static int read_line(buf *line) {
    buf_reset(line);
    for (;;) {
        int ch = _getchar_nolock();
        if (ch == EOF) {
            strip_bom(line);
            return line->len > 0;
        }
        if (ch == '\n') {
            strip_bom(line);
            return 1;
        }
        if (ch == '\r') {
            continue;
        }
        char byte = (char)ch;
        if (!buf_put(line, &byte, 1)) {
            return 0;
        }
    }
}

static void reply_err(int64_t id, const char *text) {
    job_ctx temp;
    temp.id = id;
    temp.cancel = 0;
    emit_err_text(&temp, text);
}

static int worker_wanted(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--worker") == 0) {
            return 1;
        }
        if (strcmp(argv[i], "--gui") == 0) {
            return 0;
        }
    }
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == NULL || input == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD kind = GetFileType(input);
    return kind == FILE_TYPE_PIPE || kind == FILE_TYPE_DISK;
}

int main(int argc, char **argv) {
    if (!worker_wanted(argc, argv)) {
        return gui_main();
    }
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    setvbuf(stdout, NULL, _IOFBF, 1u << 16);

    proto_init();
    active_job.id = 0;
    active_job.cancel = 0;

    InitializeCriticalSection(&queue_lock);
    queue_ready = CreateSemaphoreW(NULL, 0, 0x7FFFFFFF, NULL);
    if (queue_ready == NULL) {
        fprintf(stderr, "Couldn't create the request semaphore\n");
        return 1;
    }
    HANDLE worker = CreateThread(NULL, 0, job_thread, NULL, 0, NULL);
    if (worker == NULL) {
        fprintf(stderr, "Couldn't start the job thread\n");
        return 1;
    }

    buf line;
    buf_init(&line);

    while (read_line(&line)) {
        if (line.len == 0) {
            continue;
        }
        request *r = (request *)malloc(sizeof(request));
        if (r == NULL) {
            reply_err(0, "Out of memory");
            continue;
        }
        arena_init(&r->a);

        err e;
        err_clear(&e);
        char *copy = arena_strdup(&r->a, line.data, line.len);
        if (copy == NULL) {
            arena_free(&r->a);
            free(r);
            reply_err(0, "Out of memory");
            continue;
        }
        r->req = json_parse(&r->a, copy, line.len, &e);
        if (r->req == NULL || r->req->kind != JSON_OBJ) {
            arena_free(&r->a);
            free(r);
            reply_err(0, e.set ? e.text : "Request must be a JSON object");
            continue;
        }
        r->id = json_as_i64(json_obj_get(r->req, "id"), 0);
        r->cmd = json_as_str(json_obj_get(r->req, "cmd"), NULL);
        if (r->cmd == NULL) {
            int64_t id = r->id;
            arena_free(&r->a);
            free(r);
            reply_err(id, "Request needs a cmd");
            continue;
        }

        if (strcmp(r->cmd, "shutdown") == 0) {
            request_release(r);
            break;
        }
        if (strcmp(r->cmd, "cancel") == 0) {
            job_cancel(&active_job);
            queue_purge();
            job_ctx temp;
            temp.id = r->id;
            temp.cancel = 0;
            emit_ok_empty(&temp);
            request_release(r);
            continue;
        }

        queue_push(r);
    }

    InterlockedExchange(&stop_requested, 1);
    ReleaseSemaphore(queue_ready, 1, NULL);
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    CloseHandle(queue_ready);
    DeleteCriticalSection(&queue_lock);

    buf_free(&line);
    proto_shutdown();
    return 0;
}
