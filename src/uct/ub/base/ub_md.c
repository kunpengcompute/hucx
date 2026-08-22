/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ub_md.h"
#include "ub_device.h"
#include "ub_iface.h"

#include <ucs/arch/atomic.h>
#include <ucs/profile/profile.h>
#include <ucs/sys/math.h>
#include <ucs/sys/module.h>
#include <ucs/sys/string.h>
#include <ucs/time/time.h>
#include <ucm/api/ucm.h>
#include <ucs/datastruct/string_buffer.h>
#include <uct/api/v2/uct_v2.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include <sys/resource.h>
#include <float.h>
#include <unistd.h>

#define UCT_UB_MD_RCACHE_DEFAULT_ALIGN 16
#define UCT_UB_MD_DEVICE_NAME_MAX_LEN 256

#define UCT_UB_PCI_INFO_STR_LEN (16)
#define UCT_UB_GTS_STR_LEN (16)
#define UCT_UB_FLOATING_POINT_CMP_TH (1.01)
#define UCT_UB_BW_GBITS_TO_MBYTES (1e9 / 8.0)
#define UCT_UB_BW_MBYTES_TO_GBITS (8e-9)

#define UCT_UB_MEM_ACCESS_FLAGS \
    (URMA_ACCESS_WRITE | URMA_ACCESS_READ | URMA_ACCESS_ATOMIC)

typedef struct uct_ub_md_pci_info {
    const char *name; /* name of PCI generation */
    double     bw_gbps; /* link speed */
    uint16_t   payload; /* payload used to data transfer */
    uint16_t   tlp_overhead; /* PHY + data link layer + header + *CRC* */
    uint16_t   ctrl_ratio; /* number of TLC before ACK */
    uint16_t   ctrl_overhead; /* length of control TLP */
    uint16_t   encoding; /* number of encoded symbol bits */
    uint16_t   decoding; /* number of decoded symbol bits */
} uct_ub_md_pci_info_t;

ucs_status_t hash_status[HASH_LEN] = {
    UCS_ERR_NO_RESOURCE,
    UCS_ERR_NO_RESOURCE,
    UCS_ERR_EXCEEDS_LIMIT,
    UCS_ERR_TIMED_OUT,
    UCS_ERR_INVALID_PARAM,
    UCS_ERR_ALREADY_EXISTS,
    UCS_ERR_IO_ERROR
};

ucs_spinlock_t g_uct_ub_rkey_lock;
khash_t(uct_ub_rkey_hash) g_uct_ub_rkey_hash;

static UCS_CONFIG_DEFINE_ARRAY(pci_bw, sizeof(ucs_config_bw_spec_t), UCS_CONFIG_TYPE_BW_SPEC);

static ucs_config_field_t uct_ub_md_config_table[] = {
    {"", "", NULL,
     ucs_offsetof(uct_ub_md_config_t, super), UCS_CONFIG_TYPE_TABLE(uct_md_config_table)},

    {"REG_METHODS", "odp,direct",
     "List of registration methods in order of preference. Supported methods are:\n"
     "  odp         - implicit on-demand paging\n"
     "  direct      - direct registration\n",
     ucs_offsetof(uct_ub_md_config_t, reg_methods), UCS_CONFIG_TYPE_STRING_ARRAY},

    {"MEM_REG_OVERHEAD", "16us", "Memory registration overhead", /* TODO: take default from device */
     ucs_offsetof(uct_ub_md_config_t, reg_cost.c), UCS_CONFIG_TYPE_TIME},

    {"MEM_REG_GROWTH", "0.06ns", "Memory registration growth rate", /* TODO: take default from device */
     ucs_offsetof(uct_ub_md_config_t, reg_cost.m), UCS_CONFIG_TYPE_TIME},

    {"ASYNC_EVENTS", "y",
     "Enable listening for async events on the device",
     ucs_offsetof(uct_ub_md_config_t, async_events), UCS_CONFIG_TYPE_BOOL},

    {"PCI_BW", "",
     "Maximum effective data transfer rate of PCI bus connected to HCA, \n"
     "when the device is ub device, this config is not valid.\n",
     ucs_offsetof(uct_ub_md_config_t, pci_bw), UCS_CONFIG_TYPE_ARRAY(pci_bw)},

    {NULL}
};

