/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce预定义操作max、min支持的所有数据类型测试
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 5, 10, 20, 25, 50, 100, 250, 600, 1000, 2000, 3000, 1000000};

#define max_test(type, mpi_type)                                \
    {                                                           \
        DECL_MALLOC_IN_OUT_SOL(type);                           \
        SET_INDEX_SUM(in, rank);                                \
        SET_INDEX_SUM(org, rank);                               \
        SET_INDEX_SUM(sol, size - 1);                           \
        SET_INDEX_CONST(out, 0);                                \
        ALLREDUCE_AND_FREE(mpi_type, MPI_MAX, in, out, sol, org); \
    }

#define min_test(type, mpi_type)                                \
    {                                                           \
        DECL_MALLOC_IN_OUT_SOL(type);                           \
        SET_INDEX_SUM(in, rank);                                \
        SET_INDEX_SUM(org, rank);                               \
        SET_INDEX_SUM(sol, 0);                                  \
        SET_INDEX_CONST(out, 0);                                \
        ALLREDUCE_AND_FREE(mpi_type, MPI_MIN, in, out, sol, org); \
    }

#define max_and_min_test(type, mpi_type)                        \
    do {                                                        \
        max_test(type, mpi_type)                                \
        min_test(type, mpi_type)                                \
    } while (0)

int main(int argc, char *argv[])
{
    int rank, size;
    int i, count;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* test all datatype that op support */
    count = 3;
    max_and_min_test(int, MPI_INT);
    max_and_min_test(long, MPI_LONG);
    max_and_min_test(short, MPI_SHORT);
    max_and_min_test(unsigned short, MPI_UNSIGNED_SHORT);
    max_and_min_test(unsigned, MPI_UNSIGNED);
    max_and_min_test(unsigned long, MPI_UNSIGNED_LONG);
    max_and_min_test(unsigned char, MPI_UNSIGNED_CHAR);
    max_and_min_test(float, MPI_FLOAT);
    max_and_min_test(double, MPI_DOUBLE);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        max_test(int, MPI_INT);
    }

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

