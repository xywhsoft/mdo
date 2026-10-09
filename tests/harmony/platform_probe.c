/* Bounded functional probes, not a reusable HTTP server. This localhost-only
 * listener serves two fixed checks until the test app exits. WebSocket accept
 * calculation uses the existing xrt implementation, unchanged. */
#define XRT_MODULE_STRING
#define XRT_MODULE_WEBSOCKET_HANDSHAKE
#define XRT_IMPLEMENTATION
#include "xrt.h"
#include <AbilityKit/native_child_process.h>
#include <hilog/log.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <poll.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
static int listener = -1;
static int server_port;

static int send_all(int fd, const void *data, size_t size)
{
    const char *cursor = data;
    while (size) {
        ssize_t sent = send(fd, cursor, size, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return -1;
        cursor += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static void serve_connection(int fd)
{
    char request[2048] = {0};
    size_t used = 0;
    struct timeval timeout = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    while (used < sizeof(request) - 1 && !strstr(request, "\r\n\r\n")) {
        ssize_t received = recv(fd, request + used, sizeof(request) - 1 - used, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) return;
        used += (size_t)received;
    }
    if (!strstr(request, "\r\n\r\n")) return;
    if (strncmp(request, "GET /value ", 11) == 0) {
        const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
            "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n42";
        send_all(fd, response, sizeof(response) - 1);
    } else if (strncmp(request, "GET /ws ", 8) == 0) {
        const char *key = strstr(request, "\r\nSec-WebSocket-Key: ");
        if (!key) return;
        key += strlen("\r\nSec-WebSocket-Key: ");
        const char *end = strstr(key, "\r\n");
        if (!end || end - key > 64) return;
        char accept_key[64];
        if (!xrtWsAccept(xrtStrViewN(key, (size_t)(end - key)), accept_key, sizeof(accept_key))) return;
        char response[256];
        int size = snprintf(response, sizeof(response),
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept_key);
        if (size < 0 || size >= (int)sizeof(response) || send_all(fd, response, (size_t)size)) return;
        /* Text '42', followed by a normal (1000) close frame. */
        const unsigned char frames[] = {0x81, 2, '4', '2', 0x88, 2, 3, 0xe8};
        send_all(fd, frames, sizeof(frames));
    }
}

static void *server_thread(void *unused)
{
    (void)unused;
    for (;;) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0 && errno == EINTR) continue;
        if (fd < 0) break;
        serve_connection(fd);
        close(fd);
    }
    return NULL;
}

int mdo_probe_server_start(void)
{
    if (listener >= 0) return server_port;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -errno;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 4)) {
        int error = errno;
        close(fd);
        return -error;
    }
    socklen_t length = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &length)) {
        int error = errno;
        close(fd);
        return -error;
    }
    listener = fd;
    server_port = ntohs(address.sin_port);
    pthread_t thread;
    int error = pthread_create(&thread, NULL, server_thread, NULL);
    if (error) { listener = -1; close(fd); return -error; }
    pthread_detach(thread);
    return server_port;
}

int mdo_probe_file_check(const char *directory)
{
    char path[PATH_MAX];
    int size = snprintf(path, sizeof(path), "%s/mdo-platform-probe.txt", directory);
    if (size < 0 || size >= (int)sizeof(path)) return ENAMETOOLONG;
    FILE *file = fopen(path, "wb");
    if (!file) return errno;
    int written = fwrite("mdo-probe", 1, 9, file) == 9;
    int closed = fclose(file) == 0;
    if (!written || !closed) { unlink(path); return EIO; }
    file = fopen(path, "rb");
    if (!file) { int error = errno; unlink(path); return error; }
    char content[10] = {0};
    int valid = fread(content, 1, 9, file) == 9 && strcmp(content, "mdo-probe") == 0;
    fclose(file);
    if (unlink(path)) return errno;
    return valid ? 0 : EIO;
}

int mdo_probe_spawn(void)
{
    char *args[] = {"sh", "-c", "exit 0", NULL};
    pid_t child;
    int error = posix_spawn(&child, "/system/bin/sh", NULL, NULL, args, environ);
    if (error) return error;
    int status;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0) return errno;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : EIO;
}

/* A platform-managed library child is different from exec'ing a shell.
 * Check both paths without special permissions or changed system policy. */
int mdo_probe_native_child(void)
{
    int pipe_fd[2];
    if (pipe(pipe_fd)) return errno;
    NativeChildProcess_Fd output = {"result", pipe_fd[1], NULL};
    NativeChildProcess_Args args = {"mdo-probe", {&output}};
    NativeChildProcess_Options options = {NCP_ISOLATION_MODE_NORMAL, 0};
    int32_t pid = -1;
    bool supported = OH_Ability_IsNativeChildProcessSupported();
    int result = OH_Ability_StartNativeChildProcess(
        "libmdochildprobe.so:MdoProbeMain", args, options, &pid);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0, "MdoProbe",
        "native_child_supported=%{public}d start_result=%{public}d pid=%{public}d",
        supported, result, pid);
    close(pipe_fd[1]);
    if (!result) {
        struct pollfd readable = {pipe_fd[0], POLLIN, 0};
        int ready;
        do { ready = poll(&readable, 1, 5000); } while (ready < 0 && errno == EINTR);
        if (ready < 0) result = errno;
        else if (!ready) result = ETIMEDOUT;
        else {
            char response[2];
            ssize_t count;
            do { count = read(pipe_fd[0], response, sizeof(response)); } while (count < 0 && errno == EINTR);
            result = count == 2 && response[0] == '4' && response[1] == '2' ? 0 : EIO;
        }
    }
    close(pipe_fd[0]);
    return result;
}
