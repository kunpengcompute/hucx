#ifndef UD_MCAST_VERBS_H
#define UD_MCAST_VERBS_H

#include "ud_verbs.h"

#include <uct/ib/ud/base/ud_iface.h>
#include <uct/ib/ud/base/ud_ep.h>

typedef struct {
    uct_ud_verbs_ep_t super;
} uct_ud_mcast_verbs_ep_t;

typedef struct uct_ud_mcast_iface_addr {
    uct_ud_iface_addr_t    super;
    uint32_t               coll_id;
    union ibv_gid          mgid;
} uct_ud_mcast_iface_addr_t;


typedef struct uct_ud_mcast_verbs_ep_addr {
    uct_ud_ep_addr_t super;
    union ibv_gid    mgid;
} uct_ud_mcast_verbs_ep_addr_t;

struct uct_ud_iface_addr_t {
    uct_ud_iface_addr_t iface_addr;
    uct_ib_uint24_t     ep_id;
};

typedef struct {
    uct_ud_verbs_iface_t super;
    int                 listen_fd;
    uint32_t            coll_id;
    uint32_t            coll_cnt;
    union ibv_gid       mgid;
    int id;
    uint8_t      num_of_peers;      /* number of remote eps connected to this ep */
    uct_ud_psn_t *acked_psn_by_src; /* array of size num_of_peers - each cell represents last acked_psn from remote epid */

} uct_ud_mcast_verbs_iface_t;

#endif