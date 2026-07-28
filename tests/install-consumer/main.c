/* SPDX-License-Identifier: MIT */
#include <backend_ndl/backend_ndl.h>

#include <string.h>

int main(void) {
    BackendNdlConfig config;
    backend_ndl_config_defaults(&config);
    config.app_id = "test.backend-ndl.install-consumer";

    BackendNdl *ctx = backend_ndl_open(&config);
    backend_ndl_close(ctx);

    return strcmp(backend_ndl_result_name(BACKEND_NDL_OK), "OK") == 0 ? 0 : 1;
}
