/**
 * Copyright (c) UT-Battelle, LLC. 2014-2015. ALL RIGHTS RESERVED.
 * Copyright (C) Mellanox Technologies Ltd. 2001-2015.  ALL RIGHTS RESERVED.
 * Copyright (C) ARM Ltd. 2016.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCT_MM_IFACE_H
#define UCT_MM_IFACE_H

#include "mm_md.h"

#include <uct/base/uct_iface.h>
#include <uct/sm/base/sm_iface.h>
#include <ucs/arch/cpu.h>
#include <ucs/debug/memtrack_int.h>
#include <ucs/datastruct/arbiter.h>
#include <ucs/sys/compiler.h>
#include <ucs/sys/sys.h>
#include <sys/shm.h>
#include <sys/un.h>


enum {
    /* FIFO element polarity, changes every cycle to indicate the element is
       written by the sender */
    UCT_MM_FIFO_ELEM_FLAG_OWNER  = UCS_BIT(0),

    /* Whether the element data is inline or in receive descriptor */
    UCT_MM_FIFO_ELEM_FLAG_INLINE = UCS_BIT(1),
};


#define UCT_MM_FIFO_CTL_SIZE \
    ucs_align_up(sizeof(uct_mm_fifo_ctl_t), UCS_SYS_CACHE_LINE_SIZE)


#define UCT_MM_GET_FIFO_SIZE(_iface) \
    (UCT_MM_FIFO_CTL_SIZE + \
     ((_iface)->config.fifo_size * (_iface)->config.fifo_elem_size) + \
      (UCS_SYS_CACHE_LINE_SIZE - 1))


#define UCT_MM_IFACE_GET_FIFO_ELEM(_iface, _fifo, _index) \
    ((uct_mm_fifo_element_t*) \
     UCS_PTR_BYTE_OFFSET(_fifo, (_index) * (_iface)->config.fifo_elem_size))


#define uct_mm_iface_mapper_call(_iface, _func, ...) \
    ({ \
        uct_mm_md_t *md = ucs_derived_of((_iface)->super.super.md, uct_mm_md_t); \
        uct_mm_md_mapper_call(md, _func, ## __VA_ARGS__); \
    })


#define uct_mm_iface_trace_am(_iface, _type, _flags, _am_id, _data, _length, \
                              _elem_sn) \
    uct_iface_trace_am(&(_iface)->super.super, _type, _am_id, _data, _length, \
                       "%cX [%lu] %c%c", \
                       ((_type) == UCT_AM_TRACE_TYPE_RECV) ? 'R' : \
                       ((_type) == UCT_AM_TRACE_TYPE_SEND) ? 'T' : \
                                                             '?', \
                       (_elem_sn), \
                       ((_flags) & UCT_MM_FIFO_ELEM_FLAG_OWNER) ? 'o' : '-', \
                       ((_flags) & UCT_MM_FIFO_ELEM_FLAG_INLINE) ? 'i' : '-')


/* AIMD (additive increase/multiplicative decrease) algorithm adopted for FIFO
 * polling mechanism to adjust FIFO polling window.
 * - FIFO window is increased if the number of completed RX operations during
 *   the current iface progress call reaches FIFO window size and previous iface
 *   progress call was able to fully consume FIFO window (protection against
 *   impacting ping-pong pattern where handling of > 1 RX operation should not
 *   be expected).
 * - FIFO window is decreased if the number of completed RX operations during
 *   the current iface progress call does not reach FIFO window size.
 * See https://en.wikipedia.org/wiki/Additive_increase/multiplicative_decrease
 * for more information about original AIMD algorithm used for congestion
 * avoidance. */
#define UCT_MM_IFACE_FIFO_MIN_POLL              1 /* Minimal FIFO window size */
#define UCT_MM_IFACE_FIFO_MAX_POLL             16 /* Default value for FIFO maximal
                                                   * window size */
#define UCT_MM_IFACE_FIFO_AI_VALUE              1 /* FIFO window += AI value */
#define UCT_MM_IFACE_FIFO_MD_FACTOR             2 /* FIFO window /= MD factor */

