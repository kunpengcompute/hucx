/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_test.h"

#include "ucp/api/ucp.h"

extern "C" {
#include <ucs/stats/stats.h>
}

std::ostream& operator<<(std::ostream& os, const ucg_test_param& test_param)
{
    std::vector<std::string>::const_iterator iter;
    const std::vector<std::string>& planners = test_param.planners;
    for (iter = planners.begin(); iter != planners.end(); ++iter) {
        if (iter != planners.begin()) {
            os << ",";
        }
        os << *iter;
    }
    return os;
}

ucg_test::ucg_test() {
    ucs_status_t status;
    status = ucg_config_read(NULL, NULL, &m_ucg_config);
    ASSERT_UCS_OK(status);
}

ucg_test::~ucg_test() {

    for (ucs::ptr_vector<rank>::const_iterator iter = comm().begin();
         iter != comm().end(); ++iter)
    {
        (*iter)->warn_existing_groups();
    }
    ucg_config_release(m_ucg_config);
}

void ucg_test::cleanup() {
    m_comm.clear();
}

void ucg_test::init() {
    test_base::init();

    create_comm();
}

static bool check_planner(const std::string check_planner_name,
                          const std::vector<std::string>& planner_names) {
    return (std::find(planner_names.begin(), planner_names.end(),
                      check_planner_name) != planner_names.end());
}

bool ucg_test::has_planner(const std::string& planner_name) const {
    return check_planner(planner_name, GetParam().planners);
}

bool ucg_test::has_any_planner(const std::vector<std::string>& planner_names) const {
    const std::vector<std::string>& all_planner_names = GetParam().planners;

    return std::find_first_of(all_planner_names.begin(), all_planner_names.end(),
                              planner_names.begin(),     planner_names.end()) !=
                              all_planner_names.end();
}

ucg_test_base::rank* ucg_test::create_comm(bool add_in_front) {
    return create_comm(add_in_front, GetParam());
}

ucg_test_base::rank*
ucg_test::create_comm(bool add_in_front, const ucg_test_param &test_param) {
    rank *e = new rank(test_param, m_ucg_config, get_worker_params(), this);
    if (add_in_front) {
        m_comm.push_front(e);
    } else {
        m_comm.push_back(e);
    }
    return e;
}

ucg_params_t ucg_test::get_ucg_ctx_params() {
    ucg_params_t params = {0};

    params.completion.coll_comp_cb_f     = NULL;
    params.completion.comp_flag_offset   = offsetof(ucg_request_t, is_complete);
    params.completion.comp_status_offset = offsetof(ucg_request_t, status);

    return params;
}

ucp_params_t ucg_test::get_ucp_ctx_params() {
    ucp_params_t params = {0};

    params.field_mask = UCP_PARAM_FIELD_FEATURES;
    params.features   = UCP_FEATURE_GROUPS;

    return params;
}

ucp_worker_params_t ucg_test::get_worker_params() {
    ucp_worker_params_t params = {0};

    params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    params.thread_mode = UCS_THREAD_MODE_MULTI;

    return params;
}

ucg_group_params_t ucg_test::get_group_params(ucg_group_member_index_t index) {
    ucg_group_params_t params = {0};

    params.field_mask   = UCG_GROUP_PARAM_FIELD_MEMBER_COUNT |
                          UCG_GROUP_PARAM_FIELD_MEMBER_INDEX |
                          UCG_GROUP_PARAM_FIELD_CB_CONTEXT;
    params.member_count = m_comm.size();
    params.member_index = index;
    params.cb_context   = this;

    return params;
}

unsigned ucg_test::worker_progress() const {
    unsigned count = 0;
    for (ucs::ptr_vector<rank>::const_iterator iter = comm().begin();
         iter != comm().end(); ++iter)
    {
        count += (*iter)->worker_progress();
        sched_yield();
    }
    return count;
}

void ucg_test::set_ucg_config(ucg_config_t *config) {
    set_ucg_config(config, GetParam());
}

