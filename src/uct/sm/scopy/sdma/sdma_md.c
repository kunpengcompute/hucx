/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#include "sdma_md.h"

#include <ucs/arch/cpu.h>
#include <ucs/debug/log.h>
#include <ucs/debug/memtrack.h>
#include <ucs/sys/sys.h>
#include <ucm/api/ucm.h>

ucs_config_field_t uct_sdma_md_config_table[] = {

    {"", "", NULL, ucs_offsetof(uct_sdma_md_config_t, super), UCS_CONFIG_TYPE_TABLE(uct_md_config_table)},

    {"DEV_NAME_LEN", "20", "Size of sdma device's name", ucs_offsetof(uct_sdma_md_config_t, dev_name_len),
        UCS_CONFIG_TYPE_INT},

    {"DEV_MAXNUM", "4", "Sdma device's number", ucs_offsetof(uct_sdma_md_config_t, dev_num),
        UCS_CONFIG_TYPE_INT},

    {NULL}

};

void uct_sdma_md_close(uct_md_h md)
{
    uct_sdma_md_t *sdma_md = ucs_derived_of(md, uct_sdma_md_t);
    for (int i = 0; i < sdma_md->num_devices; i++) {
        close(sdma_md->sdma_fd[i]);
    }
    ucs_free(sdma_md->sdma_fd);
    return;
}

ucs_status_t uct_sdma_md_query(uct_md_h uct_md, uct_md_attr_v2_t *md_attr)
{
    md_attr->flags                  = UCT_MD_FLAG_REG | UCT_MD_FLAG_NEED_RKEY | UCT_MD_FLAG_ALLOC | UCT_MD_FLAG_FIXED | UCT_MD_FLAG_NEED_MEMH;
    md_attr->reg_mem_types          = UCS_BIT(UCS_MEMORY_TYPE_HOST);
    md_attr->reg_nonblock_mem_types = 0;
    md_attr->cache_mem_types        = 0;
    md_attr->alloc_mem_types        = UCS_BIT(UCS_MEMORY_TYPE_HOST);
    md_attr->access_mem_types       = UCS_BIT(UCS_MEMORY_TYPE_HOST);
    md_attr->detect_mem_types       = 0;
    md_attr->dmabuf_mem_types       = 0;
    md_attr->max_alloc              = ULONG_MAX;
    md_attr->max_reg                = ULONG_MAX;
    md_attr->rkey_packed_size       = 0;
    md_attr->reg_cost               = ucs_linear_func_make(0, 0);
    memset(&md_attr->local_cpus, 0xff, sizeof(md_attr->local_cpus));
    ucs_trace("uct_sdma_md_query OK!");
    return UCS_OK;
}

ucs_status_t uct_sdma_mem_reg(uct_md_h md, void *address, size_t length,
                              const uct_md_mem_reg_params_t *params, uct_mem_h *memh_p)
{
    uct_sdma_md_t *sdma_md = ucs_derived_of(md, uct_sdma_md_t);
    ucs_status_t status = UCS_OK;
    uct_sdma_key_t *sdma_memh;
    sdma_memh = ucs_calloc(1, sizeof(uct_sdma_key_t), "uct_sdma_key_t");
    if (sdma_memh == NULL) {
        ucs_error("Failed to allocate memory for sdma_memh");
        return UCS_ERR_NO_MEMORY;
    }

    if (sdma_md->sdma_fd[0] < 0 || sdma_md->pin_umem_cb == NULL) {
        ucs_free(sdma_memh);
        return UCS_ERR_IO_ERROR;
    }

    status = (ucs_status_t)sdma_md->pin_umem_cb(sdma_md->sdma_fd[0], address, (uint32_t)length, &sdma_memh->cookie);
    if (status != UCS_OK) {
        ucs_error("sdma_pin_umem failed , status is %d.", status);
        ucs_free(sdma_memh);
        return UCS_ERR_IO_ERROR;
    }
    ucs_trace("uct_sdma_mem_reg OK!pin_addr is %p", address);

    sdma_memh->address = address;
    *memh_p = sdma_memh;
    return status;
}

