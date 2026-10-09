/* Entry for the official Native Child Process API. No shell, persistent work,
 * or elevated permissions: return one fixed result through the supplied FD. */
#include <AbilityKit/native_child_process.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

__attribute__((visibility("default"))) void MdoProbeMain(NativeChildProcess_Args args)
{
    for (NativeChildProcess_Fd *fd = args.fdList.head; fd; fd = fd->next) {
        if (!fd->fdName || strcmp(fd->fdName, "result") != 0) continue;
        const char value[] = "42";
        size_t used = 0;
        while (used < sizeof(value) - 1) {
            ssize_t sent = write(fd->fd, value + used, sizeof(value) - 1 - used);
            if (sent < 0 && errno == EINTR) continue;
            if (sent <= 0) break;
            used += (size_t)sent;
        }
        close(fd->fd);
        return;
    }
}
