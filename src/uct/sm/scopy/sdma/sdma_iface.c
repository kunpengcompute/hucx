/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 * See file LICENSE for terms.
 */
#define _GNU_SOURCE

#include <ucs/sys/string.h>
#include <uct/sm/base/sm_iface.h>
#include <sched.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sdma_md.h"
#include "sdma_iface.h"
#include "sdma_ep.h"

#define EP_PROGRESS_TIMEOUT 1000

static ucs_config_field_t uct_sdma_iface_config_table[] = {
    {"SDMA_", "", NULL,
     ucs_offsetof(uct_sdma_iface_config_t, super),
     UCS_CONFIG_TYPE_TABLE(uct_iface_config_table)},

    {"SEG_SIZE", "8k", "Size of copy-out buffer",
     ucs_offsetof(uct_sdma_iface_config_t, seg_size),
     UCS_CONFIG_TYPE_MEMUNITS},

    /*
     * Each sdma channel supports about 300Gb, so we set both shared bw and dedicate bw;
     * shared bw for load balancing;
     * dedicate bw for comparing with cma
     */
    {"BW", "10000MBs", "BW of SDMA",
     ucs_offsetof(uct_sdma_iface_config_t, bw),
     UCS_CONFIG_TYPE_BW},

    {"SHARED_CHANNEL_MODE", "y", "SDMA use shared channels, n means to use exclusive channels",
     ucs_offsetof(uct_sdma_iface_config_t, shared_mode),
     UCS_CONFIG_TYPE_BOOL},

    {NULL}
};

ucs_status_t uct_sdma_iface_progress_exec(uct_sdma_iface_t *iface)
{
    uct_sdma_req_queue_t *sq = &(iface->req_q_iface);
    int i;
    int flight = 0; /* 未结束的任务 */
    int result = 0; /* 任务执行结果 */
    int ret; 

    /* 处理下已完成的sdma任务 */
    ucs_spin_lock(&iface->lock);

    if (!iface->config.shared_mode) {
        sdma_progress(iface->src_sdma_handle);
    }

    /* 检查sdma任务执行结果 */
    i = sq->head;
    while (i != sq->tail) {
        /**
         * 如果当前任务还未结束，则认为后续的任务都没有结束。
         * 约束：sdma任务是按顺序执行完毕的
         */
        if (iface->config.shared_mode) {
            /*
             * sdma_iquery_chn is a non-block shared-chn interface, SDMA_RNDCNT_ERR means in_progress
             * and sdma_iquery_chn dont call task_cb.
             */
            ret = sdma_iquery_chn(iface->src_sdma_handle, &sq->reqs[i].request);
            if (ret != SDMA_SUCCESS) {
                if (ret == SDMA_RNDCNT_ERR) {
                    sq->timeout++;
                    flight = 1;
                    break;
                } else {
                    ucs_fatal("sdma_iface:chn_id[%d] src_dev_idx[%d] cur_cpu[%d] status = %d, error_code = %d", iface->chn_id,
                              iface->src_dev_idx, iface->cur_cpu, result, ret);
                }
            }
            sq->reqs[i].result = ret;
        } else if (sq->reqs[i].is_over != 1) {
            sq->timeout++;
            flight = 1;
            break;
        }

        /* 保存当前任务执行结果 */
        if (sq->reqs[i].result != 0) {
            result = sq->reqs[i].result;
            /* 打印错误信息 */
            ucs_fatal("sdma_iface:chn_id[%d] src_dev_idx[%d] cur_cpu[%d] status = %d", iface->chn_id,
                iface->src_dev_idx, iface->cur_cpu, result);
        }

        /**
         * 部分操作(例如: put_zcopy\get_bcopy)携带有completion参数（完成信号）。
         * 当任务执行完毕后，需要激活completion。
         */
        if (sq->reqs[i].comp != NULL) {
            uct_invoke_completion(sq->reqs[i].comp, sq->reqs[i].result);
        }

        /* 找出循环队列下1个位置 */
        i++;
        if (i >= sq->size) {
            i = 0;
        }

        /* 更新head指针 */
        sq->head = i;
    }

    /* 任务执行完毕 */
    if (flight == 0) {
        sq->timeout = 0;
        ucs_spin_unlock(&iface->lock);
        return UCS_OK;
    }

    if (sq->timeout > EP_PROGRESS_TIMEOUT) {
        sq->timeout = 0;
        ucs_debug("sdma_iface:chn_id[%d] src_dev_idx[%d] cur_cpu[%d] requests already query %d times", iface->chn_id,
            iface->src_dev_idx, iface->cur_cpu, EP_PROGRESS_TIMEOUT);
    }

    ucs_spin_unlock(&iface->lock);
    return UCS_INPROGRESS;
}

