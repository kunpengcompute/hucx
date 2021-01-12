/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Barrier基本功能测试
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

int main(int argc, char *argv[])
{
    int rank, size;
    int delay;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    srand(rank);
    delay = rand() % 6;
    sleep(delay);

    printf("rank%d enter barrier\n", rank);
    MPI_Barrier(MPI_COMM_WORLD);
    printf("rank%d exit barrier\n", rank);

    MPI_Finalize();
    return 0;
}