/* If this bit is set in fifo_ctl.head, trigger async event on the receiver  */
#define UCT_MM_IFACE_FIFO_HEAD_EVENT_ARMED      UCS_BIT(63)


/**
 * MM interface configuration
 */
typedef struct uct_mm_iface_config {
    uct_sm_iface_config_t    super;
    size_t                   seg_size;            /* Size of the receive
                                                   * descriptor (for payload) */
    unsigned                 fifo_size;           /* Size of the receive FIFO */
    size_t                   fifo_max_poll;       /* Maximal RX completions to pick
                                                   * during RX poll */
    double                   release_fifo_factor; /* Tail index update frequency */
    ucs_ternary_auto_value_t hugetlb_mode;        /* Enable using huge pages for
                                                   * shared memory buffers */
    unsigned                 fifo_elem_size;      /* Size of the FIFO element size */
    int                      error_handling; /* Exposing of error handling cap */
    uct_iface_mpool_config_t mp;
} uct_mm_iface_config_t;


/**
 * MM interface address
 */
typedef struct uct_mm_iface_addr {
    uct_mm_seg_id_t          fifo_seg_id;     /* Shared memory identifier of FIFO */
    /* mapper-specific iface address follows */
} UCS_S_PACKED uct_mm_iface_addr_t;


/**
 * MM FIFO control segment
 */
typedef struct uct_mm_fifo_ctl {
    /* 1st cacheline */
    volatile uint64_t         head;           /* Where to write next */
    socklen_t                 signal_addrlen; /* Address length of signaling socket */
    struct sockaddr_un        signal_sockaddr;/* Address of signaling socket */
    UCS_CACHELINE_PADDING(uint64_t,
                          socklen_t,
                          struct sockaddr_un);

    /* 2nd cacheline */
    volatile uint64_t         tail;           /* How much was consumed */
    pid_t                     pid;            /* Process owner pid */
} UCS_S_PACKED UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE) uct_mm_fifo_ctl_t;


/**
 * MM receive descriptor info in the shared FIFO
 */
typedef struct uct_mm_desc_info {
    uct_mm_seg_id_t         seg_id;           /* shared memory segment id */
    unsigned                seg_size;         /* size of the shared memory segment */
    unsigned                offset;           /* offset inside the shared memory
                                                 segment */
} UCS_S_PACKED uct_mm_desc_info_t;


/**
 * MM FIFO element
 */
typedef struct uct_mm_fifo_element {
    volatile uint8_t          flags;          /* UCT_MM_FIFO_ELEM_FLAG_xx */
    uint8_t                   am_id;          /* active message id */
    uint16_t                  length;         /* length of actual data written
                                                 by producer */
    uct_mm_desc_info_t        desc;           /* remote receive descriptor
                                                 parameters for am_bcopy */
    void                      *desc_data;     /* pointer to receive descriptor,
                                                 valid only on receiver */

    /* the data follows here (in case of inline messaging) */
} UCS_S_PACKED uct_mm_fifo_element_t;


/*
 * MM receive descriptor:
 *
 * +--------------------+---------------+-----------+
 * | uct_mm_recv_desc_t | user-defined  | data      |
 * | (info + rdesc)     | rx headroom   | (payload) |
 * +--------------------+---------------+-----------+
 */
typedef struct uct_mm_recv_desc {
    uct_mm_desc_info_t        info;           /* descriptor information for the
                                                 remote side which writes to it */
    uct_recv_desc_t           recv;           /* has to be in the end */
} uct_mm_recv_desc_t;


typedef struct uct_mm_fifo_check {
    int                      is_flags_cached;
    uint8_t                  flags_cache;
    uint8_t                  fifo_shift;  /* = log2(fifo_size) */
    uint64_t                 read_index;  /* actual reading location */
    uct_mm_fifo_element_t   *read_elem;
    uct_mm_fifo_ctl_t       *fifo_ctl;    /* pointer to the struct at the
                                           * beginning of the receive fifo
                                           * which holds the head and the tail.
                                           * this struct is cache line aligned
                                           * and doesn't necessarily start where
                                           * shared_mem starts. */

    uint64_t                 fifo_release_factor_mask;
} uct_mm_fifo_check_t;