static ucs_status_t uct_sdma_iface_query(uct_iface_h tl_iface, uct_iface_attr_t *attr)
{
    uct_sdma_iface_t *iface = ucs_derived_of(tl_iface, uct_sdma_iface_t);

    uct_base_iface_query(&iface->super, attr);

    attr->iface_addr_len = sizeof(uct_sdma_iface_addr_t);
    attr->device_addr_len = uct_sm_iface_get_device_addr_len();
    attr->ep_addr_len = 0;
    attr->max_conn_priv = 0;
    attr->cap.flags = UCT_IFACE_FLAG_CONNECT_TO_IFACE |
                      UCT_IFACE_FLAG_PUT_ZCOPY |
                      UCT_IFACE_FLAG_GET_ZCOPY |
                      UCT_IFACE_FLAG_GET_BCOPY |
                      UCT_IFACE_FLAG_PUT_SHORT |
                      UCT_IFACE_FLAG_ATOMIC_CPU |
                      UCT_IFACE_FLAG_PENDING |
                      UCT_IFACE_FLAG_CB_SYNC |
                      UCT_IFACE_FLAG_PUT_BCOPY |
                      UCT_IFACE_FLAG_EP_CHECK;

    attr->cap.put.max_short = MAX_SDMA_PUT_SHORT_SIZE;
    attr->cap.put.max_bcopy = SIZE_MAX;
    attr->cap.put.min_zcopy = 0;
    attr->cap.put.max_zcopy = SIZE_MAX;
    attr->cap.put.opt_zcopy_align = 1;
    /* UCX在rma_basic.c中固定写死iov=1，因此SDMA也只支持1个iov */
    attr->cap.put.max_iov = 1;

    attr->cap.get.max_bcopy = SIZE_MAX;
    attr->cap.get.min_zcopy = 0;
    attr->cap.get.max_zcopy = SIZE_MAX;
    attr->cap.get.opt_zcopy_align = 1;
    attr->cap.get.max_iov = 1;

    attr->cap.am.max_short = iface->send_size;
    attr->cap.am.max_bcopy = MAX_SDMA_AM_BCOPY_SIZE;

    attr->cap.am.min_zcopy = 0;
    attr->cap.am.max_zcopy = 0;
    attr->cap.am.opt_zcopy_align = 1;
    attr->cap.am.max_hdr = 0;
    attr->cap.am.max_iov = SIZE_MAX;

    attr->latency = ucs_linear_func_make(0, 0);
    attr->bandwidth.dedicated = 0;
    attr->bandwidth.shared = iface->config.bw;
    attr->overhead = 10e-9;
    attr->priority = 1;
    return UCS_OK;
}

static ucs_status_t uct_sdma_iface_get_address(uct_iface_h tl_iface, uct_iface_addr_t *addr)
{
    const uct_sdma_iface_t *iface = ucs_derived_of(tl_iface, uct_sdma_iface_t);
    uct_sdma_iface_addr_t *iface_addr = (uct_sdma_iface_addr_t *)addr;
    int i;

    for (i = 0; i < iface->sdma_md->num_devices; i++) {
        iface_addr->pasid[i] = iface->src_pasid[i];
    }

    iface_addr->devid = iface->src_dev_idx;
    iface_addr->iface_id = iface->cur_cpu;
    iface_addr->shmem_key = iface->shmem_msg->shmem_key;
    return UCS_OK;
}

