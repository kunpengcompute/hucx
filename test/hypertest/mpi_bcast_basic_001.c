/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Bcast支持的所有预定义数据类型测试
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 5, 10, 20, 25, 50, 100, 250, 600, 1000, 2000, 3000, 1000000};

/* for integer */
#define bcast_test1(type, mpi_type)                             \
    {                                                           \
        DECL_MALLOC_INOUT_SOL(type);                            \
        if (rank == root) { SET_INDEX_SUM(io, 66); }            \
        else { SET_INDEX_CONST(io, -1); }                       \
        SET_INDEX_SUM(sol, 66);                                 \
        BCAST_AND_FREE(mpi_type, root, io, sol);                \
    }

/* for float point */
#define bcast_test2(type, mpi_type)                             \
    {                                                           \
        DECL_MALLOC_INOUT_SOL(type);                            \
        if (rank == root) { SET_INDEX_SUM(io, 3.1415926535898); } \
        else { SET_INDEX_CONST(io, 1.23); }                     \
        SET_INDEX_SUM(sol, 3.1415926535898);                    \
        BCAST_AND_FREE_FLOAT(mpi_type, root, io, sol);          \
    }

/* for struct with integer elem a & b */
#define bcast_test3(type, mpi_type)                             \
    {                                                           \
        DECL_MALLOC_INOUT_SOL(type);                            \
        SET_INDEX_STRUCT_SUM(sol, 88, a);                       \
        SET_INDEX_STRUCT_SUM(sol, 123456789, b);                \
        if (rank == root) {                                     \
            SET_INDEX_STRUCT_SUM(io, 88, a);                    \
            SET_INDEX_STRUCT_SUM(io, 123456789, b);             \
        } else {                                                \
            SET_INDEX_STRUCT_SUM(io, -1, a);                    \
            SET_INDEX_STRUCT_SUM(io, -1, b);                    \
        }                                                       \
        STRUCT_BCAST_AND_FREE(mpi_type, root, io, sol);         \
    }

/* for struct with float point elem a and integer elem b */
#define bcast_test4(type, mpi_type)                             \
    {                                                           \
        DECL_MALLOC_INOUT_SOL(type);                            \
        SET_INDEX_STRUCT_SUM(sol, 3.1415926535898, a);          \
        SET_INDEX_STRUCT_SUM(sol, 123456789, b);                \
        if (rank == root) {                                     \
            SET_INDEX_STRUCT_SUM(io, 3.1415926535898, a);       \
            SET_INDEX_STRUCT_SUM(io, 123456789, b);             \
        } else {                                                \
            SET_INDEX_STRUCT_SUM(io, 1.23, a);                  \
            SET_INDEX_STRUCT_SUM(io, -1, b);                    \
        }                                                       \
        STRUCT_BCAST_AND_FREE_FLOAT(mpi_type, root, io, sol);   \
    }

struct short_int {
    short a;
    int b;
};

struct int_int {
    int a;
    int b;
};

struct long_int {
    long a;
    int b;
};

struct float_int {
    float a;
    int b;
};

struct double_int {
    double a;
    int b;
};

int main(int argc, char *argv[])
{
    int rank, size, root;
    int i, count;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* test all datatype on all possible root */
    count = 3;
    for (i = 0; i < size; i++) {
        root = i;
        bcast_test1(int, MPI_INT);
        bcast_test1(long, MPI_LONG);
        bcast_test1(short, MPI_SHORT);
        bcast_test1(char, MPI_CHAR);
        bcast_test1(unsigned char, MPI_UNSIGNED_CHAR);
        bcast_test1(unsigned long, MPI_UNSIGNED_LONG);

        bcast_test2(float, MPI_FLOAT);
        bcast_test2(double, MPI_DOUBLE);

        bcast_test3(struct short_int, MPI_SHORT_INT);
        bcast_test3(struct int_int, MPI_2INT);
        bcast_test3(struct long_int, MPI_LONG_INT);

        bcast_test4(struct float_int, MPI_FLOAT_INT);
        bcast_test4(struct double_int, MPI_DOUBLE_INT);
    }

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        root = 0;
        bcast_test1(unsigned, MPI_UNSIGNED);
        bcast_test2(double, MPI_DOUBLE);
        bcast_test3(struct long_int, MPI_LONG_INT);
        bcast_test4(struct double_int, MPI_DOUBLE_INT);
    }

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

