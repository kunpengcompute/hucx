/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCT_UB_DEVICE_H
#define UCT_UB_DEVICE_H

#include <urma_api.h>
#include <urma_types.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <uct/api/uct.h>
#include <uct/base/uct_iface.h>
#include <ucs/stats/stats.h>
#include <ucs/debug/assert.h>
#include <ucs/datastruct/callbackq.h>
#include <ucs/datastruct/khash.h>
#include <ucs/type/spinlock.h>
#include <ucs/sys/sock.h>

#include <endian.h>
#include <linux/ip.h>
#include <urma_opcode.h>

#define UCT_UB_DEVICE_SYSFS_PFX           "/sys/class/uburma/%s"
#define UCT_UB_DEVICE_SYSFS_FMT           UCT_UB_DEVICE_SYSFS_PFX "/device/%s"
#define UCT_UB_DEVICE_SYSFS_NUMA          "/sys/class/uburma/%s/device/numa_node"
#define UCT_UB_DEVICE_SYSFS_NDEV          "/sys/class/uburma/%s/device/ubcore/%s/net_dev"
#define UCT_UB_DEV_MAX_PORTS              2
#define UCT_UB_DEV_MAX_BUF_SIZE           200
#define UCT_UB_DEV_SHIFT_BITS             32
#define UCT_UB_DEV_PORT_ACTIVE            URMA_PORT_ACTIVE
#define UCT_UB_S6_ADDR0                   0xff0e0000
#define UCT_UB_S6_ADDR2                   0x0000ffff

#define UCT_UB_DEV_SET_CPU_MASK(k, word, base, cpu_mask) \
    do { \
        for ((k) = 0; (word); ++(k), (word) >>= 1) { \
                if ((word) & 1) { \
                    CPU_SET((base) + (k), (cpu_mask)); \
                } \
            } \
    } while (0)

enum {
    UCT_UB_DEVICE_STAT_ASYNC_EVENT,
    UCT_UB_DEVICE_STAT_LAST
};

enum {
    UCT_UB_ADDRESS_FLAG_GID_INDEX      = UCS_BIT(0),
    UCT_UB_ADDRESS_FLAG_LINK_LAYER_ETH = UCS_BIT(1),
    UCT_UB_ADDRESS_FLAG_IPV6           = UCS_BIT(2),
    UCT_UB_ADDRESS_FLAG_SUBNET16       = UCS_BIT(3),
    UCT_UB_ADDRESS_FLAG_SUBNET64       = UCS_BIT(4)
};

typedef struct uct_ub_address {
    /* Using flags from UCT_UB_ADDRESS_FLAG_xx */
    uint8_t            flags;
    /* Following fields appear in this order (if specified by flags).
     * - urma_eid_t eid
     * - uint32_t uasid
     * - uint32_t urma_mode
     */
} UCS_S_PACKED uct_ub_address_t;

/**
 * PCI identifier of a device
 */
typedef struct {
    urma_eid_t                  eid;
    uint8_t                     eid_index;
    urma_transport_type_t       type;
    sa_family_t                 addr_family;
} uct_ub_device_eid_info_t;

typedef struct {
    uint16_t                    vendor;
    uint16_t                    device;
} uct_ub_pci_id_t;

/**
 * ub device specification.
 */
typedef struct uct_ub_device_spec {
    const char                  *name;
    uct_ub_pci_id_t             pci_id;
    unsigned                    flags;
    uint8_t                     priority;
} uct_ub_device_spec_t;

/**
 * ub async event descriptor.
 */
typedef struct uct_ub_async_event {
    urma_async_event_type_t event_type;             /* Event type */
    union {
        urma_jfc_t *jfc;
        urma_jfs_t *jfs;
        urma_jfr_t *jfr;
        uint32_t port_id;
        uint32_t resource_id;
    };
} uct_ub_async_event_t;


/**
 * ub async event waiting context.
 */
typedef struct uct_ub_async_event_wait {
    ucs_callback_t      cb;                     /* Callback */
    ucs_callbackq_t     *cbq;                   /* Async queue for callback */
    int                 cb_id;                  /* Scheduled callback ID */
} uct_ub_async_event_wait_t;

typedef struct uct_rm_urma_ep_conn_entry {
    uint64_t   tjfr;
    uint64_t   ka_tseg;
    int32_t    ref_cnt;
} uct_rm_urma_ep_conn_entry_t;

typedef struct uct_rm_urma_remote_info {
    uint32_t   jfr_id;
    urma_eid_t   eid;
    uint32_t   uasid;
} uct_rm_urma_remote_info_t;

static UCS_F_ALWAYS_INLINE
khint32_t uct_rm_urma_ep_conn_hash_func(uct_rm_urma_remote_info_t info)
{
    return kh_int_hash_func(info.jfr_id |
                            info.eid.in4.addr    |
                            info.uasid);
}

static UCS_F_ALWAYS_INLINE int
uct_rm_urma_ep_conn_hash_equal(uct_rm_urma_remote_info_t info1,
                               uct_rm_urma_remote_info_t info2)
{
    return (info1.jfr_id == info2.jfr_id) &&
           (info1.eid.in4.addr == info2.eid.in4.addr) &&
           (info1.uasid == info2.uasid);
}