ucs_status_t uct_sdma_mem_dereg(uct_md_h md, const uct_md_mem_dereg_params_t *params)
{
    uct_sdma_md_t *sdma_md = ucs_derived_of(md, uct_sdma_md_t);
    ucs_status_t status = UCS_OK;
    uct_sdma_key_t *sdma_memh;

    if (sdma_md->sdma_fd[0] < 0 || sdma_md->pin_umem_cb == NULL) {
        return UCS_ERR_IO_ERROR;
    }
    UCT_MD_MEM_DEREG_CHECK_PARAMS(params, 0);

    sdma_memh = (uct_sdma_key_t *)params->memh;
    status = (ucs_status_t)sdma_md->unpin_umem_cb(sdma_md->sdma_fd[0], sdma_memh->cookie);
    if (status != UCS_OK) {
        ucs_error("sdma_unpin_umem failed , status is %d.", status);
        return UCS_ERR_IO_ERROR;
    }
    ucs_free(sdma_memh);
    return status;
}

ucs_status_t uct_sdma_mem_alloc(uct_md_h tl_md, size_t *length_p, void **address_p, ucs_memory_type_t mem_type,
    unsigned flags, const char *alloc_name, uct_mem_h *memh_p)
{
    void *sdma_mem_addr = ucs_malloc(*length_p, "sdma_mem_addr");
    ucs_status_t status;

    if (sdma_mem_addr == NULL) {
        ucs_error("Failed to allocate memory for sdma_mem_addr");
        return UCS_ERR_NO_MEMORY;
    }

    status = uct_sdma_mem_reg(tl_md, sdma_mem_addr, *length_p, NULL, memh_p);
    if (status != UCS_OK) {
        ucs_error("Failed to pinned memory for sdma_mem_addr");
        return status;
    }
    *address_p = sdma_mem_addr;
    ucs_debug("uct_sdma_mem_alloc sdma_mem_addr is %p ", sdma_mem_addr);
    return UCS_OK;
}

ucs_status_t uct_sdma_mem_free(uct_md_h md, uct_mem_h memh)
{
    uct_sdma_key_t *sdma_memh = memh;
    ucs_status_t status = UCS_OK;
    uct_sdma_md_t *sdma_md = ucs_derived_of(md, uct_sdma_md_t);

    if (sdma_memh == NULL || sdma_memh->address == NULL) {
        if (sdma_memh) {
            ucs_free(sdma_memh);
        }
        return UCS_ERR_NO_MEMORY;
    }

    if (sdma_md->sdma_fd[0] < 0 || sdma_md->unpin_umem_cb == NULL) {
        status = UCS_ERR_IO_ERROR;
        goto out;
    }
    status = (ucs_status_t)sdma_md->unpin_umem_cb(sdma_md->sdma_fd[0], sdma_memh->cookie);
    if (status != UCS_OK) {
        ucs_error("sdma_unpin_umem failed , status is %d.", status);
        status = UCS_ERR_IO_ERROR;
        goto out;
    }
out:
    ucs_free(sdma_memh->address);
    ucs_free(sdma_memh);
    return status;
}

static uct_md_ops_t uct_sdma_md_ops = {
    .close              = uct_sdma_md_close,
    .query              = uct_sdma_md_query,
    .mem_alloc          = uct_sdma_mem_alloc,
    .mem_free           = uct_sdma_mem_free,
    .mem_advise         = (uct_md_mem_advise_func_t)ucs_empty_function_return_unsupported,
    .mem_reg            = uct_sdma_mem_reg,
    .mem_dereg          = uct_sdma_mem_dereg,
    .mkey_pack          = (uct_md_mkey_pack_func_t)ucs_empty_function_return_success,
    .detect_memory_type = (uct_md_detect_memory_type_func_t)ucs_empty_function_return_unsupported
};

