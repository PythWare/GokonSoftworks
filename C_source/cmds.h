#ifndef CMDS_H
#define CMDS_H
#include "json.h"
#include "proto.h"
#include "util.h"

int cmd_dispatch(job_ctx *job, const json_value *req, const char *cmd, arena *a, err *e);

#endif