std::vector<ucg_test_param>
ucg_test::enum_test_params(const ucg_params_t& ucg_params,
                           const ucp_params_t& ucp_params,
                           const std::string& name,
                           const std::string& test_case_name,
                           const std::string& planners)
{
    ucg_test_param test_param;
    std::stringstream ss(planners);

    test_param.ctx_params.ucg       = ucg_params;
    test_param.ctx_params.ucp       = ucp_params;

    while (ss.good()) {
        std::string planner_name;
        std::getline(ss, planner_name, ',');
        test_param.planners.push_back(planner_name);
    }

    if (check_test_param(name, test_case_name, test_param)) {
        return std::vector<ucg_test_param>(1, test_param);
    } else {
        return std::vector<ucg_test_param>();
    }
}

void ucg_test::generate_test_params_variant(const ucg_params_t& ucg_params,
                                            const ucp_params_t& ucp_params,
                                            const std::string& name,
                                            const std::string& test_case_name,
                                            const std::string& planners,
                                            ucg_group_member_index_t group_size,
                                            std::vector<ucg_test_param>& test_params)
{
    std::vector<ucg_test_param> tmp_test_params;

    tmp_test_params = ucg_test::enum_test_params(ucg_params, ucp_params, name,
                                                 test_case_name, planners);
    for (std::vector<ucg_test_param>::iterator iter = tmp_test_params.begin();
         iter != tmp_test_params.end(); ++iter)
    {
        iter->group_size = group_size;
        test_params.push_back(*iter);
    }
}

void ucg_test::set_ucg_config(ucg_config_t *config,
                              const ucg_test_param& test_param)
{
    std::stringstream ss;
    ss << test_param;
    ucg_config_modify(config, "PLANNERS", ss.str().c_str());
    /* prevent configuration warnings in the UCP testing */
    ucg_config_modify(config, "WARN_INVALID_CONFIG", "no");
}

void ucg_test::modify_config(const std::string& name, const std::string& value,
                             modify_config_mode_t mode)
{
    ucs_status_t status;

    status = ucg_config_modify(m_ucg_config, name.c_str(), value.c_str());
    if (status == UCS_ERR_NO_ELEM) {
        test_base::modify_config(name, value, mode);
    } else if (status != UCS_OK) {
        UCS_TEST_ABORT("Couldn't modify ucp config parameter: " <<
                        name.c_str() << " to " << value.c_str() << ": " <<
                        ucs_status_string(status));
    }
}

void ucg_test::stats_activate()
{
    ucs_stats_cleanup();
    push_config();
    modify_config("STATS_DEST",    "file:/dev/null");
    modify_config("STATS_TRIGGER", "exit");
    ucs_stats_init();
    ASSERT_TRUE(ucs_stats_is_active());
}

void ucg_test::stats_restore()
{
    ucs_stats_cleanup();
    pop_config();
    ucs_stats_init();
}


bool ucg_test::check_test_param(const std::string& name,
                                const std::string& test_case_name,
                                const ucg_test_param& test_param)
{
    typedef std::map<std::string, bool> cache_t;
    static cache_t cache;

    if (test_param.planners.empty()) {
        return false;
    }

    cache_t::iterator iter = cache.find(name);
    if (iter != cache.end()) {
        return iter->second;
    }

    ucs::handle<ucg_config_t*> config;
    UCS_TEST_CREATE_HANDLE(ucg_config_t*, config, ucg_config_release,
                           ucg_config_read, NULL, NULL);
    set_ucg_config(config, test_param);

    ucg_context_h ucgh;
    ucs_status_t status;
    {
        scoped_log_handler slh(hide_errors_logger);
        status = ucg_init(&test_param.ctx_params.ucg, config, &ucgh);
    }

    bool result;
    if (status == UCS_OK) {
        ucg_cleanup(ucgh);
        result = true;
    } else if (status == UCS_ERR_NO_DEVICE) {
        result = false;
    } else {
        UCS_TEST_ABORT("Failed to create context (" << test_case_name << "): "
                       << ucs_status_string(status));
    }

    UCS_TEST_MESSAGE << "checking " << name << ": " << (result ? "yes" : "no");
    cache[name] = result;
    return result;
}