static unsigned uct_sdma_iface_progress(uct_iface_h tl_iface)
{
    uct_sdma_iface_t *iface = ucs_derived_of(tl_iface, uct_sdma_iface_t);
    sdma_shmem_field_t *sdma_shmem_field;
    ucs_status_t status = UCS_OK;
    ucs_status_t progress;
    int am_short_bit;
    unsigned length;
    uint8_t id;
    void *header;

    sdma_shmem_field = (sdma_shmem_field_t *)iface->shmem_msg->shmem_base;
    for (int k = 0; k < SHMEM_FIELD_SIZE; k++) {
        if (sdma_shmem_field->am_desc[k].owner_bit) {
            id = sdma_shmem_field->am_desc[k].id;
            length = sdma_shmem_field->am_desc[k].length;
            header = &sdma_shmem_field->am_desc[k].am_field;
            am_short_bit = sdma_shmem_field->am_desc[k].am_short_bit;

            if (am_short_bit) {
                status = uct_iface_invoke_am(&iface->super, id, header,
                                             length + sizeof(sdma_shmem_field->am_desc[k].am_field.header), 0);
            } else {
                status = uct_iface_invoke_am(&iface->super, id, (void *)sdma_shmem_field->am_desc[k].am_field.buff,
                                             length, UCT_CB_PARAM_FLAG_DESC);
            }

            memset(&sdma_shmem_field->am_desc[k], 0x0, sizeof(sdma_am_desc_t));
        }
    }

    progress = uct_sdma_iface_progress_exec(iface);
    if (progress != UCS_OK) {
        status = progress;
    }

    return status;
}

static UCS_CLASS_DECLARE_DELETE_FUNC(uct_sdma_iface_t, uct_iface_t);

static uct_iface_ops_t uct_sdma_iface_ops = {
    .ep_put_short = uct_sdma_ep_put_short,
    .ep_put_bcopy = uct_sdma_ep_put_bcopy,
    .ep_put_zcopy = uct_sdma_ep_put_zcopy,
    .ep_get_zcopy = uct_sdma_ep_get_zcopy,
    .ep_get_bcopy = uct_sdma_ep_get_bcopy,
    .ep_am_short = uct_sdma_ep_am_short,
    .ep_am_bcopy = uct_sdma_ep_am_bcopy,
    .ep_pending_add = (uct_ep_pending_add_func_t)ucs_empty_function_return_busy,
    .ep_pending_purge = (uct_ep_pending_purge_func_t)ucs_empty_function,
    .ep_flush = uct_sdma_ep_flush,
    .ep_fence = uct_base_ep_fence,
    .ep_check = (uct_ep_check_func_t)ucs_empty_function_return_success,
    .ep_create = UCS_CLASS_NEW_FUNC_NAME(uct_sdma_ep_t),
    .ep_destroy = UCS_CLASS_DELETE_FUNC_NAME(uct_sdma_ep_t),
    .iface_flush = uct_base_iface_flush,
    .iface_fence = uct_base_iface_fence,
    .iface_progress_enable = uct_base_iface_progress_enable,
    .iface_progress_disable = uct_base_iface_progress_disable,
    .iface_progress = uct_sdma_iface_progress,
    .iface_close = UCS_CLASS_DELETE_FUNC_NAME(uct_sdma_iface_t),
    .iface_query = uct_sdma_iface_query,
    .iface_get_device_address = uct_sm_iface_get_device_address,
    .iface_get_address = uct_sdma_iface_get_address,
    .iface_is_reachable = uct_sm_iface_is_reachable
};

static ucs_status_t uct_sdma_query_tl_devices(uct_md_h md, uct_tl_device_resource_t **tl_devices_p,
    unsigned *num_tl_devices_p)
{
    return uct_single_device_resource(md, "memory", UCT_DEVICE_TYPE_SHM, UCS_SYS_DEVICE_ID_UNKNOWN, tl_devices_p,
        num_tl_devices_p);
}

