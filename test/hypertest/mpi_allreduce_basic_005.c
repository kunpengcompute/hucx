/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce预定义操作maxloc、minloc支持的所有数据类型测试 + 长稳测试
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 5, 10, 20, 25, 50, 100, 250, 600, 1000, 2000, 3000, 1000000};

#define test1(type, mpi_type)                                           \
    {                                                                   \
        DECL_MALLOC_INOUT_SOL(type);                                    \
        SET_INDEX_STRUCT_SUM(io, -rank, a);                             \
        SET_INDEX_STRUCT_CONST(io, rank, b);                            \
        SET_INDEX_STRUCT_SUM(sol, 0, a);                                \
        SET_INDEX_STRUCT_CONST(sol, 0, b);                              \
        STRUCT_ALLREDUCE_NOT_FREE_INPLACE(mpi_type, MPI_MAXLOC, io, sol);\
        SET_INDEX_STRUCT_SUM(io, -rank, a);                             \
        SET_INDEX_STRUCT_CONST(io, rank, b);                            \
        SET_INDEX_STRUCT_SUM(sol, 1 - size, a);                         \
        SET_INDEX_STRUCT_CONST(sol, size - 1, b);                       \
        STRUCT_ALLREDUCE_AND_FREE_INPLACE(mpi_type, MPI_MINLOC, io, sol);\
    }

#define repeat_test1(type, mpi_type)                                    \
    {                                                                   \
        int val, result;                                                \
        DECL_MALLOC_INOUT_SOL(type);                                    \
        while (repeats--) {                                             \
            val = rank - repeats;                                       \
            result = size - 1 - repeats;                                \
            SET_INDEX_STRUCT_SUM(io, val, a);                           \
            SET_INDEX_STRUCT_CONST(io, rank, b);                        \
            SET_INDEX_STRUCT_SUM(sol, result, a);                       \
            SET_INDEX_STRUCT_CONST(sol, size - 1, b);                   \
            STRUCT_ALLREDUCE_NOT_FREE_INPLACE(mpi_type, MPI_MAXLOC, io, sol);\
        }                                                               \
        free(io); free(sol);                                            \
    }

#define repeat_test2(type, mpi_type)                                    \
    {                                                                   \
        int val, result;                                                \
        DECL_MALLOC_INOUT_SOL(type);                                    \
        while (repeats--) {                                             \
            val = rank - repeats;                                       \
            result = -repeats;                                          \
            SET_INDEX_STRUCT_SUM(io, val, a);                           \
            SET_INDEX_STRUCT_CONST(io, rank, b);                        \
            SET_INDEX_STRUCT_SUM(sol, result, a);                       \
            SET_INDEX_STRUCT_CONST(sol, 0, b);                          \
            STRUCT_ALLREDUCE_NOT_FREE_INPLACE(mpi_type, MPI_MINLOC, io, sol);\
        }                                                               \
        free(io); free(sol);                                            \
    }

struct int_test {
    int a;
    int b;
};

struct long_test {
    long a;
    int b;
};

struct short_test {
    short a;
    int b;
};

struct float_test {
    float a;
    int b;
};

struct double_test {
    double a;
    int b;
};

struct long_double_test {
    long double a;
    int b;
};

int main(int argc, char *argv[])
{
    int rank, size;
    int i, count, repeats;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* test all datatype that op support */
    count = 6;
    test1(struct int_test, MPI_2INT);
    test1(struct long_test, MPI_LONG_INT);
    test1(struct short_test, MPI_SHORT_INT);
    test1(struct float_test, MPI_FLOAT_INT);
    test1(struct double_test, MPI_DOUBLE_INT);
    test1(struct long_double_test, MPI_LONG_DOUBLE_INT);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        test1(struct long_test, MPI_LONG_INT);
    }

    /* loop repeats times on contiguous datatype */
    repeats = 100000;
    count = 3;
    repeat_test1(struct int_test, MPI_2INT);

    /* loop repeats times on non-contiguous datatype */
    repeats = 10000;
    count = 3;
    repeat_test2(struct long_test, MPI_LONG_INT);

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

