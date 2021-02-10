/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce规约顺序测试，矩阵相乘（满足结合律不满足交换律）
 *              覆盖连续、不连续数据类型
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 10, 100, 1000};

struct int_int {
    int c0;
    int c1;
};

struct long_int {
    int c0;
    int dummy0;
    int c1;
    int dummy1;
};

struct matrix1 {
    struct int_int r0;
    struct int_int r1;
};

struct matrix2 {
    struct long_int r0;
    struct long_int r1;
};

void prod_matrix1(void *inP, void *inoutP, int *len, MPI_Datatype *dptr)
{
    struct matrix1 *in = (struct matrix1 *)inP;
    struct matrix1 *io = (struct matrix1 *)inoutP;
    struct matrix1 tmp;
    int i;
    for (i = 0; i < *len; i++) {
        tmp = *io;
        io->r0.c0 = in->r0.c0 * tmp.r0.c0 + in->r0.c1 * tmp.r1.c0;
        io->r0.c1 = in->r0.c0 * tmp.r0.c1 + in->r0.c1 * tmp.r1.c1;
        io->r1.c0 = in->r1.c0 * tmp.r0.c0 + in->r1.c1 * tmp.r1.c0;
        io->r1.c1 = in->r1.c0 * tmp.r0.c1 + in->r1.c1 * tmp.r1.c1;
        in++; io++;
    }
}

void prod_matrix2(void *inP, void *inoutP, int *len, MPI_Datatype *dptr)
{
    struct matrix2 *in = (struct matrix2 *)inP;
    struct matrix2 *io = (struct matrix2 *)inoutP;
    struct matrix2 tmp;
    int i;
    for (i = 0; i < *len; i++) {
        tmp = *io;
        io->r0.c0 = in->r0.c0 * tmp.r0.c0 + in->r0.c1 * tmp.r1.c0;
        io->r0.c1 = in->r0.c0 * tmp.r0.c1 + in->r0.c1 * tmp.r1.c1;
        io->r1.c0 = in->r1.c0 * tmp.r0.c0 + in->r1.c1 * tmp.r1.c0;
        io->r1.c1 = in->r1.c0 * tmp.r0.c1 + in->r1.c1 * tmp.r1.c1;
        in++; io++;
    }
}

#define mpi_dt_and_op_create1()                         \
    MPI_Op op_prod_matrix1;                             \
    MPI_Op_create(prod_matrix1, 0, &op_prod_matrix1);   \
    MPI_Datatype dt_matrix1;                            \
    MPI_Type_contiguous(2, MPI_2INT, &dt_matrix1);      \
    MPI_Type_commit(&dt_matrix1)

#define mpi_dt_and_op_free1()                           \
    MPI_Op_free(&op_prod_matrix1);                      \
    MPI_Type_free(&dt_matrix1)

#define mpi_dt_and_op_create2()                         \
    MPI_Op op_prod_matrix2;                             \
    MPI_Op_create(prod_matrix2, 0, &op_prod_matrix2);   \
    MPI_Datatype dt_matrix2;                            \
    MPI_Type_contiguous(2, MPI_LONG_INT, &dt_matrix2);  \
    MPI_Type_commit(&dt_matrix2)

#define mpi_dt_and_op_free2()                           \
    MPI_Op_free(&op_prod_matrix2);                      \
    MPI_Type_free(&dt_matrix2)

static void test1(int rank, int size, int count)
{
    int i, rc, len = 1;
    struct matrix1 tmp, ans;

    DECL_MALLOC_INOUT_SOL(struct matrix1);
    for (i = 0; i < count; i++) {
        io[i].r0.c0 = rank;
        io[i].r0.c1 = rank + 1;
        io[i].r1.c0 = rank + 2;
        io[i].r1.c1 = rank + 3;
    }

    ans.r0.c0 = size - 1;
    ans.r0.c1 = size;
    ans.r1.c0 = size + 1;
    ans.r1.c1 = size + 2;
    tmp = ans;
    for (i = size - 2; i >= 0; i--) {
        tmp.r0.c0--;
        tmp.r0.c1--;
        tmp.r1.c0--;
        tmp.r1.c1--;
        prod_matrix1(&tmp, &ans, &len, NULL);
    }

    for (i = 0; i < count; i++) {
        sol[i] = ans;
    }

    mpi_dt_and_op_create1();
    rc = MPI_Allreduce(MPI_IN_PLACE, io, count, dt_matrix1, op_prod_matrix1, MPI_COMM_WORLD);
    mpi_dt_and_op_free1();

    if (rc) {
        free(io);
        free(sol);
        MPI_Abort(MPI_COMM_WORLD, rc);
    } else {
        for (i = 0; i < count; i++) {
            if (io[i].r0.c0 != sol[i].r0.c0 ||
                io[i].r0.c1 != sol[i].r0.c1 ||
                io[i].r1.c0 != sol[i].r1.c0 ||
                io[i].r1.c1 != sol[i].r1.c1) {
                free(io); free(sol);
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }
        }
    }

    free(io);
    free(sol);
}

static void test2(int rank, int size, int count)
{
    int i, rc, len = 1;
    struct matrix2 tmp, ans;

    DECL_MALLOC_INOUT_SOL(struct matrix2);
    for (i = 0; i < count; i++) {
        io[i].r0.c0 = rank;
        io[i].r0.c1 = rank + 1;
        io[i].r1.c0 = rank + 2;
        io[i].r1.c1 = rank + 3;
    }

    ans.r0.c0 = size - 1;
    ans.r0.c1 = size;
    ans.r1.c0 = size + 1;
    ans.r1.c1 = size + 2;
    tmp = ans;
    for (i = size - 2; i >= 0; i--) {
        tmp.r0.c0--;
        tmp.r0.c1--;
        tmp.r1.c0--;
        tmp.r1.c1--;
        prod_matrix2(&tmp, &ans, &len, NULL);
    }

    for (i = 0; i < count; i++) {
        sol[i] = ans;
    }

    mpi_dt_and_op_create2();
    rc = MPI_Allreduce(MPI_IN_PLACE, io, count, dt_matrix2, op_prod_matrix2, MPI_COMM_WORLD);
    mpi_dt_and_op_free2();

    if (rc) {
        free(io);
        free(sol);
        MPI_Abort(MPI_COMM_WORLD, rc);
    } else {
        for (i = 0; i < count; i++) {
            if (io[i].r0.c0 != sol[i].r0.c0 ||
                io[i].r0.c1 != sol[i].r0.c1 ||
                io[i].r1.c0 != sol[i].r1.c0 ||
                io[i].r1.c1 != sol[i].r1.c1) {
                free(io); free(sol);
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }
        }
    }
    free(io);
    free(sol);
}

int main(int argc, char *argv[])
{
    int rank, size;
    int i, count;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        test1(rank, size, count);
        test2(rank, size, count);
    }

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

