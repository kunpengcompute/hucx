/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_test.h"

using namespace std;

class ucg_context_test : public ucg_test {
public:
    static ucp_params_t get_ctx_params() {
        ucp_params_t params = get_ctx_params_inner();
        params.features |= UCS_BIT(0) | UCS_BIT(4);
        return params;
    }

protected:
    static ucp_params_t get_ctx_params_inner() {
        ucp_params_t params;
        memset(&params, 0, sizeof(params));
        params.field_mask |= UCS_BIT(0);
        return params;
    }
};

TEST(ucg_context_test, minimal_field_mask) {
    ucs::handle<ucp_config_t *> config;
    UCS_TEST_CREATE_HANDLE(ucp_config_t *, config, ucp_config_release,
                           ucp_config_read, NULL, NULL);

    ucs::handle<ucp_context_h> ucgh;
    ucs::handle<ucp_worker_h> worker;

    {
        /* Features ONLY */
        ucp_params_t params;
        VALGRIND_MAKE_MEM_UNDEFINED(&params, sizeof(params));
        params.field_mask = UCS_BIT(0);
        params.features = ucg_context_test::get_ctx_params().features;

        UCS_TEST_CREATE_HANDLE(ucp_context_h, ucgh, ucp_cleanup,
                               ucp_init, &params, config.get());
    }

    {
        /* Empty set */
        ucp_worker_params_t params;
        VALGRIND_MAKE_MEM_UNDEFINED(&params, sizeof(params));
        params.field_mask = 0;

        UCS_TEST_CREATE_HANDLE(ucp_worker_h, worker, ucp_worker_destroy,
                               ucp_worker_create, ucgh.get(), &params);
    }
}


class ucg_version_test : public ucg_context_test {
};

TEST_F(ucg_version_test, test_wrong_api_version) {
    ucs::handle<ucp_config_t *> config;
    UCS_TEST_CREATE_HANDLE(ucp_config_t *, config, ucp_config_release,
                           ucp_config_read, NULL, NULL);

    ucp_params_t params = get_ctx_params();
    ucp_context_h ucgh;
    ucs_status_t status;

    status = ucp_init_version(99, 99, &params, config.get(), &ucgh);

    // TODO
    if (status != UCS_OK) {
        cout << "Failed to create UCP with wrong version" << endl;
    } else {
        cout << "Missing wrong version warning" << endl;
    }
}

TEST_F(ucg_version_test, test_version) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%d.%d.%d", UCG_API_MAJOR, UCG_API_MINOR, 0);

    cout << "Ucg version : " << buffer << endl;
    cout << "Ucg version : " << UCG_API_VERSION << endl;
}

TEST_F(ucg_version_test, test_version_string) {
    unsigned major_version, minor_version, release_number;

    ucg_get_version(&major_version, &minor_version, &release_number);

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%d.%d.%d", major_version, minor_version,
             release_number);

    cout << "Ucg version str: " << buffer << endl;

    EXPECT_EQ(std::string(buffer), std::string(ucg_get_version_string()));
}