static const uct_ub_md_pci_info_t uct_ub_md_pci_info[] = {
    {
        .name          = "gen1",
        .bw_gbps       = 2.5,
        .payload       = 256,
        .tlp_overhead  = 24,
        .ctrl_ratio    = 4,
        .ctrl_overhead = 16,
        .encoding      = 8,
        .decoding      = 10
    },
    {
        .name          = "gen2",
        .bw_gbps       = 5,
        .payload       = 256,
        .tlp_overhead  = 24,
        .ctrl_ratio    = 4,
        .ctrl_overhead = 16,
        .encoding      = 8,
        .decoding      = 10
    },
    {
        .name          = "gen3",
        .bw_gbps       = 8,
        .payload       = 256,
        .tlp_overhead  = 26,
        .ctrl_ratio    = 4,
        .ctrl_overhead = 16,
        .encoding      = 128,
        .decoding      = 130
    },
    {
        .name          = "gen4",
        .bw_gbps       = 16,
        .payload       = 256,
        .tlp_overhead  = 26,
        .ctrl_ratio    = 4,
        .ctrl_overhead = 16,
        .encoding      = 128,
        .decoding      = 130
    },
};

#ifdef ENABLE_STATS
static ucs_stats_class_t uct_ub_md_stats_class = {
    .name           = "",
    .num_counters   = UCT_UB_MD_STAT_LAST,
    .counter_names = {
        [UCT_UB_MD_STAT_MEM_ALLOC]   = "mem_alloc",
        [UCT_UB_MD_STAT_MEM_REG]     = "mem_reg"
    }
};
#endif

ucs_status_t get_uct_ub_status(urma_status_t urma_status)
{
    int index = urma_status - URMA_EAGAIN;

    if (urma_status == URMA_SUCCESS) {
        return UCS_OK;
    }

    if (index >= 0 && index < HASH_LEN) {
        return hash_status[index];
    }

    return UCS_ERR_IO_ERROR;
}

static ucs_status_t uct_ub_md_query(uct_md_h uct_md, uct_md_attr_v2_t *md_attr)
{
    uct_ub_md_t *md = ucs_derived_of(uct_md, uct_ub_md_t);

    md_attr->max_alloc = ULONG_MAX; /* TODO: query device */
    md_attr->max_reg = ULONG_MAX; /* TODO: query device */
    md_attr->flags = UCT_MD_FLAG_REG |
                     UCT_MD_FLAG_NEED_MEMH |
                     UCT_MD_FLAG_NEED_RKEY;
    md_attr->reg_mem_types = UCS_BIT(UCS_MEMORY_TYPE_HOST);
    md_attr->alloc_mem_types = 0;
    md_attr->access_mem_types = UCS_BIT(UCS_MEMORY_TYPE_HOST);
    md_attr->detect_mem_types = 0;
    md_attr->dmabuf_mem_types = 0;
    md_attr->cache_mem_types = UCS_MASK(UCS_MEMORY_TYPE_LAST);
    md_attr->rkey_packed_size = UCT_UB_MD_PACKED_RKEY_SIZE; /* TODO: calc */
    md_attr->reg_cost = md->reg_cost;
    md_attr->exported_mkey_packed_size = 0;
    ucs_sys_cpuset_copy(&md_attr->local_cpus, &md->dev.local_cpus);

    return UCS_OK;
}

static void uct_ub_memh_free(uct_ub_mem_t *memh)
{
    ucs_free(memh);
}

static ucs_status_t uct_ub_memh_dereg(uct_ub_md_t *md, uct_ub_mem_t *memh)
{
    urma_target_seg_t *tseg = memh->tseg;
    urma_token_id_t *token_id;
    urma_status_t urma_status;

    if (tseg == NULL) {
        return UCS_OK;
    }
    token_id = tseg->token_id;
    urma_status = urma_unregister_seg(tseg);
    if (urma_status != URMA_SUCCESS) {
        ucs_error("Failed to urma_unregister_seg: %m");
        return UCS_ERR_IO_ERROR;
    }

    urma_status = urma_free_token_id(token_id);
    if (urma_status != URMA_SUCCESS) {
        ucs_error("Failed to urma_free_token_id: %d", urma_status);
        return UCS_ERR_IO_ERROR;
    }

    return UCS_OK;
}

static uct_ub_mem_t *uct_ub_memh_alloc(uct_ub_md_t *md)
{
    return (uct_ub_mem_t *)ucs_calloc(1, md->memh_struct_size, "ub_memh");
}

static void uct_ub_md_access_flags(urma_reg_seg_flag_t *urma_flag)
{
    // TODO: config access flags based on md_config and uct_flags
    urma_flag->bs.access = UCT_UB_MEM_ACCESS_FLAGS;
    urma_flag->bs.cacheable = URMA_CACHEABLE;
    urma_flag->bs.token_policy = URMA_TOKEN_PLAIN_TEXT;
    urma_flag->bs.token_id_valid = URMA_TOKEN_ID_VALID;
}

