/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce预定义位逻辑运算操作支持的所有数据类型测试
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 5, 10, 20, 25, 50, 100, 250, 600, 1000, 2000, 3000, 1000000};

#define band_test1(type, mpi_type)                              \
    {                                                           \
        DECL_MALLOC_IN_OUT_SOL(type);                           \
        if (rank == size-1) {                                   \
            SET_INDEX_SUM(in, 0);                               \
            SET_INDEX_SUM(org, 0);                              \
        }                                                       \
        else {                                                  \
            SET_INDEX_CONST(in, ~0);                            \
            SET_INDEX_CONST(org, ~0);                           \
        }                                                       \
        SET_INDEX_SUM(sol, 0);                                  \
        SET_INDEX_CONST(out, 0);                                \
        ALLREDUCE_AND_FREE(mpi_type, MPI_BAND, in, out, sol, org);\
    }

#define band_test2(type, mpi_type)                              \
    {                                                           \
        DECL_MALLOC_IN_OUT_SOL(type);                           \
        if (rank == size-1) {                                   \
            SET_INDEX_SUM(in, 0);                               \
            SET_INDEX_SUM(org, 0);                              \
        }                                                       \
        else {                                                  \
            SET_INDEX_CONST(in, 0);                             \
            SET_INDEX_CONST(org, 0);                            \
        }                                                       \
        SET_INDEX_CONST(sol, 0);                                \
        SET_INDEX_CONST(out, 0);                                \
        ALLREDUCE_AND_FREE(mpi_type, MPI_BAND, in, out, sol, org);\
    }

#define bor_test1(type, mpi_type)                                       \
    const_test(type, mpi_type, MPI_BOR, (rank & 0x3), ((size < 3) ? size - 1 : 0x3), 0)

#define bxor_test1(type, mpi_type)                                      \
    const_test(type, mpi_type, MPI_BXOR, (rank == 1) * 0xf0, (size > 1) * 0xf0, 0)

#define bxor_test2(type, mpi_type)                      \
    const_test(type, mpi_type, MPI_BXOR, 0, 0, 0)

#define bxor_test3(type, mpi_type)                                      \
    const_test(type, mpi_type, MPI_BXOR, ~0, (size &0x1) ? ~0 : 0, 0)

#define blogic_test(type, mpi_type)                             \
    do {                                                        \
        band_test1(type, mpi_type)                              \
        band_test2(type, mpi_type)                              \
        bor_test1(type, mpi_type)                               \
        bxor_test1(type, mpi_type)                              \
        bxor_test2(type, mpi_type)                              \
        bxor_test3(type, mpi_type)                              \
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
    blogic_test(int, MPI_INT);
    blogic_test(long, MPI_LONG);
    blogic_test(short, MPI_SHORT);
    blogic_test(unsigned short, MPI_UNSIGNED_SHORT);
    blogic_test(unsigned, MPI_UNSIGNED);
    blogic_test(unsigned long, MPI_UNSIGNED_LONG);
    blogic_test(unsigned char, MPI_UNSIGNED_CHAR);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        bxor_test1(int, MPI_INT);
    }

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

