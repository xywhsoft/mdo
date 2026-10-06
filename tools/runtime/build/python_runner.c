#define _POSIX_C_SOURCE 200809L
#include <Python.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Native Android subprocess launcher for the official embeddable CPython.
 * MDO_PYTHON_HOME points at the extracted standard-library root when the
 * launcher and native libraries are installed separately through an APK. */
int main(int argc, char **argv)
{
    char home[PATH_MAX];
    const char *configured = getenv("MDO_PYTHON_HOME");
    if (configured && configured[0]) {
        if (strlen(configured) >= sizeof(home)) {
            fprintf(stderr, "MDO_PYTHON_HOME is too long\n");
            return 2;
        }
        strcpy(home, configured);
    } else {
        ssize_t size = readlink("/proc/self/exe", home, sizeof(home) - 1);
        if (size < 0 || (size_t)size >= sizeof(home) - 1) {
            fprintf(stderr, "Cannot locate Python runtime: %s\n", strerror(errno));
            return 2;
        }
        home[size] = '\0';
        char *slash = strrchr(home, '/');
        if (slash) *slash = '\0';
        slash = strrchr(home, '/');
        if (slash) *slash = '\0';
    }
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    config.parse_argv = 1;
    PyStatus status = PyConfig_SetBytesString(&config, &config.home, home);
    if (!PyStatus_Exception(status))
        status = PyConfig_SetBytesArgv(&config, argc, argv);
    if (!PyStatus_Exception(status))
        status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        if (PyStatus_IsExit(status)) return status.exitcode;
        fprintf(stderr, "Python initialization failed: %s\n",
                status.err_msg ? status.err_msg : "unknown error");
        return 2;
    }
    return Py_RunMain();
}
