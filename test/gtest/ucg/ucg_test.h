/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2014.  ALL RIGHTS RESERVED.
 * Copyright (C) Huawei Technologies Co., Ltd. 2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_TEST_H_
#define UCG_TEST_H_

#define __STDC_LIMIT_MACROS

#include <common/test.h>

#if _OPENMP
#include "omp.h"
#endif

#if _OPENMP && ENABLE_MT
#define MT_TEST_NUM_THREADS omp_get_max_threads()
#else
#define MT_TEST_NUM_THREADS 4
#endif

extern "C" {
#include <ucg/api/ucg.h>
}

/* UCG version compile time test */
#if (UCG_API_VERSION != UCG_VERSION(UCG_API_MAJOR,UCG_API_MINOR))
#error possible bug in UCG version
#endif

struct ucg_test_param {
    struct {
        ucp_params_t          ucp;
        ucg_params_t          ucg;
    } ctx_params;

    std::vector<std::string>  planners;

    int                       thread_type;
    ucg_group_member_index_t  group_size;
    int                       variant;
};

typedef struct ucg_request {
    uintptr_t    is_complete;
    ucs_status_t status;
} ucg_request_t;

class ucg_test; // forward declaration

template <typename T>
class commmunicator_storage : public ucs::entities_storage<T> {
public:
    const ucs::ptr_vector<T>& comm() const {
        return m_comm;
    }

    T& get_rank(size_t idx) {
        return m_comm.at(idx);
    }

    ucs::ptr_vector<T> m_comm;
};

class ucg_test_base : public ucs::test_base {
public:
    enum {
        SINGLE_THREAD = 7,
        MULTI_THREAD_CONTEXT, /* workers are single-threaded, context is mt-shared */
        MULTI_THREAD_WORKER   /* workers are multi-threaded, cotnext is mt-single */
    };

    class rank {
        typedef std::vector<ucs::handle<ucg_group_h, rank*>> group_vec_t;

    public:

        rank(const ucg_test_param& test_param, ucg_config_t* ucg_config,
             const ucp_worker_params_t& worker_params,
             const ucg_test_base* test_owner);

        ~rank();

        void groupify(const ucs::ptr_vector<rank>& ranks,
                      const ucg_group_params_t& group_params,
                      int group_idx = 0, int do_set_group = 1);

        ucg_group_h group(int group_index = 0) const;

        ucp_worker_h worker() const;

        ucg_context_h ucgh() const;

        int get_num_groups() const;

        unsigned worker_progress();

        void warn_existing_groups() const;

    protected:
        ucs::handle<ucg_context_h>      m_ucgh;
        ucs::handle<ucp_worker_h>       m_worker;
        group_vec_t                     m_groups;

    private:
        void set_group(ucg_group_h group, int group_index);
    };

    static bool is_request_completed(ucg_request_t *req);
};

/**
 * UCG test
 */
class ucg_test : public ucg_test_base,
                 public ::testing::TestWithParam<ucg_test_param>,
                 public ::commmunicator_storage<ucg_test_base::rank> {

    friend class ucg_test_base::rank;

public:
    UCS_TEST_BASE_IMPL;

    ucg_test();
    virtual ~ucg_test();

    ucg_config_t* m_ucg_config;

    static std::vector<ucg_test_param>
    enum_test_params(const ucg_params_t& ucg_params,
                     const ucp_params_t& ucp_params,
                     const std::string& name,
                     const std::string& test_case_name,
                     const std::string& planners);

    static ucg_params_t get_ucg_ctx_params();
    static ucp_params_t get_ucp_ctx_params();
    virtual ucp_worker_params_t get_worker_params();
    virtual ucg_group_params_t get_group_params(ucg_group_member_index_t index);

    static void
    generate_test_params_variant(const ucg_params_t& ucg_params,
                                 const ucp_params_t& ucp_params,
                                 const std::string& name,
                                 const std::string& test_case_name,
                                 const std::string& planners,
                                 ucg_group_member_index_t group_size,
                                 std::vector<ucg_test_param>& test_params);

    virtual void modify_config(const std::string& name, const std::string& value,
                               modify_config_mode_t mode = FAIL_IF_NOT_EXIST);
    void stats_activate();
    void stats_restore();

private:
    static void set_ucg_config(ucg_config_t *config,
                               const ucg_test_param& test_param);
    static bool check_test_param(const std::string& name,
                                 const std::string& test_case_name,
                                 const ucg_test_param& test_param);
    ucs_status_t request_process(void *req, int worker_index, bool wait);

protected:
    virtual void init();
    virtual void cleanup();
    virtual bool has_planner(const std::string& planner_name) const;
    bool has_any_planner(const std::vector<std::string>& planner_names) const;
    rank* create_comm(bool add_in_front = false);
    rank* create_comm(bool add_in_front, const ucg_test_param& test_param);
    unsigned worker_progress() const;
    ucs_status_t request_wait(ucg_request_t *req, int worker_index = 0);
    void set_ucg_config(ucg_config_t *config);
};


std::ostream& operator<<(std::ostream& os, const ucg_test_param& test_param);

/**
 * Instantiate the parameterized test case a combination of transports.
 *
 * @param _test_case   Test case class, derived from ucg_test.
 * @param _name        Instantiation name.
 * @param ...          Transport names.
 */
#define UCG_INSTANTIATE_TEST_CASE_PLANNERS(_test_case, _name, _planners) \
    INSTANTIATE_TEST_CASE_P(_name,  _test_case, \
                            testing::ValuesIn(_test_case::enum_test_params(_test_case::get_ucg_ctx_params(), \
                                                                           _test_case::get_ucp_ctx_params(), \
                                                                           #_name, \
                                                                           #_test_case, \
                                                                           _planners)));


/**
 * Instantiate the parameterized test case for all transport combinations.
 *
 * @param _test_case  Test case class, derived from ucg_test.
 */
#define UCG_INSTANTIATE_TEST_CASE(_test_case) \
    UCG_INSTANTIATE_TEST_CASE_PLANNERS(_test_case, builtin, "builtin")


#endif