static void uct_ub_mem_init(uct_ub_mem_t *memh, unsigned uct_flags)
{
    // TODO: config memh->flags based on access_flags
    memh->flags = 0;
}

static ucs_status_t uct_ub_md_reg_seg(uct_ub_md_t *md, void *address, size_t length,
                                      urma_reg_seg_flag_t urma_flag, uct_ub_mem_t *memh)
{
    urma_token_id_t *entry_tid;
    urma_token_id_flag_t flag;
    urma_seg_cfg_t seg_cfg;

    flag.bs.multi_seg = 0;
    entry_tid = urma_alloc_token_id_ex(md->dev.urma_ctx, flag);
    if (entry_tid == NULL) {
        ucs_error("Failed to urma_alloc_token_id_ex, entry_tid = NULL");
        return UCS_ERR_NO_RESOURCE;
    }
    seg_cfg.va          = (uint64_t)address;
    seg_cfg.len         = length;
    seg_cfg.token_id    = entry_tid;
    seg_cfg.token_value = md->dev.token;
    seg_cfg.flag        = urma_flag;
    seg_cfg.user_ctx    = (uint64_t)NULL;
    seg_cfg.iova        = 0;

    memh->tseg = urma_register_seg(md->dev.urma_ctx, &seg_cfg);
    if (memh->tseg == NULL) {
        (void)urma_free_token_id(entry_tid);
        ucs_error("Register seg fail. memh->tseg = NULL");
        return UCS_ERR_NO_RESOURCE;
    }
    ucs_trace("Token_id:%u", memh->tseg->seg.token_id);
    return UCS_OK;
}

static ucs_status_t uct_ub_mem_reg_internal(uct_md_h uct_md, void *address, size_t length,
                                            const uct_md_mem_reg_params_t *params,
                                            uct_ub_mem_t *memh)
{
    uct_ub_md_t *md = ucs_derived_of(uct_md, uct_ub_md_t);
    urma_reg_seg_flag_t urma_flag = {0};
    ucs_status_t status;

    uct_ub_md_access_flags(&urma_flag);

    uct_ub_mem_init(memh, params->flags);

    status = uct_ub_md_reg_seg(md, address, length, urma_flag, memh);
    if (status != UCS_OK) {
        return status;
    }

    ucs_debug("Registered memory on %s urma_flag 0x%x flags 0x%lx",
              md->dev.dev_name, urma_flag.value, params->flags);

    UCS_STATS_UPDATE_COUNTER(md->stats, UCT_UB_MD_STAT_MEM_REG, +1);
    return UCS_OK;
}

static ucs_status_t uct_ub_mem_reg(uct_md_h uct_md, void *address, size_t length,
                                   const uct_md_mem_reg_params_t *params, uct_mem_h *memh_p)
{
    uct_ub_md_t *md = ucs_derived_of(uct_md, uct_ub_md_t);
    uct_ub_mem_t *memh = NULL;
    ucs_status_t status;

    if (memh_p == NULL) {
        ucs_debug("Failed to reg ub mem, memh_p is NULL");
        return UCS_ERR_INVALID_PARAM;
    }

    memh = uct_ub_memh_alloc(md);
    if (memh == NULL) {
        uct_md_log_mem_reg_error(params->flags, "Md failed to allocate memory handle");
        return UCS_ERR_NO_MEMORY;
    }

    status = uct_ub_mem_reg_internal(uct_md, address, length, params, memh);
    if (status != UCS_OK) {
        uct_ub_memh_free(memh);
        return status;
    }
    *memh_p = memh;

    return UCS_OK;
}

static ucs_status_t uct_ub_mem_dereg(uct_md_h uct_md, const uct_md_mem_dereg_params_t *params)
{
    uct_ub_md_t *md = ucs_derived_of(uct_md, uct_ub_md_t);
    uct_ub_mem_t *ub_memh = (uct_ub_mem_t *)params->memh;
    ucs_status_t status;

    status  = uct_ub_memh_dereg(md, ub_memh);
    uct_ub_memh_free(ub_memh);

    if (UCT_MD_MEM_DEREG_FIELD_VALUE(params, flags, FIELD_FLAGS, 0) & UCT_MD_MEM_DEREG_FLAG_INVALIDATE) {
        ucs_assert(params->comp != NULL);
        uct_invoke_completion(params->comp, UCS_OK);
    }

    return status;
}

