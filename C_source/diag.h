#ifndef DIAG_H
#define DIAG_H
#include "util.h"
#define DIAG_RECOMMENDED_FREE 20000000000ll
#define DIAG_CRITICAL_FREE 512000000ll
#define DIAG_LONG_PATH 120
#define DIAG_PROBE "gokonsoftworks-probe"

typedef struct {
    char severity[16];
    char message[ERR_MAX];
} diag_issue;

typedef struct {
    char *path;
    char *drive;
    int have_space;
    int64_t free_bytes;
    int64_t total_bytes;
    int can_read;
    int can_write;
    int can_create;
    int can_delete;
    const char *protected_name;
    diag_issue *issues;
    size_t issue_count;
} gs_diagnostics;

const char *diag_protected_location(const char *path);
int diag_run(const char *path, gs_diagnostics *out);
void diag_free(gs_diagnostics *d);
int diag_has_errors(const gs_diagnostics *d);
int diag_has_warnings(const gs_diagnostics *d);
const char *diag_status(const gs_diagnostics *d);
char *diag_report_text(const gs_diagnostics *d);

#endif
