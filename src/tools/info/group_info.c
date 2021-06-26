/**
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#include "ucx_info.h"

#include <ucg/api/ucg_mpi.h>
#include <ucg/api/ucg_mpi.h>
#include <ucs/debug/memtrack.h>

/* In accordance with @ref enum ucg_predefined */
const char *collective_names[] = {
    "barrier",
    "reduce",
    "gather",
    "bcast",
    "scatter",
    "allreduce",
    NULL
};

#define EMPTY UCG_GROUP_MEMBER_DISTANCE_LAST
