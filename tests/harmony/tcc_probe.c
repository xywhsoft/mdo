/* The same small, header-free source is compiled on Windows, the HarmonyOS
 * emulator and, later, an ARM64 phone. Windows success is only a control run;
 * it does not establish HarmonyOS execution or release-signing compatibility.
 * This probe uses the pinned xs TCC unchanged, with normal app permissions. */
#include <stdio.h>
#include <string.h>
#include "libtcc.h"
#ifdef __OHOS__
#include "hilog/log.h"
#endif

static void record_error(void *opaque, const char *message)
{
    (void)opaque;
    fprintf(stderr, "tcc_error=%s\n", message);
#ifdef __OHOS__
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "MdoProbe", "tcc_error=%{public}s", message);
#endif
    fflush(stderr);
}

/* A flush before each step leaves the last reached step visible if the
 * platform terminates the process on generated-code execution. */
static void stage(const char *name)
{
    printf("stage=%s\n", name);
#ifdef __OHOS__
    OH_LOG_Print(LOG_APP, LOG_INFO, 0, "MdoProbe", "stage=%{public}s", name);
#endif
    fflush(stdout);
}

static int host_add(int left, int right) { return left + right; }

int mdo_harmony_tcc_probe(void)
{
    TCCState *compiler;
    int (*value)(void) = NULL;
    void *symbol;
    int result = 1;

    stage("tcc_new");
    compiler = tcc_new();
    if (compiler == NULL) return 1;
    tcc_set_error_func(compiler, NULL, record_error);
    tcc_set_options(compiler, "-nostdlib -nostdinc");
    if (tcc_set_output_type(compiler, TCC_OUTPUT_MEMORY) < 0) goto done;

    /* Exercise the native callback ABI used by xs tool/API registration. */
    int (*host_function)(int, int) = host_add;
    void *host_symbol = NULL;
    if (sizeof(host_function) != sizeof(host_symbol)) goto done;
    memcpy(&host_symbol, &host_function, sizeof(host_symbol));
    if (tcc_add_symbol(compiler, "mdo_host_add", host_symbol) < 0) goto done;

    stage("compile");
    if (tcc_compile_string(compiler,
            "extern int mdo_host_add(int, int);"
            "int mdo_probe_value(void) { return mdo_host_add(19, 23); }") < 0) goto done;
    stage("relocate");
    if (tcc_relocate(compiler) < 0) goto done;
    stage("lookup");
    symbol = tcc_get_symbol(compiler, "mdo_probe_value");
    if (symbol == NULL || sizeof(value) != sizeof(symbol)) goto done;
    memcpy(&value, &symbol, sizeof(value));

    stage("invoke");
    const int actual = value();
    printf("value=%d\n", actual);
    result = actual == 42 ? 0 : 1;
done:
    stage("delete");
    tcc_delete(compiler);
    printf("result=%s\n", result == 0 ? "pass" : "fail");
    fflush(stdout);
    return result;
}

#ifndef MDO_HARMONY_SHARED_PROBE
int main(void)
{
    return mdo_harmony_tcc_probe();
}
#endif