static ucs_status_t ucg_worker_create(ucg_context_h context,
                                      const ucp_worker_params_t *params,
                                      ucp_worker_h *worker_p)
{
    return ucp_worker_create(ucg_context_get_ucp(context), params, worker_p);
}

ucg_test_base::rank::rank(const ucg_test_param& test_param,
                          ucg_config_t* ucg_config,
                          const ucp_worker_params_t& worker_params,
                          const ucg_test_base *test_owner)
{
    ucg_test_param comm_param = test_param;
    ucp_worker_params_t local_worker_params = worker_params;
    int num_workers;

    if (test_param.thread_type == MULTI_THREAD_CONTEXT) {
        num_workers = MT_TEST_NUM_THREADS;
        comm_param.ctx_params.ucp.mt_workers_shared = 1;
        local_worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;
    } else if (test_param.thread_type == MULTI_THREAD_WORKER) {
        num_workers = 1;
        comm_param.ctx_params.ucp.mt_workers_shared = 0;
        local_worker_params.thread_mode = UCS_THREAD_MODE_MULTI;
    } else {
        num_workers = 1;
        comm_param.ctx_params.ucp.mt_workers_shared = 0;
        local_worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;
    }

    comm_param.ctx_params.ucp.field_mask |= UCP_PARAM_FIELD_MT_WORKERS_SHARED;
    local_worker_params.field_mask       |= UCP_WORKER_PARAM_FIELD_THREAD_MODE;

    ucg_test::set_ucg_config(ucg_config, comm_param);

    {
        scoped_log_handler slh(hide_errors_logger);
        UCS_TEST_CREATE_HANDLE_IF_SUPPORTED(ucg_context_h, m_ucgh, ucg_cleanup,
                                            ucg_init, &comm_param.ctx_params.ucg,
                                            ucg_config);
    }

    for (int i = 0; i < num_workers; i++) {
        UCS_TEST_CREATE_HANDLE(ucp_worker_h, m_worker,
                               ucp_worker_destroy, ucg_worker_create, m_ucgh,
                               &local_worker_params);
    }
}

ucg_test_base::rank::~rank() {
    m_worker.reset();
}
/*
static int resolve_cb() {
    ucs_status_t status;
    ucp_address_t *address;
    size_t address_length;
    ucp_ep_h ep;
    status = ucp_worker_get_address(other->worker(i), &address, &address_length);
    ASSERT_UCS_OK(status);
    {
        scoped_log_handler slh(hide_errors_logger);
        ucp_ep_params_t local_ep_params = ep_params;
        local_ep_params.field_mask |= UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
        local_ep_params.address     = address;
        status = ucp_ep_create(m_workers[i].first, &local_ep_params, &ep);
    }
    if (status == UCS_ERR_UNREACHABLE) {
        ucp_worker_release_address(other->worker(i), address);
        UCS_TEST_SKIP_R(m_errors.empty() ? "Unreachable" : m_errors.back());
    }
    ASSERT_UCS_OK(status, << " (" << m_errors.back() << ")");
}
static int release_cb() {
    ucp_worker_release_address(other->worker(i), address);
}
*/
void ucg_test_base::rank::groupify(const ucs::ptr_vector<rank>& ranks,
                                   const ucg_group_params_t& group_params,
                                   int group_idx, int do_set_group) {
    ucg_group_h group;
    ucg_group_create(m_worker.get(), &group_params, &group);

    if (do_set_group) {
        set_group(group, group_idx);
    }
}

void ucg_test_base::rank::set_group(ucg_group_h group, int group_index) {
    // TODO: use group_index...
    m_groups.push_back(ucs::handle<ucg_group_h, rank*>(group, ucg_group_destroy));
}

ucg_group_h ucg_test_base::rank::group(int group_index) const {
    return m_groups[group_index];
}

ucp_worker_h ucg_test_base::rank::worker() const {
    return m_worker;
}

ucg_context_h ucg_test_base::rank::ucgh() const {
    return m_ucgh;
}

unsigned ucg_test_base::rank::worker_progress()
{
    return ucp_worker_progress(worker());
}

int ucg_test_base::rank::get_num_groups() const {
    return m_groups.size();
}