KHASH_TYPE(uct_rm_urma_ep_conn_hash, uct_rm_urma_remote_info_t, uct_rm_urma_ep_conn_entry_t);
KHASH_IMPL(uct_rm_urma_ep_conn_hash, uct_rm_urma_remote_info_t, uct_rm_urma_ep_conn_entry_t, 1,
           uct_rm_urma_ep_conn_hash_func, uct_rm_urma_ep_conn_hash_equal);

/* jfs advise hash, use jfs_id as the key and jfr information as value of the hash table */
typedef struct uct_rm_urma_link_hash_entry {
    int32_t    ref_cnt;
} uct_rm_urma_link_hash_entry_t;

typedef struct uct_rm_urma_link_hash_key {
    uint32_t   jfs_id;
    uint32_t   jfr_id;
} uct_rm_urma_link_hash_key_t;

static UCS_F_ALWAYS_INLINE
khint32_t uct_rm_urma_link_hash_func(uct_rm_urma_link_hash_key_t info)
{
    return kh_int_hash_func(info.jfs_id | info.jfr_id);
}

static UCS_F_ALWAYS_INLINE int
uct_rm_urma_link_hash_equal(uct_rm_urma_link_hash_key_t info1, uct_rm_urma_link_hash_key_t info2)
{
    return (info1.jfs_id == info2.jfs_id) && (info1.jfr_id == info2.jfr_id);
}

KHASH_TYPE(uct_rm_urma_link_hash, uct_rm_urma_link_hash_key_t, uct_rm_urma_link_hash_entry_t);
KHASH_IMPL(uct_rm_urma_link_hash, uct_rm_urma_link_hash_key_t, uct_rm_urma_link_hash_entry_t, 1,
           uct_rm_urma_link_hash_func, uct_rm_urma_link_hash_equal);

/**
 * ub async event state.
 */
typedef struct {
    unsigned                  flag;             /* Event happened */
    uct_ub_async_event_wait_t *wait_ctx;        /* Waiting context */
} uct_ub_async_event_val_t;

KHASH_TYPE(uct_ub_async_event, uct_ub_async_event_t, uct_ub_async_event_val_t);

/**
 * ub device (corresponds to HCA)
 */
typedef struct uct_ub_device {
    urma_context_t                     *urma_ctx;       /* Urma context */
    urma_token_t                       token;
    urma_device_attr_t                 dev_attr;        /* Urma dev attr */
    uint8_t                            first_port;      /* Number of first port (usually 1) */
    uint8_t                            num_ports;       /* Amount of physical ports */
    ucs_sys_cpuset_t                   local_cpus;      /* CPUs local to device */
    int                                numa_node;       /* NUMA node of the device */
    int                                async_events;    /* Whether async events are handled */
    int                                max_zcopy_log_sge; /* Maximum sges log for zcopy am */
    UCS_STATS_NODE_DECLARE(stats)
    uct_ub_pci_id_t                    pci_id;
    unsigned                           flags;
    char                               dev_name[UCT_DEVICE_NAME_MAX];
    /* Async event subscrubers */
    ucs_spinlock_t                     async_event_lock;
    khash_t(uct_ub_async_event)        async_events_hash;
    khash_t(uct_rm_urma_ep_conn_hash)  *ep_hash;
    khash_t(uct_rm_urma_link_hash)     *link_hash; /* Avoid repeatedly calling urma_advise_jfr to build a link */
} uct_ub_device_t;

/**
 * Check if a port on a device is active and supports the given flags.
 */
ucs_status_t uct_ub_device_port_check(uct_ub_device_t *dev, uint8_t port_num,
                                      unsigned flags);

/*
 * Helper function to list ub transport resources.
 *
 * @param dev              ub device.
 * @param flags            Transport requirements from ub device (see UCT_ub_RESOURCE_FLAG_xx)
 * @param devices_p        Filled with a pointer to an array of devices.
 * @param num_devices_p    Filled with the number of devices.
 */
ucs_status_t uct_ub_device_query_ports(uct_ub_device_t *dev, unsigned flags,
                                       uct_tl_device_resource_t **devices_p,
                                       unsigned *num_devices_p);

ucs_status_t uct_ub_device_query(urma_device_t *urma_dev, uct_ub_device_t *dev);

ucs_status_t uct_ub_device_init(uct_ub_device_t *dev, const char *md_name,
                                int async_events
                                UCS_STATS_ARG(ucs_stats_node_t *stats_parent));

void uct_ub_device_cleanup(uct_ub_device_t *dev);


/**
 * @return device specification.
 */
const uct_ub_device_spec_t* uct_ub_device_spec(uct_ub_device_t *dev);

void uct_ub_handle_async_event(uct_ub_device_t *dev, uct_ub_async_event_t *event);

ucs_status_t uct_ub_device_find_port(uct_ub_device_t *dev, const char *resource_dev_name, uint8_t *p_port_num);

size_t uct_ub_mtu_value(urma_mtu_t mtu);

ucs_status_t uct_ub_device_get_ndev_name(uct_ub_device_t *dev, char *ndev_name, size_t max);

sa_family_t uct_ub_device_get_addr_family(urma_eid_t *eid);
#ifdef __cplusplus
}
#endif

#endif