static ucs_status_t uct_ub_mkey_pack(uct_md_h uct_md, uct_mem_h uct_memh,
                                     const uct_md_mkey_pack_params_t *params,
                                     void *rkey_buffer)
{
    uct_ub_seg_info_t *rkey_info       = (uct_ub_seg_info_t *)rkey_buffer;
    uct_ub_mem_t *memh                 = (uct_ub_mem_t *)uct_memh;

    rkey_info->eid          = memh->tseg->seg.ubva.eid;
    rkey_info->uasid        = memh->tseg->seg.ubva.uasid;
    rkey_info->seg_va       = memh->tseg->seg.ubva.va;
    rkey_info->seg_len      = memh->tseg->seg.len;
    rkey_info->seg_flag     = memh->tseg->seg.attr.value;
    rkey_info->seg_token_id = memh->tseg->seg.token_id;

    return UCS_OK;
}

static ucs_status_t uct_ub_rkey_unpack(uct_component_t *component,
                                       const void *rkey_buffer, uct_rkey_t *rkey_p,
                                       void **handle_p)
{
    uct_ub_seg_info_t *rkey_info = (uct_ub_seg_info_t *)rkey_buffer;
    uct_ub_rkey_info_t **entry = NULL;
    uct_ub_rkey_info_t *elem = NULL;
    ucs_status_t status = UCS_OK;
    uct_ub_rkey_hash_key_t key;
    khiter_t iter;
    int ret;

    ucs_spin_lock(&g_uct_ub_rkey_lock);

    key.deid     = rkey_info->eid;
    key.duasid   = rkey_info->uasid;
    key.va       = rkey_info->seg_va;
    key.token_id = rkey_info->seg_token_id;
    iter = kh_put(uct_ub_rkey_hash, &g_uct_ub_rkey_hash, key, &ret);
    if (ret == UCS_KH_PUT_FAILED) {
        ucs_error("Put hash error, deid = "EID_FMT", duasid:%u, va:%u.", EID_ARGS(key.deid), key.duasid, key.va);
        status = UCS_ERR_NO_MEMORY;
        goto out;
    }
    entry = &kh_value(&g_uct_ub_rkey_hash, iter);

    if (ret == UCS_KH_PUT_KEY_PRESENT) {
        elem = *entry;
        elem->ref_cnt++;
    } else {
        elem = (uct_ub_rkey_info_t *)ucs_calloc(1, sizeof(uct_ub_rkey_info_t), "uct_ub_rkey_info_t");
        if (elem == NULL) {
            ucs_error("Failed to allocate memory for uct_ub_rkey_info_t");
            status = UCS_ERR_NO_MEMORY;
            goto out;
        }
        elem->remote_seg.ubva.eid   = rkey_info->eid;
        elem->remote_seg.ubva.uasid = rkey_info->uasid;
        elem->remote_seg.ubva.va    = rkey_info->seg_va;
        elem->remote_seg.len        = rkey_info->seg_len;
        elem->remote_seg.attr.value = rkey_info->seg_flag;
        elem->remote_seg.token_id   = rkey_info->seg_token_id;
        elem->ref_cnt               = 1;
        (void)pthread_rwlock_init(&elem->import_tseg_lock, NULL);
        *entry = elem;
    }
    *rkey_p   = (uct_rkey_t)elem;
    *handle_p = NULL;

out:
    ucs_spin_unlock(&g_uct_ub_rkey_lock);

    return status;
}

static ucs_status_t uct_ub_rkey_release(uct_component_t *component,
                                        uct_rkey_t rkey, void *handle)
{
    uct_ub_rkey_info_t *rkey_info = (uct_ub_rkey_info_t *)rkey;
    uct_ub_rkey_info_t **entry = NULL;
    uct_ub_rkey_info_t *elem = NULL;
    ucs_status_t status = UCS_OK;
    uct_ub_rkey_hash_key_t key;
    urma_status_t ret;
    khiter_t iter;

    ucs_spin_lock(&g_uct_ub_rkey_lock);

    key.deid     = rkey_info->remote_seg.ubva.eid;
    key.duasid   = rkey_info->remote_seg.ubva.uasid;
    key.va       = rkey_info->remote_seg.ubva.va;
    key.token_id = rkey_info->remote_seg.token_id;
    iter = kh_get(uct_ub_rkey_hash, &g_uct_ub_rkey_hash, key);
    if (iter == kh_end(&g_uct_ub_rkey_hash)) {
        status = UCS_ERR_NO_ELEM;
        goto out;
    }

    entry = &kh_value(&g_uct_ub_rkey_hash, iter);
    elem = *entry;
    elem->ref_cnt--;

    /* Performance optimization temporarily adopts the solution of avoiding this interface */
    if (elem->ref_cnt == 0) {
        if (rkey_info->import_tseg == NULL) {
            ucs_debug("Rkey_info->import_tseg is NULL");
            status = UCS_ERR_NO_RESOURCE;
            goto out;
        }
        ret = urma_unimport_seg(rkey_info->import_tseg);
        if (ret != URMA_SUCCESS) {
            ucs_warn("Failed to urma_unimport_seg, ret = %d", ret);
            status = UCS_ERR_IO_ERROR;
            goto out;
        }
        rkey_info->import_tseg = NULL;
        pthread_rwlock_destroy(&rkey_info->import_tseg_lock);
        kh_del(uct_ub_rkey_hash, &g_uct_ub_rkey_hash, iter);
        free(elem);
        elem = NULL;
    }

out:
    ucs_spin_unlock(&g_uct_ub_rkey_lock);
    return status;
}