/**
 * MM trandport interface
 */
typedef struct uct_mm_base_iface {
    uct_sm_iface_t          super;

    /* Receive FIFO */
    uct_allocated_memory_t  recv_fifo_mem;
    uct_mm_fifo_check_t     recv_check;
    void                    *recv_fifo_elems; /* pointer to the first fifo element
                                                 in the receive fifo */


    unsigned                fifo_mask;        /* = 2^fifo_shift - 1 */

    unsigned                fifo_poll_count;     /* How much RX operations can be polled
                                                  * during an iface progress call */
    int                     fifo_prev_wnd_cons;  /* Was FIFO window size fully consumed by
                                                  * the previous call to iface progress */

    ucs_mpool_t             recv_desc_mp;
    uct_mm_recv_desc_t      *last_recv_desc;  /* next receive descriptor to use */

    int                     signal_fd;        /* Unix socket for receiving remote signal */

    size_t                  rx_headroom;
    ucs_arbiter_t           arbiter;
    uct_recv_desc_t         release_desc;

    struct {
        unsigned            fifo_size;
        unsigned            fifo_elem_size;
        unsigned            seg_size;         /* size of the receive descriptor (for payload)*/
        unsigned            fifo_max_poll;
        uint64_t            extra_cap_flags;
    } config;
} uct_mm_base_iface_t;

typedef struct uct_mm_iface {
    uct_mm_base_iface_t super;
} uct_mm_iface_t;

/*
 * Define a memory-mapper transport for MM.
 *
 * @param _name         Component name token
 * @param _md_ops       Memory domain operations, of type uct_mm_md_ops_t
 * @param _rkey_unpack  Remote key unpack function
 * @param _rkey_release Remote key release function
 * @param _cfg_prefix   Prefix for configuration variables
 * @param _cfg_table    Configuration table
 */
