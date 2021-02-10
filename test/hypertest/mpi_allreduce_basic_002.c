/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce预定义操作sum、prod支持的所有数据类型测试
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 5, 10, 20, 25, 50, 100, 250, 600, 1000, 2000, 3000, 1000000};

#define test_1(type, mpi_type)                                  \
    {                                                           \
        type val1 = rank % 2 ? 1 : -1;                          \
        type val2 = -val1;                                      \
        type sum = size % 2 ? -1 : 0;                           \
        type prd1 = (size + 1) / 2 % 2 ? -1 : 1;                \
        type prd2 = size / 2 % 2 ? -1 : 1;                      \
        DECL_MALLOC_INOUT_SOL(type);                            \
        SET_INDEX_TWO_VAL(io, val1, val2);                      \
        SET_INDEX_TWO_VAL(sol, sum, -sum);                      \
        ALLREDUCE_NOT_FREE_INPLACE(mpi_type, MPI_SUM, io, sol); \
        SET_INDEX_TWO_VAL(io, val1, val2);                      \
        SET_INDEX_TWO_VAL(sol, prd1, prd2);                     \
        ALLREDUCE_AND_FREE_INPLACE(mpi_type, MPI_PROD, io, sol); \
    }

#define test_1u(type, mpi_type)                                 \
    {                                                           \
        type val1 = rank;                                       \
        type val2 = size - rank;                                \
        type sum1 = (size - 1) * size / 2;                      \
        type sum2 = (size + 1) * size / 2;                      \
        type prd;                                               \
        DECL_MALLOC_INOUT_SOL(type);                            \
        SET_INDEX_TWO_VAL(io, val1, val2);                      \
        SET_INDEX_TWO_VAL(sol, sum1, sum2);                     \
        ALLREDUCE_NOT_FREE_INPLACE(mpi_type, MPI_SUM, io, sol); \
        val1 = rank < 5 ? rank + 1 : 1;                         \
        val2 = size - rank <= 5 ? size - rank : 1;              \
        if (size < 3) {prd = size;}                             \
        else if (size < 4) {prd = 6;}                           \
        else if (size < 5) {prd = 24;}                          \
        else {prd = 120;}                                       \
        SET_INDEX_TWO_VAL(io, val1, val2);                      \
        SET_INDEX_TWO_VAL(sol, prd, prd);                       \
        ALLREDUCE_AND_FREE_INPLACE(mpi_type, MPI_PROD, io, sol); \
    }

#define test_2(type, mpi_type)                                  \
    {                                                           \
        type val1 = (rank + 1) * 1.00001;                       \
        type val2 = -val1;                                      \
        type sum = (size + 1) * 1.00001 * size / 2;             \
        type prd1, prd2;                                        \
        int count1, count2;                                     \
        DECL_MALLOC_INOUT_SOL(type);                            \
        SET_INDEX_TWO_VAL(io, val1, val2);                      \
        SET_INDEX_TWO_VAL(sol, sum, -sum);                      \
        ALLREDUCE_NOT_FREE_INPLACE_FLOAT(mpi_type, MPI_SUM, io, sol); \
        val1 = rank % 2 ? 0.999 : 0.998;                        \
        val2 = rank % 2 ? -0.998 : -0.999;                      \
        count1 = (size + 1) / 2;                                \
        count2 = size / 2;                                      \
        prd1 = pow(0.998, count1) * pow(0.999, count2);         \
        prd2 = pow(-0.999, count1) * pow(-0.998, count2);       \
        SET_INDEX_TWO_VAL(io, val1, val2);                      \
        SET_INDEX_TWO_VAL(sol, prd1, prd2);                      \
        ALLREDUCE_AND_FREE_INPLACE_FLOAT(mpi_type, MPI_PROD, io, sol); \
    }

#define test_3(type, mpi_type)                                  \
    {                                                           \
        type val1 = rank + I;                                   \
        type val2 = 1 + rank * 2 * I;                           \
        type sum1 = (size - 1) * size / 2 + size * I;           \
        type sum2 = size + (size - 1) * size * I;               \
        DECL_MALLOC_INOUT_SOL(type);                            \
        SET_INDEX_TWO_VAL(io, val1, val2);                      \
        SET_INDEX_TWO_VAL(sol, sum1, sum2);                     \
        ALLREDUCE_AND_FREE_INPLACE(mpi_type, MPI_SUM, io, sol); \
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

    test_1(short, MPI_SHORT);
    test_1u(unsigned short, MPI_UNSIGNED_SHORT);
    test_1(int16_t, MPI_INT16_T);
    test_1u(uint16_t, MPI_UINT16_T);

    test_1(int, MPI_INT);
    test_1u(unsigned, MPI_UNSIGNED);
    test_1(int32_t, MPI_INT32_T);
    test_1u(uint32_t, MPI_UINT32_T);

    test_1(long, MPI_LONG);
    test_1u(unsigned long, MPI_UNSIGNED_LONG);
    test_1(int64_t, MPI_INT64_T);
    test_1u(uint64_t, MPI_UINT64_T);
    test_1(long long, MPI_LONG_LONG);
    test_1u(unsigned long long, MPI_UNSIGNED_LONG_LONG);

    test_2(double, MPI_DOUBLE);
    test_2(long double, MPI_LONG_DOUBLE);

    test_3(float complex, MPI_C_COMPLEX);
    test_3(double complex, MPI_C_DOUBLE_COMPLEX);
    test_3(long double complex, MPI_C_LONG_DOUBLE_COMPLEX);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        test_1(int, MPI_INT);
    }

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

