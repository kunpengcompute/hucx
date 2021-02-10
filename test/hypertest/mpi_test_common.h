/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: HMPI测试用例集公共头文件和宏定义
 * Author: shizhibao
 * Create: 2021-01-09
 */

#ifndef MPI_TEST_COMMON_H

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <complex.h>
#include <string.h>


#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define DECL_MALLOC_IN_OUT_SOL(type)           \
    type *in, *org, *out, *sol;                \
    in  = (type *)calloc(count, sizeof(type)); \
    org = (type *)calloc(count, sizeof(type)); \
    out = (type *)calloc(count, sizeof(type)); \
    sol = (type *)calloc(count, sizeof(type));

#define DECL_MALLOC_INOUT_SOL(type)            \
    type *io, *sol;                            \
    io  = (type *)calloc(count, sizeof(type)); \
    sol = (type *)calloc(count, sizeof(type));

#define SET_INDEX_CONST(arr, val)               \
    {                                           \
        int i;                                  \
        for (i = 0; i < count; i++)             \
            arr[i] = val;                       \
    }

#define SET_INDEX_TWO_VAL(arr, val1, val2)      \
    {                                           \
        int i;                                  \
        for (i = 0; i < count; i += 2)          \
            arr[i] = val1;                      \
        for (i = 1; i < count; i += 2)          \
            arr[i] = val2;                      \
    }

#define SET_INDEX_SUM(arr, val)                 \
    {                                           \
        int i;                                  \
        for (i = 0; i < count; i++)             \
            arr[i] = i + val;                   \
    }

#define SET_INDEX_SUM_SIZE(arr, val)            \
    {                                           \
        int i;                                  \
        for (i = 0; i < count; i++)             \
            arr[i] = i * size + val;            \
    }

#define SET_INDEX_FACTOR(arr, val)              \
    {                                           \
        int i;                                  \
        for (i = 0; i < count; i++)             \
            arr[i] = i * (val);                 \
    }

#define SET_INDEX_POWER(arr, val)               \
    {                                           \
        int i, j;                               \
        for (i = 0; i < count; i++) {           \
            arr[i] = 1;                         \
            for (j = 0; j < (val); j++)         \
                arr[i] *= i;                    \
        }                                       \
    }

#define SET_INDEX_STRUCT_CONST(arr, val, el)    \
    {                                           \
        int i;                                  \
        for (i = 0; i < count; i++)             \
            arr[i].el = val;                    \
    }

#define SET_INDEX_STRUCT_SUM(arr, val, el)      \
    {                                           \
        int i;                                  \
        for (i = 0; i < count; i++)             \
            arr[i].el = i + (val);              \
    }

#define SET_INDEX_STRUCT_SUM_SIZE(arr, val, el) \
    {                                           \
        int i;                                  \
        for (i = 0; i < count; i++)             \
            arr[i].el = i * size + (val);       \
    }

#define SET_INDEX_ARRAY_CONST(arr, val, el, n)  \
    {                                           \
        int i, j;                               \
        for (i = 0; i < count; i++)             \
            for (j = 0; j < n; j++)             \
                arr[i].el[j] = (val);           \
    }

#define SET_INDEX_ARRAY_SUM(arr, val, el, n)    \
    {                                           \
        int i, j;                               \
        for (i = 0; i < count; i++)             \
            for (j = 0; j < n; j++)             \
                arr[i].el[j] = i + (val);       \
    }

#define SET_INDEX_ARRAY_SUM_SIZE(arr, val, el, n) \
    {                                           \
        int i, j;                               \
        for (i = 0; i < count; i++)             \
            for (j = 0; j < n; j++)             \
                arr[i].el[j] = i * size + (val); \
    }

#define SET_INDEX_ARRAY_STRUCT_SUM(arr, val1, val2, n) \
    {                                           \
        int i, j;                               \
        for (i = 0; i < count; i++)             \
            for (j = 0; j < n; j++) {           \
                arr[i].st[j].a = i + (val1);    \
                arr[i].st[j].b = i + (val2);    \
            }                                   \
    }