static uct_md_ops_t uct_ub_md_ops = {
    .close              = uct_ub_md_close,
    .query              = uct_ub_md_query,
    .mem_advise         = ucs_empty_function_return_unsupported,
    .mem_reg            = uct_ub_mem_reg,
    .mem_dereg          = uct_ub_mem_dereg,
    .mkey_pack          = uct_ub_mkey_pack,
    .detect_memory_type = ucs_empty_function_return_unsupported,
};

bool uct_check_ip_valid(urma_device_t *urma_dev, uint32_t eid_index)
{
    urma_eid_info_t *eid_list = NULL;
    uint32_t eid_cnt;
    uint32_t i;

    eid_list = urma_get_eid_list(urma_dev, &eid_cnt);
    if (eid_list == NULL) {
        return false;
    }
    if (eid_cnt <= 0) {
        urma_free_eid_list(eid_list);
        return false;
    }

    for (i = 0; i < eid_cnt; i++) {
        if (eid_list[i].eid_index == eid_index && eid_list[i].eid.in4.addr != 0) {
            urma_free_eid_list(eid_list);
            return true;
        }
    }

    urma_free_eid_list(eid_list);
    return false;
}

bool uct_check_urma_device_state(urma_device_t *urma_dev)
{
    urma_device_attr_t dev_attr;
    uint32_t port_idx;
    bool ret;

    /* The value of eid_index is the same with the second parameter of function urma_create_context */
    ret = uct_check_ip_valid(urma_dev, 0);
    if (ret == false) {
        ucs_info("The ip is invalid for device: %s.\n", urma_dev->name);
        return false;
    }

    if (urma_query_device(urma_dev, &dev_attr) == URMA_SUCCESS) {
        for (port_idx = 0; port_idx < dev_attr.port_cnt; port_idx++) {
            if (dev_attr.port_attr[port_idx].state == URMA_PORT_ACTIVE) {
                return true;
            }
        }
    }

    return false;
}


static ucs_status_t uct_ub_query_md_resources(uct_component_t *component,
                                              uct_md_resource_desc_t **resources_p,
                                              unsigned *num_resources_p)
{
    uct_md_resource_desc_t *resources = NULL;
    UCS_MODULE_FRAMEWORK_DECLARE(uct_ub);
    urma_device_t **device_list = NULL;
    uint32_t num_devices = 0;
    ucs_status_t status;
    int index = 0;
    int i;

    UCS_MODULE_FRAMEWORK_LOAD(uct_ub, 0);
    device_list = urma_get_device_list(&num_devices);
    if (device_list == NULL) {
        ucs_debug("Failed to get urma device list, assuming no devices are present");
        *resources_p = NULL;
        *num_resources_p = 0;
        return UCS_OK;
    }

    resources = ucs_calloc(num_devices, sizeof(*resources), "ub resources");
    if (resources == NULL) {
        status = UCS_ERR_NO_MEMORY;
        goto out_free_device_list;
    }

    for (i = 0; i < num_devices; ++i) {
        if ((device_list[i]->type != URMA_TRANSPORT_UB) || (uct_check_urma_device_state(device_list[i]) == false)) {
            continue;
        }
        ucs_snprintf_zero(resources[index].md_name, sizeof(resources[index].md_name),
                          "%s", device_list[i]->name);
        index++;
    }
    *resources_p = resources;
    *num_resources_p = index;
    urma_free_device_list(device_list);
    return UCS_OK;

out_free_device_list:
    urma_free_device_list(device_list);
    return status;
}

static void uct_ub_md_release_device_config(uct_ub_md_t *md)
{
    unsigned i;

    for (i = 0; i < md->custom_devices.count; ++i) {
        free((char*)md->custom_devices.specs[i].name);
    }
    ucs_free(md->custom_devices.specs);
}

