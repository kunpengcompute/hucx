/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * See file LICENSE for terms.
 */

#ifndef UCT_SDMA_IFACE_H
#define UCT_SDMA_IFACE_H

#include <ucs/type/spinlock.h>
#include <uct/base/uct_iface.h>
#include "sdma_md.h"

#define MAX_SDMA_PUT_SHORT_SIZE 0x2000
#define MAX_SDMA_AM_BCOPY_SIZE 256
#define SDMA_EP_LIST_SIZE 144*144
#define SDMA_BANDWIDTH 16911
#define MAX_SDMA_DEV_NUM 4
#define DIE_NUM_PER_SOCKET 2
#define SDMA_REQ_FIFO_SIZE 65536

typedef struct uct_sdma_req {
    sdma_sqe_task_t task;
    sdma_request_t request; 
    uct_completion_t *comp;
    int is_over;
    int result;
} uct_sdma_req_t;

typedef struct uct_sdma_req_queue {
    int head;                                /* 队列头 */
    int tail;                                /* 队列尾 */
    int size;                                /* 队列长度 */
    int timeout;                             /* 超时次数 */
    uct_sdma_req_t reqs[SDMA_REQ_FIFO_SIZE]; /* sdma任务列表，循环队列 */
} uct_sdma_req_queue_t;

typedef struct uct_sdma_iface_config {
    uct_iface_config_t super;
    size_t seg_size;            /* Maximal send size */
    double bw;                  /* BW for SDMA */
    int shared_mode;            /* share channels mode */
} uct_sdma_iface_config_t;

typedef struct uct_sdma_iface_addr {
    int pasid[MAX_SDMA_DEV_NUM];
    int devid;
    int iface_id; /* 传递iface编号 */
    key_t shmem_key;
} UCS_S_PACKED uct_sdma_iface_addr_t;

#define SHMEM_FIELD_SIZE 15
#define SHMEM_BUFF_SIZE 4096
typedef struct sdma_am_field {
    uint64_t header;
    char buff[SHMEM_BUFF_SIZE];
} sdma_am_field_t;

typedef struct sdma_am_desc {
    int owner_bit;
    int am_short_bit;
    uint8_t id;
    unsigned length;
    unsigned flags;
    sdma_am_field_t am_field;
} sdma_am_desc_t;

typedef struct sdma_shmem_field {
    sdma_am_desc_t am_desc[SHMEM_FIELD_SIZE];
} sdma_shmem_field_t;

typedef struct sdma_shmem_msg {
    int shmem_id;
    key_t shmem_key;
    void *shmem_base;
} sdma_shmem_msg_t;

typedef ucs_status_t (*progress_callback)(void *ep);
typedef struct uct_sdma_iface_ep_entity {
    void *ep;
    progress_callback ep_cb;
} uct_sdma_iface_ep_entity_t;

typedef struct uct_sdma_iface {
    uct_base_iface_t super;
    void *src_sdma_handle;
    int src_pasid[MAX_SDMA_DEV_NUM];
    int src_dev_idx;
    int chn_id;
    int pid; /* 进程pid */
    int cur_cpu; /* 通过cpu编号作为iface编号 */
    int iface_creat_id;
    ucs_spinlock_t lock;
    uct_sdma_req_queue_t req_q_iface;
    sdma_shmem_msg_t *shmem_msg;
    uct_sdma_md_t *sdma_md;
    size_t send_size; /* Maximum size for payload */
    struct {
        size_t seg_size;    /* Maximal send size */
        double bw;          /* BW for SDMA */
        int shared_mode;    /* share channels mode */
    } config;
} uct_sdma_iface_t;

int uct_sdma_iface_register_ep(uct_sdma_iface_t *iface, void *ep, progress_callback cb);
void uct_sdma_iface_unregister_ep(uct_sdma_iface_t *iface, void *ep);

#endif