#define SET_INDEX_ARRAY_STRUCT_SUM_SIZE(arr, val1, val2, n) \
    {                                                   \
        int i, j;                                       \
        for (i = 0; i < count; i++)                     \
            for (j = 0; j < n; j++) {                   \
                arr[i].st[j].a = i * size + (val1);     \
                arr[i].st[j].b = i * size + (val2);     \
            }                                           \
    }

#define ALLREDUCE_AND_FREE(mpi_type, mpi_op, in, out, sol, org)         \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Allreduce(in, out, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(in); free(out); free(sol); free(org);                  \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (out[i] != sol[i] || in[i] != org[i]) {              \
                    free(in); free(out); free(sol); free(org);          \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
        free(in); free(out); free(sol); free(org);                      \
    }

#define ALLREDUCE_AND_FREE_FLOAT(mpi_type, mpi_op, in, out, sol, org)   \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Allreduce(in, out, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(in); free(out); free(sol); free(org);                  \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (fabs(out[i] - sol[i]) > 1e-6 || fabs(in[i] - org[i]) > 1e-6) { \
                    free(in); free(out); free(sol); free(org);          \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
        free(in); free(out); free(sol); free(org);                      \
    }

#define ALLREDUCE_NOT_FREE(mpi_type, mpi_op, in, out, sol, org)         \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Allreduce(in, out, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(in); free(out); free(sol); free(org);                  \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (out[i] != sol[i] || in[i] != org[i]) {              \
                    free(in); free(out); free(sol); free(org);          \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
    }

#define ALLREDUCE_NOT_FREE_FLOAT(mpi_type, mpi_op, in, out, sol, org)   \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Allreduce(in, out, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(in); free(out); free(sol); free(org);                  \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (fabs(out[i] - sol[i]) > 1e-6 || fabs(in[i] - org[i]) > 1e-6) { \
                    free(in); free(out); free(sol); free(org);          \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
    }

#define BCAST_NOT_FREE(mpi_type, root, io, sol)                         \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Bcast(io, count, mpi_type, root, MPI_COMM_WORLD);      \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (io[i] != sol[i]) {                                  \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
    }

#define BCAST_AND_FREE(mpi_type, root, io, sol)                         \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Bcast(io, count, mpi_type, root, MPI_COMM_WORLD);      \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (io[i] != sol[i]) {                                  \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define BCAST_AND_FREE_FLOAT(mpi_type, root, io, sol)                   \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Bcast(io, count, mpi_type, root, MPI_COMM_WORLD);      \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (fabs(io[i] - sol[i]) > 1e-6) {                      \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define STRUCT_BCAST_NOT_FREE(mpi_type, root, io, sol)                  \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Bcast(io, count, mpi_type, root, MPI_COMM_WORLD);      \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if ((io[i].a != sol[i].a) || (io[i].b != sol[i].b)) {   \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
    }

