/* Minimal ordinary-permission Native HAP bridge. Generated-code execution is
 * explicit, so an execution-policy failure cannot hide the AOT control result.
 * This is test-only code; it does not implement a new mdo platform shell. */
#include "napi/native_api.h"

int mdo_harmony_tcc_probe(void);
int mdo_probe_server_start(void);
int mdo_probe_file_check(const char *directory);
int mdo_probe_spawn(void);
int mdo_probe_native_child(void);

static napi_value int_result(napi_env env, int result)
{
    napi_value value;
    return napi_create_int32(env, result, &value) == napi_ok ? value : NULL;
}

static napi_value start_server(napi_env env, napi_callback_info info)
{
    (void)info;
    return int_result(env, mdo_probe_server_start());
}

static napi_value file_check(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value arg;
    char directory[4096];
    size_t size;
    if (napi_get_cb_info(env, info, &argc, &arg, NULL, NULL) != napi_ok || argc != 1 ||
        napi_get_value_string_utf8(env, arg, NULL, 0, &size) != napi_ok || size >= sizeof(directory) ||
        napi_get_value_string_utf8(env, arg, directory, sizeof(directory), &size) != napi_ok) return NULL;
    return int_result(env, mdo_probe_file_check(directory));
}

static napi_value spawn_check(napi_env env, napi_callback_info info)
{
    (void)info;
    return int_result(env, mdo_probe_spawn());
}

static napi_value native_child_check(napi_env env, napi_callback_info info)
{
    (void)info;
    return int_result(env, mdo_probe_native_child());
}

static napi_value aot_value(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value value;
    if (napi_create_int32(env, 42, &value) != napi_ok) return NULL;
    return value;
}

static napi_value run_tcc(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value result;
    const int status = mdo_harmony_tcc_probe();
    if (napi_create_int32(env, status, &result) != napi_ok) return NULL;
    return result;
}

static napi_value init(napi_env env, napi_value exports)
{
    napi_property_descriptor methods[] = {
        { "aotValue", NULL, aot_value, NULL, NULL, NULL, napi_default, NULL },
        { "runTcc", NULL, run_tcc, NULL, NULL, NULL, napi_default, NULL },
        { "startServer", NULL, start_server, NULL, NULL, NULL, napi_default, NULL },
        { "fileCheck", NULL, file_check, NULL, NULL, NULL, napi_default, NULL },
        { "spawnCheck", NULL, spawn_check, NULL, NULL, NULL, napi_default, NULL },
        { "nativeChildCheck", NULL, native_child_check, NULL, NULL, NULL, napi_default, NULL }
    };
    if (napi_define_properties(env, exports,
            sizeof(methods) / sizeof(methods[0]), methods) != napi_ok) return NULL;
    return exports;
}

static napi_module module = {
    .nm_version = 1,
    .nm_register_func = init,
    .nm_modname = "mdoprobe"
};

__attribute__((constructor)) static void register_probe(void)
{
    napi_module_register(&module);
}
