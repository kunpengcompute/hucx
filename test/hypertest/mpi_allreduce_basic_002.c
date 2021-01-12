/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce预定义操作sum、prod支持的所有数据类型测试
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 5, 10, 20, 25, 50, 100, 250, 600, 1000, 2000, 3000, 1000000};

#define sum_test_inplace(type, mpi_type)                        \
    {                                                           \
        DECL_MALLOC_INOUT_SOL(type);                            \
        SET_INDEX_SUM(io, 0);                                   \
        SET_INDEX_FACTOR(sol, size);                            \
        ALLREDUCE_AND_FREE_INPLACE(mpi_type, MPI_SUM, io, sol); \
    }

#define prod_test_inplace(type, mpi_type)                       \
    {                                                           \
        DECL_MALLOC_INOUT_SOL(type);                            \
        SET_INDEX_SUM(io, 0);                                   \
        SET_INDEX_POWER(sol, size);                             \
        ALLREDUCE_AND_FREE_INPLACE(mpi_type, MPI_PROD, io, sol);\
    }

#define sum_and_prod_test(type, mpi_type)                       \
    do {                                                        \
        sum_test_inplace(type, mpi_type)                        \
        prod_test_inplace(type, mpi_type)                       \
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
    sum_and_prod_test(int, MPI_INT);
    sum_and_prod_test(long, MPI_LONG);
    sum_and_prod_test(short, MPI_SHORT);
    sum_and_prod_test(unsigned short, MPI_UNSIGNED_SHORT);
    sum_and_prod_test(unsigned, MPI_UNSIGNED);
    sum_and_prod_test(unsigned long, MPI_UNSIGNED_LONG);
    sum_and_prod_test(unsigned char, MPI_UNSIGNED_CHAR);
    sum_and_prod_test(float, MPI_FLOAT);
    sum_and_prod_test(double, MPI_DOUBLE);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        sum_test_inplace(int, MPI_INT);
    }

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