#define STRUCT_BCAST_AND_FREE(mpi_type, root, io, sol)                  \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Bcast(io, count, mpi_type, root, MPI_COMM_WORLD);      \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if ((io[i].a != sol[i].a) || (io[i].b != sol[i].b)) {   \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define STRUCT_BCAST_AND_FREE_FLOAT(mpi_type, root, io, sol)            \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Bcast(io, count, mpi_type, root, MPI_COMM_WORLD);      \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (fabs(io[i].a - sol[i].a) > 1e-6 || io[i].b != sol[i].b) { \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define ARRAY_BCAST_AND_FREE(mpi_type, root, io, sol, n)                \
    {                                                                   \
        int i, j, rc;                                                   \
        rc = MPI_Bcast(io, count, mpi_type, root, MPI_COMM_WORLD);      \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                for (j = 0; j < n; j++) {                               \
                    if (io[i].a[j] != sol[i].a[j]) {                    \
                        free(io); free(sol);                            \
                        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);        \
                    }                                                   \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define ARRAY_STRUCT_BCAST_AND_FREE(mpi_type, root, io, sol, n)         \
    {                                                                   \
        int i, j, rc;                                                   \
        rc = MPI_Bcast(io, count, mpi_type, root, MPI_COMM_WORLD);      \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                for (j = 0; j < n; j++) {                               \
                    if (io[i].st[j].a != sol[i].st[j].a ||              \
                        io[i].st[j].b != sol[i].st[j].b) {              \
                        free(io); free(sol);                            \
                        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);        \
                    }                                                   \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define ALLREDUCE_AND_FREE_INPLACE(mpi_type, mpi_op, io, sol)           \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Allreduce(MPI_IN_PLACE, io, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (io[i] != sol[i]) {                                  \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define ALLREDUCE_AND_FREE_INPLACE_FLOAT(mpi_type, mpi_op, io, sol)     \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Allreduce(MPI_IN_PLACE, io, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (fabs(io[i] - sol[i]) > 1e-6) {                      \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define ALLREDUCE_NOT_FREE_INPLACE(mpi_type, mpi_op, io, sol)           \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Allreduce(MPI_IN_PLACE, io, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (io[i] != sol[i]) {                                  \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
    }

#define ALLREDUCE_NOT_FREE_INPLACE_FLOAT(mpi_type, mpi_op, io, sol)     \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Allreduce(MPI_IN_PLACE, io, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if (fabs(io[i] - sol[i]) > 1e-6) {                      \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
    }

#define STRUCT_ALLREDUCE_NOT_FREE_INPLACE(mpi_type, mpi_op, io, sol)    \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Allreduce(MPI_IN_PLACE, io, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if ((io[i].a != sol[i].a) || (io[i].b != sol[i].b)) {   \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
    }

#define STRUCT_ALLREDUCE_AND_FREE_INPLACE(mpi_type, mpi_op, io, sol)    \
    {                                                                   \
        int i, rc;                                                      \
        rc = MPI_Allreduce(MPI_IN_PLACE, io, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                if ((io[i].a != sol[i].a) || (io[i].b != sol[i].b)) {   \
                    free(io); free(sol);                                \
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define ARRAY_ALLREDUCE_AND_FREE_INPLACE(mpi_type, mpi_op, io, sol, n)  \
    {                                                                   \
        int i, j, rc;                                                   \
        rc = MPI_Allreduce(MPI_IN_PLACE, io, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                for (j = 0; j < n; j++) {                               \
                    if (io[i].a[j] != sol[i].a[j]) {                    \
                        free(io); free(sol);                            \
                        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);        \
                    }                                                   \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define ARRAY_STRUCT_ALLREDUCE_AND_FREE_INPLACE(mpi_type, mpi_op, io, sol, n) \
    {                                                                   \
        int i, j, rc;                                                   \
        rc = MPI_Allreduce(MPI_IN_PLACE, io, count, mpi_type, mpi_op, MPI_COMM_WORLD); \
        if (rc) {                                                       \
            free(io); free(sol);                                        \
            MPI_Abort(MPI_COMM_WORLD, rc);                              \
        } else {                                                        \
            for (i = 0; i < count; i++) {                               \
                for (j = 0; j < n; j++) {                               \
                    if (io[i].st[j].a != sol[i].st[j].a ||              \
                        io[i].st[j].b != sol[i].st[j].b) {              \
                        free(io); free(sol);                            \
                        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);        \
                    }                                                   \
                }                                                       \
            }                                                           \
        }                                                               \
        free(io); free(sol);                                            \
    }

#define const_test(type, mpi_type, mpi_op, val1, val2, val3)    \
    {                                                           \
        DECL_MALLOC_IN_OUT_SOL(type);                           \
        SET_INDEX_CONST(in, (val1));                            \
        SET_INDEX_CONST(org, (val1));                           \
        SET_INDEX_CONST(sol, (val2));                           \
        SET_INDEX_CONST(out, (val3));                           \
        ALLREDUCE_AND_FREE(mpi_type, mpi_op, in, out, sol, org);\
    }

#define WAIT_ALL_SUCCESS()                      \
    MPI_Barrier(MPI_COMM_WORLD);                \
    if (rank == 0)                              \
        printf("%s\b\b success\n", __FILE__)

#endif // !MPI_TEST_COMMON_H

