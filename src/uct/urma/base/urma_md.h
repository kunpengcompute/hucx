/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_URMA_MD_H
#define UCT_URMA_MD_H

#include <urma_types.h>
#include "urma_device.h"

#include <uct/base/uct_md.h>
#include <ucs/stats/stats.h>
#include <ucs/memory/numa.h>
#include <ucs/memory/rcache.h>
#include <urma_api.h>
#ifdef HAVE_URMA_IB
#include <urma_ib.h>
#endif

#define UCT_URMA_CONFIG_PREFIX "URMA_"
#define UCT_URMA_MD_PACKED_RKEY_SIZE  sizeof(uct_urma_seg_info_t)
#define UB_URMA_MAX_PRIORITY 5
#define HASH_LEN 7

extern ucs_status_t hash_status[HASH_LEN];

/**
 * URMA MD statistics counters
 */
enum {
    UCT_URMA_MD_STAT_MEM_ALLOC,
    UCT_URMA_MD_STAT_MEM_REG,
    UCT_URMA_MD_STAT_LAST
};

typedef struct uct_urma_seg_info {
    urma_eid_t eid;
    uint32_t   uasid;
    /* segment */
    uint64_t   seg_va;
    uint64_t   seg_len;
    uint32_t   seg_flag;
    uint32_t   seg_token_id ;
} UCS_S_PACKED uct_urma_seg_info_t;

typedef struct uct_urma_rkey_info {
    urma_seg_t remote_seg;
    urma_target_seg_t *import_tseg; /* Imported target segment for read/write/atomic */
    size_t ref_cnt;
    pthread_rwlock_t import_tseg_lock;
} uct_urma_rkey_info_t;

typedef struct uct_urma_md {
    uct_md_t                   super;
    uct_urma_device_t          dev;       /**< URMA device */
    ucs_linear_func_t          reg_cost;  /**< Memory registration cost */
    struct uct_urma_md_ops     *ops;
    UCS_STATS_NODE_DECLARE(stats)
    struct {
        uct_urma_device_spec_t *specs;    /* Custom device specifications */
        unsigned               count;     /* Number of custom devices */
    } custom_devices;
    double                     pci_bw;
    size_t                     memh_struct_size;
    urma_jfc_t                 *jfc;      /* just as a parameter for the function urma_register_seg */
} uct_urma_md_t;

typedef struct uct_urma_md_config {
    uct_md_config_t          super;

    /** List of registration methods in order of preference */
    UCS_CONFIG_STRING_ARRAY_FIELD(rmtd) reg_methods;
    ucs_linear_func_t        reg_cost;  /* Memory registration cost estimation without using the cache */
    int                      async_events; /* Whether async events should be delivered */
    UCS_CONFIG_STRING_ARRAY_FIELD(spec) custom_devices; /* Custom device specifications */
    UCS_CONFIG_ARRAY_FIELD(ucs_config_bw_spec_t, device) pci_bw; /* List of PCI BW for devices */
} uct_urma_md_config_t;

typedef struct uct_urma_mem {
    uint32_t            flags;
    urma_target_seg_t   *tseg;
} uct_urma_mem_t;

typedef struct uct_urma_rkey_hash_key {
    urma_eid_t              deid;
    uint32_t                duasid;
    uint32_t                va;
    uint32_t                token_id;
} uct_urma_rkey_hash_key_t;

/**
 * Memory domain constructor.
 *
 * @param [in]  urma_dev      URMA device.
 *
 * @param [in]  md_config     Memory domain configuration parameters.
 *
 * @param [out] md_p          Handle to memory domain.
 *
 * @return UCS_OK on success or error code in case of failure.
 */
typedef ucs_status_t (*uct_urma_md_open_func_t)(urma_device_t *urma_dev,
                                                const uct_urma_md_config_t *md_config,
                                                struct uct_urma_md **md_p);

/**
 * Memory domain destructor.
 *
 * @param [in]  md      Memory domain.
 */
typedef void (*uct_urma_md_cleanup_func_t)(struct uct_urma_md *);

typedef struct uct_urma_md_ops {
    uct_urma_md_open_func_t                open;
    uct_urma_md_cleanup_func_t             cleanup;
} uct_urma_md_ops_t;

typedef struct uct_urma_md_ops_entry {
    ucs_list_link_t             list;
    const char                  *name;
    uct_urma_md_ops_t           *ops;
    int                         priority;
} uct_urma_md_ops_entry_t;

#define UCT_URMA_MD_OPS(_md_ops, _priority) \
    extern ucs_list_link_t uct_urma_md_ops_list; \
    UCS_STATIC_INIT { \
        static uct_urma_md_ops_entry_t *p, entry = { \
            .name     = UCS_PP_MAKE_STRING(_md_ops), \
            .ops      = &_md_ops, \
            .priority = _priority, \
        }; \
        ucs_list_for_each(p, &uct_urma_md_ops_list, list) { \
            if (p->priority < _priority) { \
                ucs_list_insert_before(&p->list, &entry.list); \
                return; \
            } \
        } \
        ucs_list_add_tail(&uct_urma_md_ops_list, &entry.list); \
    }

KHASH_TYPE(uct_urma_rkey_hash, uct_urma_rkey_hash_key_t, uct_urma_rkey_info_t*);

static UCS_F_ALWAYS_INLINE khint32_t uct_urma_rkey_hash_func(uct_urma_rkey_hash_key_t key)
{
    return kh_int_hash_func(key.deid.in4.addr | key.duasid | key.va | key.token_id);
}

static UCS_F_ALWAYS_INLINE int uct_urma_rkey_hash_equal(uct_urma_rkey_hash_key_t a,
                                                        uct_urma_rkey_hash_key_t b)
{
    return ((a.deid.in4.addr == b.deid.in4.addr) && (a.duasid == b.duasid) &&
            (a.va == b.va) && (a.token_id == b.token_id));
}

KHASH_IMPL(uct_urma_rkey_hash, uct_urma_rkey_hash_key_t, uct_urma_rkey_info_t*, 1,
           uct_urma_rkey_hash_func, uct_urma_rkey_hash_equal)

static UCS_F_ALWAYS_INLINE urma_target_seg_t *uct_urma_memh_get_tseg(uct_mem_h memh)
{
    ucs_assert(memh != UCT_MEM_HANDLE_NULL);
    return ((uct_urma_mem_t*)memh)->tseg;
}

extern uct_component_t uct_urma_component;
extern ucs_spinlock_t g_uct_urma_rkey_lock;

ucs_status_t get_uct_urma_status(urma_status_t urma_status);
ucs_status_t uct_urma_md_open(uct_component_t *component, const char *md_name,
                              const uct_md_config_t *uct_md_config, uct_md_h *md_p);

ucs_status_t uct_urma_md_open_common(uct_urma_md_t *md,
                                     urma_device_t *urma_dev,
                                     const uct_urma_md_config_t *md_config);

ucs_status_t uct_urma_verbs_md_open(urma_device_t *urma_dev,
                                    const uct_urma_md_config_t *md_config,
                                    uct_urma_md_t **p_md);
void uct_urma_md_close_common(uct_urma_md_t *md,
                              urma_device_t *urma_dev,
                              const uct_urma_md_config_t *md_config);
void uct_urma_md_close(uct_md_h uct_md);
bool uct_check_ip_valid(urma_device_t *urma_dev, uint32_t eid_index);

#endif
