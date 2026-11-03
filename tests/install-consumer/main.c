/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <backend_ndl/backend_ndl.h>

#include <string.h>
#ifdef TEST_WITH_GSTREAMER
#include <dlfcn.h>
#endif

int main(void) {
#ifdef TEST_WITH_GSTREAMER
    const char *hooks[] = {"g_object_set", "gst_app_src_push_buffer", "gst_pipeline_new",
                           "gst_element_factory_make", "gst_element_factory_create", "gst_bus_post"};
    Dl_info executable;
    if (!dladdr((void *)&main, &executable)) return 2;
    for (size_t i = 0; i < sizeof(hooks) / sizeof(*hooks); i++) {
        Dl_info symbol;
        if (!dladdr(dlsym(RTLD_DEFAULT, hooks[i]), &symbol) ||
            symbol.dli_fbase != executable.dli_fbase) return 3;
    }
#endif
    BackendNdlConfig config;
    backend_ndl_config_defaults(&config);
    config.app_id = "test.backend-ndl.install-consumer";

    BackendNdl *ctx = backend_ndl_open(&config);
    backend_ndl_close(ctx);

    return strcmp(backend_ndl_result_name(BACKEND_NDL_OK), "OK") == 0 ? 0 : 1;
}
