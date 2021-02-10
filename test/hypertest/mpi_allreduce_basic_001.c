/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce预定义操作max、min支持的所有数据类型测试
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 5, 10, 20, 25, 50, 100, 250, 600, 1000, 2000, 3000, 1000000};

#define test_1(type, mpi_type)                                  \
    {                                                           \
        type val1 = rank % 0x80;                                \
        type val2 = -val1 - 1;                                  \
        type max = size > 0x80 ? 0x7f : size - 1;               \
        type min = size > 0x80 ? -0x80 : -size;                 \
        DECL_MALLOC_IN_OUT_SOL(type);                           \
        SET_INDEX_TWO_VAL(in, val1, val2);                      \
        SET_INDEX_TWO_VAL(org, val1, val2);                     \
        SET_INDEX_TWO_VAL(sol, max, -1);                        \
        ALLREDUCE_NOT_FREE(mpi_type, MPI_MAX, in, out, sol, org); \
        SET_INDEX_TWO_VAL(sol, 0, min);                           \
        ALLREDUCE_AND_FREE(mpi_type, MPI_MIN, in, out, sol, org); \
    }

#define test_1u(type, mpi_type)                                 \
    {                                                           \
        type val1 = rank % 0x100;                               \
        type val2 = 0xff - val1;                                \
        type max = size > 0xff ? 0xff : size - 1;               \
        type min = 0xff - max;                                  \
        DECL_MALLOC_IN_OUT_SOL(type);                           \
        SET_INDEX_TWO_VAL(in, val1, val2);                      \
        SET_INDEX_TWO_VAL(org, val1, val2);                     \
        SET_INDEX_TWO_VAL(sol, max, 0xff);                        \
        ALLREDUCE_NOT_FREE(mpi_type, MPI_MAX, in, out, sol, org); \
        SET_INDEX_TWO_VAL(sol, 0, min);                           \
        ALLREDUCE_AND_FREE(mpi_type, MPI_MIN, in, out, sol, org); \
    }

#define test_2(type, mpi_type)                                  \
    {                                                           \
        type val1 = rank;                                       \
        type val2 = -val1 - 1;                                  \
        type max = size - 1;                                    \
        type min = -size;                                       \
        DECL_MALLOC_IN_OUT_SOL(type);                           \
        SET_INDEX_TWO_VAL(in, val1, val2);                      \
        SET_INDEX_TWO_VAL(org, val1, val2);                     \
        SET_INDEX_TWO_VAL(sol, max, -1);                        \
        ALLREDUCE_NOT_FREE(mpi_type, MPI_MAX, in, out, sol, org); \
        SET_INDEX_TWO_VAL(sol, 0, min);                           \
        ALLREDUCE_AND_FREE(mpi_type, MPI_MIN, in, out, sol, org); \
    }

#define test_2u(type, mpi_type)                                 \
    {                                                           \
        type val1 = rank;                                       \
        type val2 = size - rank;                                \
        type max = size - 1;                                    \
        DECL_MALLOC_IN_OUT_SOL(type);                           \
        SET_INDEX_TWO_VAL(in, val1, val2);                      \
        SET_INDEX_TWO_VAL(org, val1, val2);                     \
        SET_INDEX_TWO_VAL(sol, max, size);                        \
        ALLREDUCE_NOT_FREE(mpi_type, MPI_MAX, in, out, sol, org); \
        SET_INDEX_TWO_VAL(sol, 0, 1);                             \
        ALLREDUCE_AND_FREE(mpi_type, MPI_MIN, in, out, sol, org); \
    }

#define test_3(type, mpi_type)                                  \
    {                                                           \
        type val1 = size + rank * 1.0e-6;                       \
        type val2 = -val1;                                      \
        type max = size + (size - 1) * 1.0e-6;                  \
        type min = -max;                                        \
        DECL_MALLOC_IN_OUT_SOL(type);                           \
        SET_INDEX_TWO_VAL(in, val1, val2);                      \
        SET_INDEX_TWO_VAL(org, val1, val2);                     \
        SET_INDEX_TWO_VAL(sol, max, -size);                       \
        ALLREDUCE_NOT_FREE_FLOAT(mpi_type, MPI_MAX, in, out, sol, org); \
        SET_INDEX_TWO_VAL(sol, size, min);                              \
        ALLREDUCE_AND_FREE_FLOAT(mpi_type, MPI_MIN, in, out, sol, org); \
    }

int main(int argc, char *argv[])
{
    int rank, size;
    int i, count;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* test all datatype that op support */
    count = 6;

    test_1(char, MPI_CHAR);
    test_1(signed char, MPI_SIGNED_CHAR);
    test_1u(unsigned char, MPI_UNSIGNED_CHAR);
    test_1(int8_t, MPI_INT8_T);
    test_1u(uint8_t, MPI_UINT8_T);

    test_2(short, MPI_SHORT);
    test_2u(unsigned short, MPI_UNSIGNED_SHORT);
    test_2(int16_t, MPI_INT16_T);
    test_2u(uint16_t, MPI_UINT16_T);

    test_2(int, MPI_INT);
    test_2u(unsigned, MPI_UNSIGNED);
    test_2(int32_t, MPI_INT32_T);
    test_2u(uint32_t, MPI_UINT32_T);

    test_2(long, MPI_LONG);
    test_2u(unsigned long, MPI_UNSIGNED_LONG);
    test_2(int64_t, MPI_INT64_T);
    test_2u(uint64_t, MPI_UINT64_T);
    test_2(long long, MPI_LONG_LONG);
    test_2u(unsigned long long, MPI_UNSIGNED_LONG_LONG);

    test_3(float, MPI_FLOAT);
    test_3(double, MPI_DOUBLE);
    test_3(long double, MPI_LONG_DOUBLE);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        test_2(int, MPI_INT);
    }

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