static ucs_status_t uct_ub_md_parse_device_config(uct_ub_md_t *md, const uct_ub_md_config_t *md_config)
{
    uct_ub_device_spec_t *spec = NULL;
    ucs_status_t status = UCS_OK;
    char *flags_str = NULL;
    unsigned i, count;
    int nfields;

    count = md->custom_devices.count = md_config->custom_devices.count;
    if (count == 0) {
        md->custom_devices.specs = NULL;
        md->custom_devices.count = 0;
        return status;
    }

    md->custom_devices.specs = (uct_ub_device_spec_t *)ucs_calloc(count, sizeof(*md->custom_devices.specs),
                                          "ub_custom_devices");
    if (md->custom_devices.specs == NULL) {
        status = UCS_ERR_NO_MEMORY;
        goto err;
    }

    for (i = 0; i < count; ++i) {
        spec = &md->custom_devices.specs[i];
        nfields = sscanf(md_config->custom_devices.spec[i],
                         "%hi:%hi:%m[^:]:%m[^:]:%hhu",
                         &spec->pci_id.vendor, &spec->pci_id.device, &spec->name,
                         &flags_str, &spec->priority);
        if (nfields < 2) { // 2: min device config fields
            ucs_error("Failed to parse device config '%s' (parsed: %d/%d)",
                      md_config->custom_devices.spec[i], nfields, 5); // 5: max device config fields
            status = UCS_ERR_INVALID_PARAM;
            goto err_free;
        }

        spec->flags = 0; // TODO: get ub flags by abbreviations

        ucs_trace("Added device '%s' vendor_id 0x%x device_id %d flags %u prio %d",
                  spec->name, spec->pci_id.vendor, spec->pci_id.device, spec->flags, spec->priority);
    }

    return status;

err_free:
    uct_ub_md_release_device_config(md);
err:
    return status;
}

static double uct_ub_md_read_pci_bw(const char *md_name)
{
    const char *pci_width_file_name = "current_link_width";
    const char *pci_speed_file_name = "current_link_speed";
    double bw_gbps, effective_bw, link_utilization;
    char pci_width_str[UCT_UB_PCI_INFO_STR_LEN];
    char pci_speed_str[UCT_UB_PCI_INFO_STR_LEN];
    const uct_ub_md_pci_info_t *p = NULL;
    char gts[UCT_UB_GTS_STR_LEN];
    unsigned width;
    ssize_t len;
    size_t i;

    len = ucs_read_file(pci_width_str, sizeof(pci_width_str) - 1, 1,
                        UCT_UB_DEVICE_SYSFS_FMT, md_name, pci_width_file_name);
    if (len < 1) {
        ucs_debug("Failed to read file: (%s, %s, %s)",
            UCT_UB_DEVICE_SYSFS_FMT, md_name, pci_width_file_name);
        return DBL_MAX; /* failed to read file */
    }
    pci_width_str[len] = '\0';

    len = ucs_read_file(pci_speed_str, sizeof(pci_speed_str) - 1, 1,
                        UCT_UB_DEVICE_SYSFS_FMT, md_name, pci_speed_file_name);
    if (len < 1) {
        ucs_debug("Failed to read file: (%s, %s, %s)",
            UCT_UB_DEVICE_SYSFS_FMT, md_name, pci_speed_file_name);
        return DBL_MAX; /* failed to read file */
    }
    pci_speed_str[len] = '\0';

    if (sscanf(pci_width_str, "%u", &width) < 1) {
        ucs_debug("Incorrect format of %s file: expected: <unsigned integer>, actual: %s\n",
                  pci_width_file_name, pci_width_str);
        return DBL_MAX;
    }

    if ((sscanf(pci_speed_str, "%lf%s", &bw_gbps, gts) < 2) || // 2: num of dst parameters
        strcasecmp("GT/s", ucs_strtrim(gts))) {
        ucs_debug("Incorrect format of %s file: expected: <double> GT/s, actual: %s\n",
                  pci_speed_file_name, pci_speed_str);
        return DBL_MAX;
    }

    for (i = 0; i < ucs_static_array_size(uct_ub_md_pci_info); i++) {
        p = &uct_ub_md_pci_info[i];
        if ((bw_gbps / p->bw_gbps) > UCT_UB_FLOATING_POINT_CMP_TH) {
            continue;
        }

        link_utilization = (double)(p->payload * p->ctrl_ratio) /
                           (((p->payload + p->tlp_overhead) * p->ctrl_ratio) + p->ctrl_overhead);
        /* coverity[overflow] */
        effective_bw     = (p->bw_gbps * UCT_UB_BW_GBITS_TO_MBYTES) * width *
                           ((double)p->encoding / p->decoding) * link_utilization;
        ucs_trace("%s: PCIe %s %ux, effective throughput %.3f MB/s %.3f Gb/s",
                  md_name, p->name, width, effective_bw / UCS_MBYTE, effective_bw * UCT_UB_BW_MBYTES_TO_GBITS);
        return effective_bw;
    }

    return DBL_MAX;
}

