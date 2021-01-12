/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce不连续自定义数据类型测试，单个数据大小覆盖0、
 *              am_short&bcopy_one临界值左右、超大几种场景
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 100};

#define sum_arr_long_int_test(type, mpi_type, mpi_op, n)                \
    {                                                                   \
        int ranks_sum = size * (size - 1) / 2;                          \
        int ranks_sum_100 = size * 100 + ranks_sum;                     \
        DECL_MALLOC_INOUT_SOL(type);                                    \
        SET_INDEX_ARRAY_STRUCT_SUM(io, rank + 100, rank, n);            \
        SET_INDEX_ARRAY_STRUCT_SUM_SIZE(sol, ranks_sum_100, ranks_sum, n);     \
        ARRAY_STRUCT_ALLREDUCE_AND_FREE_INPLACE(mpi_type, mpi_op, io, sol, n); \
    }

struct long_int {
    long a;
    int b;
};

#define declare_arr_long_int_struct_and_op(n)                           \
struct arr_long_int##n {                                                \
    struct long_int st[n];                                              \
};                                                                      \
void sum_long_int##n(void *inP, void *inoutP, int *len, MPI_Datatype *dptr) \
{                                                                       \
    struct arr_long_int##n *in = (struct arr_long_int##n *)inP;         \
    struct arr_long_int##n *io = (struct arr_long_int##n *)inoutP;      \
    int i, j;                                                           \
    for (i = 0; i < *len; i++) {                                        \
        for (j = 0; j < n; j++) {                                       \
            io->st[j].a += in->st[j].a;                                 \
            io->st[j].b += in->st[j].b;                                 \
        }                                                               \
        in++; io++;                                                     \
    }                                                                   \
}

declare_arr_long_int_struct_and_op(0);
declare_arr_long_int_struct_and_op(3);
declare_arr_long_int_struct_and_op(15);
declare_arr_long_int_struct_and_op(100);
declare_arr_long_int_struct_and_op(800);
declare_arr_long_int_struct_and_op(100000);

#define mpi_dt_and_op_create(n)                             \
    MPI_Op op_sum_long_int##n;                              \
    MPI_Op_create(sum_long_int##n, 1, &op_sum_long_int##n); \
    MPI_Datatype dt_long_int##n;                            \
    MPI_Type_contiguous(n, MPI_LONG_INT, &dt_long_int##n);  \
    MPI_Type_commit(&dt_long_int##n)

#define mpi_dt_and_op_free(n)                               \
    MPI_Op_free(&op_sum_long_int##n);                       \
    MPI_Type_free(&dt_long_int##n)

int main(int argc, char *argv[])
{
    int rank, size;
    int i, count;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    mpi_dt_and_op_create(0);
    mpi_dt_and_op_create(3);
    mpi_dt_and_op_create(15);
    mpi_dt_and_op_create(100);
    mpi_dt_and_op_create(800);
    mpi_dt_and_op_create(100000);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        sum_arr_long_int_test(struct arr_long_int0, dt_long_int0, op_sum_long_int0, 0);
        sum_arr_long_int_test(struct arr_long_int3, dt_long_int3, op_sum_long_int3, 3);
        sum_arr_long_int_test(struct arr_long_int15, dt_long_int15, op_sum_long_int15, 15);
        sum_arr_long_int_test(struct arr_long_int100, dt_long_int100, op_sum_long_int100, 100);
        sum_arr_long_int_test(struct arr_long_int800, dt_long_int800, op_sum_long_int800, 800);
        sum_arr_long_int_test(struct arr_long_int100000, dt_long_int100000, op_sum_long_int100000, 100000);
    }

    mpi_dt_and_op_free(0);
    mpi_dt_and_op_free(3);
    mpi_dt_and_op_free(15);
    mpi_dt_and_op_free(100);
    mpi_dt_and_op_free(800);
    mpi_dt_and_op_free(100000);

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