static ucs_status_t uct_sdma_open_device(uct_sdma_md_t *md, uct_sdma_md_config_t *md_config)
{
    int sdma_num = 0;
    char sdma_dev[md_config->dev_name_len];
    sprintf(sdma_dev, "/dev/sdma%d", 0);

    md->num_devices = 0;
    md->sdma_fd = ucs_calloc(sizeof(int), md->max_num_devices, "sdma_fd");
    if (md->sdma_fd == NULL) {
        ucs_error("Failed to allocate memory for uct_sdma_md_t sdma_fd");
        return UCS_ERR_NO_MEMORY;
    }

    md->sdma_fd[0] = open(sdma_dev, O_RDWR);
    if (md->sdma_fd[0] < 0) {
        ucs_error("Failed to create src_sdma_fd: %s for: %m", sdma_dev);
        goto open_fd_err;
    }
    sdma_num += 1;
    md->num_devices = sdma_devices_num(md->sdma_fd[0]);
    if (md->num_devices == SDMA_FAILED || md->num_devices > md->max_num_devices) {
        md->num_devices = 0;
        ucs_error("Failed to find sdma devices: %m");
        goto query_num_err;
    }
    while (sdma_num < md->num_devices) {
        sprintf(sdma_dev, "/dev/sdma%d", sdma_num);
        md->sdma_fd[sdma_num] = open(sdma_dev, O_RDWR);
        if (md->sdma_fd[sdma_num] < 0) {
            ucs_error("Failed to create src_sdma_fd: %m");
            goto query_num_err;
        }
        sdma_num += 1;
    }
    return UCS_OK;
query_num_err:
    while (sdma_num > 0) {
        close(md->sdma_fd[sdma_num - 1]);
        sdma_num -= 1;
    }
open_fd_err:
    ucs_free(md->sdma_fd);
    return UCS_ERR_NO_DEVICE;
}

static ucs_status_t uct_sdma_md_open(
    uct_component_t *component, const char *md_name, const uct_md_config_t *config, uct_md_h *md_p)
{
    uct_sdma_md_config_t *md_config = ucs_derived_of(config, uct_sdma_md_config_t);
    uct_sdma_md_t *md;
    ucs_status_t status;

    md = ucs_malloc(sizeof(*md), "uct_sdma_md_t");
    if (md == NULL) {
        ucs_error("Failed to allocate memory for uct_sdma_md_t");
        return UCS_ERR_NO_MEMORY;
    }

    md->max_num_devices = md_config->dev_num;
    status = uct_sdma_open_device(md, md_config);
    if (status != UCS_OK) {
        ucs_error("Failed to open sdma device");
        ucs_free(md);
        return UCS_ERR_NO_DEVICE;
    }

    md->super.ops = &uct_sdma_md_ops;
    md->super.component = &uct_sdma_component;
    md->pin_umem_cb = sdma_pin_umem;
    md->unpin_umem_cb = sdma_unpin_umem;

    *md_p = &md->super;
    ucs_debug("uct_sdma_md_open sdma device OK,sdma_fd is %d num is %d.", md->sdma_fd[0], md->num_devices);
    return UCS_OK;
}

static ucs_status_t uct_md_query_sdma_md_resource(
    uct_component_t *component, uct_md_resource_desc_t **resources_p, unsigned *num_resources_p)
{
    int fd;
    fd = open("/dev/sdma0", O_RDWR);
    if (fd < 0) {
        ucs_debug("could not open the SDMA device file at /dev/sdma0: %m. Disabling sdma resource");
        return uct_md_query_empty_md_resource(resources_p, num_resources_p);
    }
    close(fd);
    return uct_md_query_single_md_resource(component, resources_p, num_resources_p);
}

uct_component_t uct_sdma_component = {
    .name = {'s', 'd', 'm', 'a', '\0'},
    .query_md_resources = uct_md_query_sdma_md_resource,
    .md_open = uct_sdma_md_open,
    .cm_open = (uct_component_cm_open_func_t)ucs_empty_function_return_unsupported,
    .rkey_unpack = uct_md_stub_rkey_unpack,
    .rkey_ptr = (uct_component_rkey_ptr_func_t)ucs_empty_function_return_success,
    .rkey_release = (uct_component_rkey_release_func_t)ucs_empty_function_return_success,
    .md_config =
        {
            .name = " sdma memory domain",
            .prefix = "SDMA_",
            .table = uct_sdma_md_config_table,
            .size = sizeof(uct_sdma_md_config_t),
        },
    .cm_config = UCS_CONFIG_EMPTY_GLOBAL_LIST_ENTRY,
    .tl_list = UCT_COMPONENT_TL_LIST_INITIALIZER(&uct_sdma_component),
    .flags = 0,
    .md_vfs_init = (uct_component_md_vfs_init_func_t)ucs_empty_function
};
UCT_COMPONENT_REGISTER(&uct_sdma_component);