int iface_creat_id = 0;
static UCS_CLASS_INIT_FUNC(uct_sdma_iface_t, uct_md_h md, uct_worker_h worker, const uct_iface_params_t *params,
    const uct_iface_config_t *tl_config)
{
    sdma_shmem_msg_t *shmem_msg;
    ucs_status_t status;
    int pasid;
    uct_sdma_iface_config_t *config = ucs_derived_of(tl_config, uct_sdma_iface_config_t);

    UCS_CLASS_CALL_SUPER_INIT(uct_base_iface_t, &uct_sdma_iface_ops, &uct_base_iface_internal_ops, md, worker, params,
        tl_config UCS_STATS_ARG((params->field_mask & UCT_IFACE_PARAM_FIELD_STATS_ROOT) ? params->stats_root : NULL)
        UCS_STATS_ARG(params->mode.device.dev_name));

    self->sdma_md = (uct_sdma_md_t *)md;
    self->config.shared_mode = config->shared_mode;
    if (NULL == self->sdma_md) {
        ucs_error("Failed to allocate memory for uct_sdma_md_t");
        return UCS_ERR_NO_MEMORY;
    }

    self->pid = (int)getpid();
    self->cur_cpu = sched_getcpu();
    self->chn_id = self->cur_cpu;
    self->src_dev_idx = sdma_nearest_id();
    if (self->src_dev_idx < 0) {
        ucs_error("Failed to get nearest sdma id");
        return UCS_ERR_IO_ERROR;
    }

    self->iface_creat_id = iface_creat_id;
    self->config.bw = config->bw;

    shmem_msg = (sdma_shmem_msg_t *)calloc(1, sizeof(sdma_shmem_msg_t));
    status = uct_creat_shmem(SHMEM_KEY_GET(self->pid, iface_creat_id), shmem_msg);
    iface_creat_id++;
    if (status) {
        ucs_error("create shmem failed, err = %d", status);
        return status;
    }
    self->shmem_msg = shmem_msg;
    ucs_assert(self->src_dev_idx < self->sdma_md->max_num_devices);

    for (int i = 0; i < self->sdma_md->num_devices; i++) {
        pasid = -1;
        status = (ucs_status_t)sdma_get_process_id(self->sdma_md->sdma_fd[i], &pasid);
        if (status) {
            ucs_error("failed to create sdma device[%d]:pasid %d", i, status);
            return UCS_ERR_IO_ERROR;
        }
        self->src_pasid[i] = pasid;
        ucs_info("self->src_pasid[%d] = %u", i, self->src_pasid[i]);
    }

    if (self->config.shared_mode) {
        self->src_sdma_handle = sdma_init_chn(self->sdma_md->sdma_fd[self->src_dev_idx], self->chn_id);
    } else {
        self->src_sdma_handle = sdma_alloc_chn(self->sdma_md->sdma_fd[self->src_dev_idx]);
    }

    if (self->src_sdma_handle == NULL) {
        ucs_error("Failed to create sdma_device[%d] handle", self->src_dev_idx);
        uct_shmem_del(self->shmem_msg);
        free(shmem_msg);
        self->shmem_msg = NULL;
        return UCS_ERR_IO_ERROR;
    }
    self->send_size = MAX_SDMA_PUT_SHORT_SIZE;

    self->req_q_iface.head = 0;
    self->req_q_iface.tail = 0;
    self->req_q_iface.size = SDMA_REQ_FIFO_SIZE;
    self->req_q_iface.timeout = 0;
    status = ucs_spinlock_init(&self->lock, 0);
    if (status != UCS_OK) {
        ucs_error("lock init failed, err = %d", status);
        return status;
    }
    ucs_info("sdma: iface[%d:%d] init", self->pid, self->cur_cpu);

    return UCS_OK;
}

static UCS_CLASS_CLEANUP_FUNC(uct_sdma_iface_t)
{
    ucs_status_t ret;
    uct_base_iface_progress_disable(&self->super.super, UCT_PROGRESS_SEND | UCT_PROGRESS_RECV);
    if (self->src_sdma_handle != NULL) {
        if (self->config.shared_mode) {
            sdma_deinit_chn(self->src_sdma_handle);
        } else {
            sdma_free_chn(self->src_sdma_handle);
        }
    }

    ret = uct_shmem_del(self->shmem_msg);
    if (ret) {
        ucs_error("shmem_del failed");
    }
    free(self->shmem_msg);
    self->shmem_msg = NULL;

    ucs_info("sdma: iface[%d:%d] cleanup.", self->pid, self->cur_cpu);
}

UCS_CLASS_DEFINE(uct_sdma_iface_t, uct_base_iface_t);

static UCS_CLASS_DEFINE_NEW_FUNC(uct_sdma_iface_t, uct_iface_t, uct_md_h, uct_worker_h, const uct_iface_params_t *,
    const uct_iface_config_t *);

static UCS_CLASS_DEFINE_DELETE_FUNC(uct_sdma_iface_t, uct_iface_t);

UCT_TL_DEFINE(&uct_sdma_component, sdma, uct_sdma_query_tl_devices, uct_sdma_iface_t, "SDMA_",
    uct_sdma_iface_config_table, uct_sdma_iface_config_t);
