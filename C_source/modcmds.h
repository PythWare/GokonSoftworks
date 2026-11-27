#ifndef MODCMDS_H
#define MODCMDS_H
#include "json.h"
#include "proto.h"
#include "util.h"

int modcmds_dispatch(job_ctx *job, const json_value *req, const char *cmd, err *e);

#endif