static double uct_ub_md_pci_bw(const uct_ub_md_config_t *md_config,
                               const char *md_name)
{
    unsigned i;

    for (i = 0; i < md_config->pci_bw.count; i++) {
        if (!strcmp(md_name, md_config->pci_bw.device[i].name)) {
            if (UCS_CONFIG_DBL_IS_AUTO(md_config->pci_bw.device[i].bw)) {
                break; /* read data from system */
            }
            return md_config->pci_bw.device[i].bw;
        }
    }

    return uct_ub_md_read_pci_bw(md_name);
}

UCS_LIST_HEAD(uct_ub_md_ops_list);

ucs_status_t uct_ub_md_open(uct_component_t *component, const char *md_name,
                            const uct_md_config_t *uct_md_config, uct_md_h *md_p)
{
    const uct_ub_md_config_t *md_config = ucs_derived_of(uct_md_config, uct_ub_md_config_t);
    uct_ub_md_ops_entry_t *md_ops_entry = NULL;
    ucs_status_t status = UCS_ERR_UNSUPPORTED;
    urma_device_t *urma_dev = NULL;
    uct_ub_md_t *md = NULL;
    int device_num;
    int i;

    urma_device_t **device_list;
    device_list = urma_get_device_list(&device_num);
    if (device_list == NULL) {
        status = UCS_ERR_NO_DEVICE;
        goto err;
    }

    for (i = 0; i < device_num; i++) {
        if (strcmp(device_list[i]->name, md_name) == 0) {
            urma_dev = device_list[i];
            break;
        }
    }

    ucs_list_for_each(md_ops_entry, &uct_ub_md_ops_list, list) {
        status = md_ops_entry->ops->open(urma_dev, md_config, &md);
        if (status == UCS_OK) {
            ucs_debug("%s: md open by '%s' is successful", md_name,
                      md_ops_entry->name);
            md->ops = md_ops_entry->ops;
            break;
        } else if (status != UCS_ERR_UNSUPPORTED) {
            goto free_list;
        }
        ucs_debug("%s: md open by '%s' failed, trying next", md_name,
                  md_ops_entry->name);
    }

    if (status != UCS_OK) {
        ucs_assert(status == UCS_ERR_UNSUPPORTED);
        ucs_debug("Unsupported UB device %s", md_name);
        goto free_list;
    }

    /* Out values */
    *md_p = &md->super;
    status = UCS_OK;
    goto free_list;

free_list:
    urma_free_device_list(device_list);
err:
    return status;
}

void uct_ub_md_close_common(uct_ub_md_t *md, urma_device_t *urma_dev, const uct_ub_md_config_t *md_config)
{
    uct_ub_device_t *ub_dev = NULL;

    ub_dev = &md->dev;
    uct_ub_device_cleanup(ub_dev);
    UCS_STATS_NODE_FREE(md->stats);
    uct_ub_md_release_device_config(md);
    urma_delete_context(ub_dev->urma_ctx);
}

ucs_status_t uct_ub_md_open_common(uct_ub_md_t *md,
                                   urma_device_t *urma_dev,
                                   const uct_ub_md_config_t *md_config)
{
    uct_ub_device_t *ub_dev = NULL;
    uct_md_attr_t md_attr;
    ucs_status_t status;

    ub_dev = &md->dev;
    status = uct_ub_device_query(urma_dev, ub_dev);
    if (status != UCS_OK) {
        ucs_debug("Failed to urma device query.");
        goto err;
    }

    ub_dev->urma_ctx = urma_create_context(urma_dev, 0);
    if (ub_dev->urma_ctx == NULL) {
        ucs_diag("Failed to urma_create_context: %m.");
        status = UCS_ERR_IO_ERROR;
        goto err;
    }

    status = uct_ub_md_parse_device_config(md, md_config);
    if (status != UCS_OK) {
        goto err_free_context;
    }

    md->memh_struct_size = sizeof(uct_ub_mem_t);
    md->super.ops        = &uct_ub_md_ops;
    md->super.component  = &uct_ub_component;
    md->reg_cost  = md_config->reg_cost;

    /* Create statistics */
    status = UCS_STATS_NODE_ALLOC(&md->stats, &uct_ub_md_stats_class,
                                  ucs_stats_get_root(),
                                  "%s", urma_dev->name);
    if (status != UCS_OK) {
        goto err_dev_cfg;
    }

    status = uct_ub_device_init(ub_dev, urma_dev->name, md_config->async_events
                                UCS_STATS_ARG(md->stats));
    if (status != UCS_OK) {
        goto err_release_stats;
    }

    status = uct_md_query(&md->super, &md_attr);
    if (status != UCS_OK) {
        goto err_cleanup_device;
    }

    ub_dev->max_zcopy_log_sge = INT_MAX;
    if (md_attr.cap.reg_mem_types & ~UCS_BIT(UCS_MEMORY_TYPE_HOST)) {
        ub_dev->max_zcopy_log_sge = 1;
    }

    // ub dev do not have pci bus, we donot support get ubus bw now
    if (ub_dev->urma_ctx->dev->type == URMA_TRANSPORT_UB) {
        md->pci_bw = uct_ub_md_pci_bw(md_config, urma_dev->name);
    } else {
        md->pci_bw = DBL_MAX;
    }

    return UCS_OK;

err_cleanup_device:
    uct_ub_device_cleanup(ub_dev);
err_release_stats:
    UCS_STATS_NODE_FREE(md->stats);
err_dev_cfg:
    uct_ub_md_release_device_config(md);
err_free_context:
    urma_delete_context(ub_dev->urma_ctx);
err:
    return status;
}