#define UCT_MM_TL_DEFINE(_name, _md_ops, _rkey_unpack, _rkey_release, \
                         _cfg_prefix, _cfg_table, _tl_suffix) \
    \
    UCT_MM_COMPONENT_DEFINE(uct_##_name##_tl_suffix##_component, \
            _name, _tl_suffix, _md_ops, _rkey_unpack, _rkey_release, _cfg_prefix) \
    \
    UCT_TL_DEFINE(&(uct_##_name##_tl_suffix##_component).super, \
                  _name##_tl_suffix, \
                  uct_sm_base_query_tl_devices, \
                  uct_mm_iface_t, \
                  _cfg_prefix, \
                  uct_mm##_tl_suffix##iface_t, \
                  "MM_" _cfg_prefix, \
                  _cfg_table, \
                  uct_mm_iface_config_t);


extern ucs_config_field_t uct_mm_iface_config_table[];

static UCS_F_ALWAYS_INLINE int
uct_mm_iface_fifo_flag_has_new_data(uint8_t flags,
                                    uint64_t read_index,
                                    uint8_t fifo_shift)
{
    return (((read_index >> fifo_shift) & 1) !=
            (UCT_MM_FIFO_ELEM_FLAG_OWNER & flags));
}

static UCS_F_ALWAYS_INLINE int
uct_mm_iface_fifo_has_new_data(uct_mm_fifo_check_t *check_info)
{
    /* check the flags_cache to see if anything changed */
    uint8_t flags = check_info->read_elem->flags;
    if (ucs_likely((check_info->is_flags_cached) &&
                   (check_info->flags_cache == flags))) {
        return 0;
    }

    /* check the read_index to see if there is a new item to read (checking the owner bit) */
    if (uct_mm_iface_fifo_flag_has_new_data(flags,
                                            check_info->read_index,
                                            check_info->fifo_shift)) {
        check_info->is_flags_cached = 1;
        check_info->flags_cache     = flags;
        return 0;
    }

    ucs_memory_cpu_load_fence();
    return 1;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
uct_mm_iface_invoke_am(uct_mm_base_iface_t *iface, uint8_t am_id, void *data,
                       unsigned length, unsigned flags)
{
    ucs_status_t status;
    void         *desc;

    status = uct_iface_invoke_am(&iface->super.super, am_id, data, length,
                                 flags);

    if (status == UCS_INPROGRESS) {
        desc = (void *)((uintptr_t)data - iface->rx_headroom);
        /* save the release_desc for later release of this desc */
        uct_recv_desc(desc) = &iface->release_desc;
    }

    return status;
}

static UCS_F_ALWAYS_INLINE void
uct_mm_iface_fifo_window_adjust(uct_mm_base_iface_t *iface,
                                unsigned fifo_poll_count)
{
    if (fifo_poll_count < iface->fifo_poll_count) {
        iface->fifo_poll_count = ucs_max(iface->fifo_poll_count /
                                         UCT_MM_IFACE_FIFO_MD_FACTOR,
                                         UCT_MM_IFACE_FIFO_MIN_POLL);
        iface->fifo_prev_wnd_cons = 0;
        return;
    }

    ucs_assert(fifo_poll_count == iface->fifo_poll_count);

    if (iface->fifo_prev_wnd_cons) {
        /* Increase FIFO window size if it was fully consumed
         * during the previous iface progress call in order
         * to prevent the situation when the window will be
         * adjusted to [MIN, MIN + 1, MIN, MIN + 1, ...] that
         * is harmful to latency */
        iface->fifo_poll_count = ucs_min(iface->fifo_poll_count +
                                         UCT_MM_IFACE_FIFO_AI_VALUE,
                                         iface->config.fifo_max_poll);
    } else {
        iface->fifo_prev_wnd_cons = 1;
    }
}

static UCS_F_ALWAYS_INLINE void
uct_mm_progress_fifo_tail(uct_mm_fifo_check_t *recv_check)
{
    ucs_prefetch(recv_check->read_elem);

    recv_check->is_flags_cached = 0;

    /* don't progress the tail every time - release in batches. improves performance */
    if (recv_check->read_index & recv_check->fifo_release_factor_mask) {
        return;
    }

    recv_check->fifo_ctl->tail  = recv_check->read_index;
}


/**
 * Set aligned pointers of the FIFO according to the beginning of the allocated
 * memory.
 * @param [in] fifo_mem      Pointer to the beginning of the allocated memory.
 * @param [out] fifo_ctl_p   Pointer to the FIFO control structure.
 * @param [out] fifo_elems   Pointer to the array of FIFO elements.
 */
void uct_mm_iface_set_fifo_ptrs(void *fifo_mem, uct_mm_fifo_ctl_t **fifo_ctl_p,
                                void **fifo_elems_p);

UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_iface_t, uct_iface_t, uct_md_h, uct_worker_h,
                           const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE(uct_mm_base_iface_t, uct_iface_ops_t*, uct_md_h, uct_worker_h,
                  const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE(uct_mm_iface_t, uct_md_h, uct_worker_h,
                  const uct_iface_params_t*, const uct_iface_config_t*);

ucs_status_t uct_mm_assign_desc_to_fifo_elem(uct_mm_base_iface_t *iface,
                                             uct_mm_fifo_element_t *elem,
                                             unsigned need_new_desc);

void uct_mm_iface_release_desc(uct_recv_desc_t *self, void *desc);

ucs_status_t uct_mm_flush();

ucs_status_t uct_mm_iface_flush(uct_iface_h tl_iface, unsigned flags,
                                uct_completion_t *comp);

ucs_status_t uct_mm_iface_query(uct_iface_h tl_iface,
                                uct_iface_attr_t *iface_attr);

ucs_status_t uct_mm_iface_get_address(uct_iface_t *tl_iface,
                                      uct_iface_addr_t *addr);

int uct_mm_iface_is_reachable(const uct_iface_h tl_iface,
                              const uct_device_addr_t *dev_addr,
                              const uct_iface_addr_t *tl_iface_addr);

unsigned uct_mm_iface_progress(uct_iface_h tl_iface);

ucs_status_t uct_mm_iface_event_fd_get(uct_iface_h tl_iface, int *fd_p);

ucs_status_t uct_mm_iface_event_fd_arm(uct_iface_h tl_iface, unsigned events);

#endif
