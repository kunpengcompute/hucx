/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: 测试MPI_Comm_create()及新通信域下的集合操作功能
 * Author: shizhibao
 * Create: 2021-01-12
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 10, 1000, 10000};

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
    free(new_member);                       \
    while (k >= 0) {                        \
        MPI_Comm_free(&new_comm[k]);        \
        k--;                                \
    }                                       \
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE)

#define NEW_COMM_COUNT 10000

int main(int argc, char *argv[])
{
    int world_rank, world_size;
    MPI_Group world_group, new_group;
    MPI_Comm new_comm[NEW_COMM_COUNT];
    int i, j, k, new_size, repeats;
    int *new_member = NULL;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_group(MPI_COMM_WORLD, &world_group);

    new_size = (world_size + 1) / 2;
    if (new_size < 2) {
        goto wait_and_out;
    }

    repeats = 10;
    new_member = (int *)calloc(new_size, sizeof(int));
    while (repeats--) {
        for (i = 0, j = repeats % 2; i < new_size && j < world_size; i++, j += 2) {
            new_member[i] = j;
        }
        MPI_Group_incl(world_group, i, new_member, &new_group);
        for (k = 0; k < NEW_COMM_COUNT; k++) {
            MPI_Comm_create(MPI_COMM_WORLD, new_group, &new_comm[k]);
            if (new_comm[k] != MPI_COMM_NULL && test_allreduce(new_comm[k])) {
                free_and_abort();
            }
            if (new_comm[k] != MPI_COMM_NULL && test_bcast(new_comm[k])) {
                free_and_abort();
            }
        }
        for (k = 0; k < NEW_COMM_COUNT; k++) {
            if (new_comm[k] != MPI_COMM_NULL) {
                MPI_Comm_free(&new_comm[k]);
            }
        }
    }
    free(new_member);

wait_and_out:
    MPI_Barrier(MPI_COMM_WORLD);
    if (world_rank == 0) {
        printf("%s\b\b success\n", __FILE__);
    }

    MPI_Finalize();
    return 0;
}