void uct_ub_md_close(uct_md_h uct_md)
{
    uct_ub_md_t *md = ucs_derived_of(uct_md, uct_ub_md_t);

    md->ops->cleanup(md);
    uct_ub_device_cleanup(&md->dev);
    uct_ub_md_release_device_config(md);
    UCS_STATS_NODE_FREE(md->stats);
    urma_delete_context(md->dev.urma_ctx);
    ucs_free(md);
}

static uct_ub_md_ops_t uct_ub_verbs_md_ops = {
    .open                = uct_ub_verbs_md_open,
    .cleanup             = (uct_ub_md_cleanup_func_t)ucs_empty_function,
};

UCT_UB_MD_OPS(uct_ub_verbs_md_ops, 0);

ucs_status_t uct_ub_verbs_md_open(urma_device_t *urma_dev,
                                  const uct_ub_md_config_t *md_config,
                                  uct_ub_md_t **p_md)
{
    uct_ub_md_t *md = NULL;
    ucs_status_t status;

    md = (uct_ub_md_t *)ucs_calloc(1, sizeof(*md), "ub_md");
    if (md == NULL) {
        ucs_error("Failed to calloc md");
        return UCS_ERR_NO_MEMORY;
    }

    status = uct_ub_md_open_common(md, urma_dev, md_config);
    if (status != UCS_OK) {
        goto err;
    }

    *p_md = md;
    return UCS_OK;

err:
    ucs_free(md);
    return status;
}

uct_component_t uct_ub_component = {
    .name               = {'u','b','\0'},
    .query_md_resources = uct_ub_query_md_resources,
    .md_open            = uct_ub_md_open,
    .cm_open            = ucs_empty_function_return_unsupported,
    .rkey_unpack        = uct_ub_rkey_unpack,
    .rkey_ptr           = ucs_empty_function_return_unsupported,
    .rkey_release       = uct_ub_rkey_release,
    .md_config          = {
        .name           = "UB memory domain",
        .prefix         = UCT_UB_CONFIG_PREFIX,
        .table          = uct_ub_md_config_table,
        .size           = sizeof(uct_ub_md_config_t),
    },
    .cm_config          = UCS_CONFIG_EMPTY_GLOBAL_LIST_ENTRY,
    .tl_list            = UCT_COMPONENT_TL_LIST_INITIALIZER(&uct_ub_component),
    .flags              = 0,
    .md_vfs_init        = (uct_component_md_vfs_init_func_t)ucs_empty_function
};

#if HAVE_UB_REG
UCT_COMPONENT_REGISTER(&uct_ub_component);
#endif

UCS_STATIC_INIT {
    urma_status_t urma_status = URMA_SUCCESS;
    uct_md_config_t *md_config = NULL;
    urma_init_attr_t init_attr = {0};
    ucs_status_t status;

    /* Read MD configuration */
    status = uct_md_config_read(&uct_ub_component, NULL, NULL, &md_config);
    if (status != UCS_OK) {
        ucs_debug("Md config read failed!");
        return;
    }

    urma_status = urma_init(&init_attr);
    if (urma_status != URMA_SUCCESS) {
        ucs_debug("Urma init failed!");
    }

    uct_config_release(md_config);

    kh_init_inplace(uct_ub_rkey_hash, &g_uct_ub_rkey_hash);
    ucs_spinlock_init(&g_uct_ub_rkey_lock, 0);
}

UCS_STATIC_CLEANUP {
    ucs_spinlock_destroy(&g_uct_ub_rkey_lock);
    kh_destroy_inplace(uct_ub_rkey_hash, &g_uct_ub_rkey_hash);
    urma_uninit();
}