void ucg_test_base::rank::warn_existing_groups() const {
    for (size_t group_index = 0; group_index < m_groups.size(); ++group_index) {
        ADD_FAILURE() << "group(" << group_index <<
                         ")=" << m_groups[group_index].get() <<
                         " was not destroyed during test cleanup()";
    }
}

bool ucg_test_base::is_request_completed(ucg_request_t *request) {
    return (bool)request->is_complete;
}


test_ucg_group_base::test_ucg_group_base(unsigned nodes,
                                         unsigned ppn,
                                         ucg_group_member_index_t my_rank)
{
    // TODO: generate info by <nodes, ppn> to pass during worker creation!
    create_comms(nodes * ppn);

    ucg_test_base::rank& me = get_rank(my_rank);
    m_worker = me.worker();
    m_group  = me.group();

    m_group_params = get_group_params(my_rank);

    //    m_planc = NULL;
    //    m_all_rank_infos.clear();
    //    m_resource_factory->create_balanced_rank_info(m_all_rank_infos, node_cnt, ppn);
    //    m_group_params = m_resource_factory->create_group_params(m_all_rank_infos[myrank], m_all_rank_infos);
    //    m_group = m_resource_factory->create_group(m_group_params, m_ucg_worker);
    //    m_coll_params = m_resource_factory->create_collective_params(UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE,
    //                                                                 0, NULL, 1, NULL, 4, NULL, NULL);
    //    m_coll_type.modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE;
    //    m_coll_type.root = 0;
    //    ucg_plan_select(m_group, NULL, m_coll_params, &m_planc);
    //    m_builtin_ctx = (ucg_builtin_group_ctx_t *)UCG_GROUP_TO_COMPONENT_CTX(ucg_builtin_component, m_group);
}

test_ucg_group_base::~test_ucg_group_base() {

}

void test_ucg_group_base::create_comms(ucg_group_member_index_t group_size) {
    ucg_group_member_index_t member_idx;
    for (member_idx = 0; member_idx < group_size; member_idx++) {
        get_rank(member_idx).groupify(comm(), get_group_params(member_idx));
    }
}



