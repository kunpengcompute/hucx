/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce预定义操作maxloc、minloc支持的所有数据类型测试 + 长稳测试
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 5, 10, 20, 25, 50, 100, 250, 600, 1000, 2000, 3000, 1000000};

#define maxloc_test(type, mpi_type)                                     \
    {                                                                   \
        DECL_MALLOC_INOUT_SOL(type);                                    \
        SET_INDEX_STRUCT_SUM(io, rank, a);                              \
        SET_INDEX_STRUCT_CONST(io, rank, b);                            \
        SET_INDEX_STRUCT_SUM(sol, size - 1, a);                         \
        SET_INDEX_STRUCT_CONST(sol, size - 1, b);                       \
        STRUCT_ALLREDUCE_AND_FREE_INPLACE(mpi_type, MPI_MAXLOC, io, sol);\
    }

#define minloc_test(type, mpi_type)                                     \
    {                                                                   \
        DECL_MALLOC_INOUT_SOL(type);                                    \
        SET_INDEX_STRUCT_SUM(io, rank, a);                              \
        SET_INDEX_STRUCT_CONST(io, rank, b);                            \
        SET_INDEX_STRUCT_SUM(sol, 0, a);                                \
        SET_INDEX_STRUCT_CONST(sol, 0, b);                              \
        STRUCT_ALLREDUCE_AND_FREE_INPLACE(mpi_type, MPI_MINLOC, io, sol);\
    }

/* for integer */
#define repeat_maxloc_test1(type, mpi_type)                             \
    {                                                                   \
        DECL_MALLOC_INOUT_SOL(type);                                    \
        SET_INDEX_STRUCT_SUM(sol, size - 1, a);                         \
        SET_INDEX_STRUCT_CONST(sol, size - 1, b);                       \
        while (repeats--) {                                             \
            SET_INDEX_STRUCT_SUM(io, rank, a);                          \
            SET_INDEX_STRUCT_CONST(io, rank, b);                        \
            STRUCT_ALLREDUCE_NOT_FREE_INPLACE(mpi_type, MPI_MAXLOC, io, sol);\
        }                                                               \
        free(io); free(sol);                                            \
    }

#define max_min_loc_test(type, mpi_type)                        \
    do {                                                        \
        maxloc_test(type, mpi_type)                             \
        minloc_test(type, mpi_type)                             \
    } while (0)

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

int main(int argc, char *argv[])
{
    int rank, size;
    int i, count, repeats;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* test all datatype that op support */
    count = 3;
    max_min_loc_test(struct int_test, MPI_2INT);
    max_min_loc_test(struct long_test, MPI_LONG_INT);
    max_min_loc_test(struct short_test, MPI_SHORT_INT);
    max_min_loc_test(struct float_test, MPI_FLOAT_INT);
    max_min_loc_test(struct double_test, MPI_DOUBLE_INT);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        maxloc_test(struct long_test, MPI_LONG_INT);
    }

    /* loop repeats times on contiguous datatype */
    repeats = 20000;
    count = 10;
    repeat_maxloc_test1(struct int_test, MPI_2INT);

    /* loop repeats times on non-contiguous datatype */
    repeats = 20000;
    count = 10;
    repeat_maxloc_test1(struct long_test, MPI_LONG_INT);

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

