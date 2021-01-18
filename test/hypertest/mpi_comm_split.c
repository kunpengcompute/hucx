/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: 测试MPI_Comm_split()及新通信域下的集合操作功能
 * Author: shizhibao
 * Create: 2021-01-12
 */

#include "mpi_test_common.h"

#define NEW_COMM_COUNT 1000
static int g_count[] = {0, 1, 10, 1000};

#define check_and_return()              \
    if (rc) {                           \
        free(io); free(sol);            \
        return 1;                       \
    } else {                            \
        for (i = 0; i < count; i++) {   \
            if (io[i] != sol[i]) {      \
                free(io); free(sol);    \
                return 2;               \
            }                           \
        }                               \
    }

static int test_allreduce(MPI_Comm mpi_comm)
{
    int rank, size, count, ans;
    int i, rc;

    MPI_Comm_rank(mpi_comm, &rank);
    MPI_Comm_size(mpi_comm, &size);

    ans = size * (size - 1) / 2;
    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        DECL_MALLOC_INOUT_SOL(long);
        SET_INDEX_SUM(io, rank);
        SET_INDEX_SUM_SIZE(sol, ans);
        rc = MPI_Allreduce(MPI_IN_PLACE, io, count, MPI_LONG, MPI_SUM, mpi_comm);
        check_and_return();
    }

    return 0;
}

static int test_bcast(MPI_Comm mpi_comm)
{
    int rank, size, count;
    int i, rc;

    MPI_Comm_rank(mpi_comm, &rank);
    MPI_Comm_size(mpi_comm, &size);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        DECL_MALLOC_INOUT_SOL(int);
        SET_INDEX_SUM(sol, 987654321);
        if (rank == 0) {
            SET_INDEX_SUM(io, 987654321);
        } else {
            SET_INDEX_CONST(io, -1);
        }
        rc = MPI_Bcast(io, count, MPI_INT, 0, mpi_comm);
        check_and_return();
    }

    return 0;
}

#define free_and_abort()                    \
    while (k >= 0) {                        \
        MPI_Comm_free(&new_comm[k]);        \
        k--;                                \
    }                                       \
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE)

int main(int argc, char *argv[])
{
    int world_rank, world_size;
    MPI_Comm new_comm[NEW_COMM_COUNT];
    int k, repeats;
    int color, key;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (world_size < 4) {
        goto wait_and_out;
    }

    repeats = 3;
    while (repeats--) {
        for (k = 0; k < NEW_COMM_COUNT; k++) {
            color = world_rank % 2;
            key = (repeats % 2) ? world_rank : -world_rank;
            MPI_Comm_split(MPI_COMM_WORLD, color, key, &new_comm[k]);
            if (k == 0) {
                if (new_comm[k] != MPI_COMM_NULL && test_allreduce(new_comm[k])) {
                    free_and_abort();
                }
                if (new_comm[k] != MPI_COMM_NULL && test_bcast(new_comm[k])) {
                    free_and_abort();
                }
            }
        }
        for (k = 0; k < NEW_COMM_COUNT; k++) {
            if (new_comm[k] != MPI_COMM_NULL) {
                MPI_Comm_free(&new_comm[k]);
            }
        }
    }

wait_and_out:
    MPI_Barrier(MPI_COMM_WORLD);
    if (world_rank == 0) {
        printf("%s\b\b success\n", __FILE__);
    }

    MPI_Finalize();
    return 0;
}