//using namespace std;
//
//static class ucg_resource_factory g_ucg_resource_factory;
//
//ucg_resource_factory *ucg_coll_test::m_resource_factory = &g_ucg_resource_factory;
//
//ucg_coll_test::ucg_coll_test()
//{
//    m_ucg_context = NULL;
//    m_ucg_worker = NULL;
//
//    init_ucg_component();
//}
//
//ucg_coll_test::~ucg_coll_test()
//{
//    for (ucs::ptr_vector<rank>::const_iterator iter = comm().begin();
//         iter != comm().end(); ++iter)
//    {
//        (*iter)->warn_existing_groups();
//    }
//
//    ucg_config_release(m_ucg_config);
//
//    if (m_ucg_worker != NULL) {
//        ucg_worker_destroy(m_ucg_worker);
//        m_ucg_worker = NULL;
//    }
//
//    if (m_ucg_context != NULL) {
//        ucg_cleanup(m_ucg_context);
//        m_ucg_context = NULL;
//    }
//}
//
//void ucg_coll_test::init_ucg_component()
//{
//    ucg_params_t params;
//    ucg_config_t *config = NULL;
//
//    /* Read options */
//    (void)ucg_config_read("MPI", NULL, &config);
//
//    /* Initialize UCX context */
//    params.field_mask = UCP_PARAM_FIELD_FEATURES |
//                        UCP_PARAM_FIELD_REQUEST_SIZE |
//                        UCP_PARAM_FIELD_REQUEST_INIT |
//                        UCP_PARAM_FIELD_REQUEST_CLEANUP |
//                        // UCP_PARAM_FIELD_TAG_SENDER_MASK |
//                        UCP_PARAM_FIELD_MT_WORKERS_SHARED |
//                        UCP_PARAM_FIELD_ESTIMATED_NUM_EPS;
//    params.features = UCP_FEATURE_TAG |
//                      UCP_FEATURE_RMA |
//                      UCP_FEATURE_AMO32 |
//                      UCP_FEATURE_AMO64 |
//                      UCP_FEATURE_GROUPS;
//    // params.request_size      = sizeof(ompi_request_t);
//    // params.request_init      = mca_coll_ucx_request_init;
//    // params.request_cleanup   = mca_coll_ucx_request_cleanup;
//    params.mt_workers_shared = 0; /* we do not need mt support for context
//                                     since it will be protected by worker */
//    params.estimated_num_eps = 0;
//
//    (void)ucg_init(&params, config, &m_ucg_context);
//
//    ucg_worker_params_t work_params;
//
//    /* TODO check MPI thread mode */
//    work_params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
//    work_params.thread_mode = UCS_THREAD_MODE_SINGLE;
//    work_params.thread_mode = UCS_THREAD_MODE_SINGLE;
//
//    (void)ucg_worker_create(m_ucg_context, &work_params, &m_ucg_worker);
//}
//
//ucg_collective_type_t ucg_coll_test::create_allreduce_coll_type() const {
//    ucg_collective_type_t type;
//    type.modifiers = (ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
//                                                 UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST);
//    type.root = 0;
//    return type;
//}
//
//ucg_collective_type_t ucg_coll_test::create_bcast_coll_type() const {
//    ucg_collective_type_t type;
//    type.modifiers = (ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
//                                                 UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE);
//    type.root = 0;
//    return type;
//}
//
//ucg_collective_type_t ucg_coll_test::create_barrier_coll_type() const {
//    ucg_collective_type_t type;
//    type.modifiers = (ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
//                                                 UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
//                                                 UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER);
//    type.root = 0;
//    return type;
//}
//
//ucg_collective_params_t *ucg_coll_test::create_allreduce_params() const {
//    size_t count = 2;
//    int *send_buf = new int[count];
//    int *recv_buf = new int[count];
//    for (size_t i = 0; i < count; i++) {
//        send_buf[i] = i;
//        recv_buf[i] = -1;
//    }
//
//    ucg_ompi_op *op = new ucg_ompi_op();
//    op->commutative = false;
//
//    return m_resource_factory->create_collective_params(create_allreduce_coll_type().modifiers,
//                                                        0, send_buf, count, recv_buf, sizeof(int), NULL, op);
//}
//
//ucg_collective_params_t *ucg_coll_test::create_bcast_params() const {
//    size_t count = 2;
//    int *send_buf = new int[count];
//    int *recv_buf = new int[count];
//    for (size_t i = 0; i < count; i++) {
//        send_buf[i] = i;
//        recv_buf[i] = -1;
//    }
//
//    return m_resource_factory->create_collective_params(create_bcast_coll_type().modifiers,
//                                                        0, send_buf, count, recv_buf, sizeof(int), NULL, NULL);
//}
//
//int mca_coll_ucg_datatype_convert_for_ut(void *mpi_dt, ucp_datatype_t *ucp_dt)
//{
//    if (mpi_dt != NULL) {
//        ucs_info("mca_coll_ucg_datatype_convert_for_ut");
//    }
//
//    *ucp_dt = UCP_DATATYPE_CONTIG;
//    return 0;
//}
//
//ucg_builtin_config_t *ucg_resource_factory::create_config(
//    unsigned bcast_alg, unsigned allreduce_alg, unsigned barrier_alg)
//{
//    ucg_builtin_config_t *config = new ucg_builtin_config_t;
//    config->super.ft = UCG_PLAN_FT_IGNORE;
//
//    config->bmtree.degree_inter_fanout = 8;
//    config->bmtree.degree_inter_fanin = 8;
//    config->bmtree.degree_intra_fanout = 2;
//    config->bmtree.degree_intra_fanin = 2;
//
//    config->recursive.factor = 2;
//
//    config->cache_size = 1000;
//    config->mem_reg_opt_cnt = 10;
//    config->bcopy_to_zcopy_opt = 1;
//    config->pipelining = 0;
//    config->short_max_tx = 200;
//    config->bcopy_max_tx = 32768;
//
//    config->bcast_algorithm = bcast_alg;
//    config->allreduce_algorithm = allreduce_alg;
//    config->barrier_algorithm = barrier_alg;
//
//    return config;
//}
//
//ucs_status_t resolve_address_callback(void *cb_group_obj, ucg_group_member_index_t index,
//                                      ucg_address_t **addr, size_t *addr_len)
//{
//    *addr_len = 0;
//    return UCS_OK;
//}
//
//static ucg_group_member_index_t mpi_global_idx_dummy(void *cb_group_obj, ucg_group_member_index_t index)
//{
//    return 0;
//}
//
//ucg_group_params_t *ucg_resource_factory::create_group_params(
//    ucg_rank_info my_rank_info, const std::vector<ucg_rank_info> &rank_infos)
//{
//    ucg_group_params_t *args = new ucg_group_params_t();
//    args->member_count = rank_infos.size();
//    args->cid = 0;
//    args->mpi_reduce_f = NULL;
//    args->resolve_address_f = &resolve_address_callback;
//    args->release_address_f = NULL;
//    args->cb_group_obj = NULL;
//    args->op_is_commute_f = ompi_op_is_commute;
//    args->mpi_dt_convert = mca_coll_ucg_datatype_convert_for_ut;
//    args->distance = (ucg_group_member_distance *) malloc(args->member_count * sizeof(*args->distance));
//    args->node_index = (uint16_t *) malloc(args->member_count * sizeof(*args->node_index));
//    args->mpi_global_idx_f = mpi_global_idx_dummy;
//
//    for (size_t i = 0; i < rank_infos.size(); i++) {
//        if (rank_infos[i].rank == my_rank_info.rank) {
//            args->distance[rank_infos[i].rank] = UCG_GROUP_MEMBER_DISTANCE_NONE;
//        } else if (rank_infos[i].nodex_idx == my_rank_info.nodex_idx) {
//            if (rank_infos[i].socket_idx == my_rank_info.socket_idx) {
//                args->distance[rank_infos[i].rank] = UCG_GROUP_MEMBER_DISTANCE_SOCKET;
//            } else {
//                args->distance[rank_infos[i].rank] = UCG_GROUP_MEMBER_DISTANCE_HOST;
//            }
//        } else {
//            args->distance[rank_infos[i].rank] = UCG_GROUP_MEMBER_DISTANCE_NET;
//        }
//
//        args->node_index[i] = rank_infos[i].nodex_idx;
//    }
//
//    return args;
//}
//
//ucg_group_h ucg_resource_factory::create_group(const ucg_group_params_t *params, ucp_worker_h ucg_worker)
//{
//    ucg_group_h group;
//    ucg_group_create(ucg_worker, params, &group);
//    return group;
//}
//
//ucg_collective_params_t *ucg_resource_factory::create_collective_params(
//    ucg_collective_modifiers modifiers, ucg_group_member_index_t root,
//    void *send_buffer, int count, void *recv_buffer, void *dtype, void *op_ext)
//{
//    ucg_collective_params_t *params = new ucg_collective_params_t();
//    params->send.type.modifiers = modifiers;
//    params->send.type.root  = root;
//    params->send.buffer = send_buffer;
//    params->send.count  = count;
//    params->send.dtype  = dtype;
//
//    params->recv.buffer = recv_buffer;
//    params->recv.count  = count;
//    params->recv.dtype  = dtype;
//    params->recv.op     = op_ext;
//
//    return params;
//}
//
//void ucg_resource_factory::create_balanced_rank_info(std::vector<ucg_rank_info> &rank_infos,
//                                                     size_t nodes, size_t ppn, bool map_by_socket)
//{
//    int rank = 0;
//    ucg_rank_info info;
//
//    for (size_t i = 0; i < nodes; i++) {
//        for (size_t j = 0; j < ppn; j++) {
//            info.rank = rank++;
//            info.nodex_idx = i;
//            if (map_by_socket) {
//                info.socket_idx = j < ppn / 2 ? 0 : 1;
//            } else {
//                info.socket_idx = 0;
//            }
//
//            rank_infos.push_back(info);
//        }
//    }
//}
//
//int ompi_op_is_commute(void *op)
//{
//    return (int) ((ucg_ompi_op *) op)->commutative;
//}
