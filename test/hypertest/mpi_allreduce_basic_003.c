/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce预定义逻辑运算操作支持的所有数据类型测试
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 5, 10, 20, 25, 50, 100, 250, 600, 1000, 2000, 3000, 1000000};

#define land_test1(type, mpi_type)                              \
    const_test(type, mpi_type, MPI_LAND, (rank & 0x1), 0, 0)

#define land_test2(type, mpi_type)                      \
    const_test(type, mpi_type, MPI_LAND, 1, 1, 0)

#define lor_test1(type, mpi_type)                                       \
    const_test(type, mpi_type, MPI_LOR, (rank & 0x1), (size > 1), 0)

#define lor_test2(type, mpi_type)                       \
    const_test(type, mpi_type, MPI_LOR, 0, 0, 0)

#define lxor_test1(type, mpi_type)                                      \
    const_test(type, mpi_type, MPI_LXOR, (rank == 1), (size > 1), 0)

#define lxor_test2(type, mpi_type)                      \
    const_test(type, mpi_type, MPI_LXOR, 0, 0, 0)

#define lxor_test3(type, mpi_type)                              \
    const_test(type, mpi_type, MPI_LXOR, 1, (size & 0x1), 0)

#define logic_test(type, mpi_type)                              \
    do {                                                        \
        land_test1(type, mpi_type)                              \
        land_test2(type, mpi_type)                              \
        lor_test1(type, mpi_type)                               \
        lor_test2(type, mpi_type)                               \
        lxor_test1(type, mpi_type)                              \
        lxor_test2(type, mpi_type)                              \
        lxor_test3(type, mpi_type)                              \
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
    logic_test(int, MPI_INT);
    logic_test(long, MPI_LONG);
    logic_test(short, MPI_SHORT);
    logic_test(unsigned short, MPI_UNSIGNED_SHORT);
    logic_test(unsigned, MPI_UNSIGNED);
    logic_test(unsigned long, MPI_UNSIGNED_LONG);
    logic_test(unsigned char, MPI_UNSIGNED_CHAR);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        land_test1(int, MPI_INT);
    }

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

