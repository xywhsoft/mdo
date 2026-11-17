/* Own one runtime tree. Parent stdin is a lifeline, never runtime input.
 * No shell, PID reattachment, network listener, credentials or model policy. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdlib.h>
static DWORD WINAPI lifeline(void* event) {
    char byte; DWORD size;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), &byte, 1, &size, NULL) && size) {}
    SetEvent((HANDLE)event); return 0;
}
static wchar_t* quote(wchar_t* out, const wchar_t* value) {
    *out++ = L'"';
    while (*value) {
        size_t slashes = 0; while (*value == L'\\') { slashes++; value++; }
        size_t count = slashes * ((!*value || *value == L'"') ? 2 : 1);
        while (count--) *out++ = L'\\';
        if (*value == L'"') *out++ = L'\\';
        if (*value) *out++ = *value++;
    }
    *out++ = L'"'; *out = 0; return out;
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 125;
    size_t bytes = 8;
    for (int i = 1; i < argc; i++) bytes += (wcslen(argv[i]) * 2 + 4) * sizeof(wchar_t);
    if (bytes > 65536) return 125;
    wchar_t* line = calloc(1, bytes), *out = line;
    if (!line) return 125;
    for (int i = 1; i < argc; i++) { if (i > 1) *out++ = L' '; out = quote(out, argv[i]); }
    HANDLE job = CreateJobObjectW(NULL, NULL), done = CreateEventW(NULL, TRUE, FALSE, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    STARTUPINFOW start = {0}; PROCESS_INFORMATION child = {0}; start.cb = sizeof(start);
    HANDLE null = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    /* Explicitly inherited descriptors: the child must not retain the parent's
     * lifeline read handle, otherwise process-tree ownership becomes ambiguous. */
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = null; start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SetHandleInformation(GetStdHandle(STD_INPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(null, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    DWORD result = 125;
    if (job && done && null != INVALID_HANDLE_VALUE &&
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
        CreateProcessW(argv[1], line, NULL, NULL, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &start, &child)) {
        if (AssignProcessToJobObject(job, child.hProcess)) {
            HANDLE watcher = CreateThread(NULL, 0, lifeline, done, 0, NULL);
            if (watcher) {
                ResumeThread(child.hThread);
                HANDLE events[2] = {done, child.hProcess};
                if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0) TerminateJobObject(job, 143);
                WaitForSingleObject(child.hProcess, INFINITE); GetExitCodeProcess(child.hProcess, &result);
                CloseHandle(watcher);
            } else TerminateProcess(child.hProcess, 125);
        } else TerminateProcess(child.hProcess, 125);
    }
    if (child.hThread) CloseHandle(child.hThread);
    if (child.hProcess) CloseHandle(child.hProcess);
    if (job) CloseHandle(job);
    /* The watcher can still borrow done at process teardown; the OS retires it. */
    if (null != INVALID_HANDLE_VALUE) CloseHandle(null);
    free(line); return (int)result;
}
#else
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
static volatile sig_atomic_t stopped;
static void stop(int signal) { (void)signal; stopped = 1; }
int main(int argc, char** argv) {
    if (argc < 2) return 125;
    struct sigaction action = {0}; action.sa_handler = stop;
    sigaction(SIGTERM, &action, NULL); sigaction(SIGINT, &action, NULL);
    pid_t child = fork();
    if (child < 0) return 125;
    if (!child) {
        setpgid(0, 0); signal(SIGTERM, SIG_DFL); signal(SIGINT, SIG_DFL);
        int null = open("/dev/null", O_RDONLY); if (null < 0 || dup2(null, STDIN_FILENO) < 0) _exit(125); close(null);
        execv(argv[1], argv + 1); _exit(127);
    }
    setpgid(child, child); int status = 0; struct pollfd input = {STDIN_FILENO, POLLIN | POLLHUP, 0};
    while (!stopped) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) { kill(-child, SIGKILL); return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status); }
        if (result < 0 && errno != EINTR) break;
        if (poll(&input, 1, 100) > 0) { char buffer[32]; if (read(STDIN_FILENO, buffer, sizeof(buffer)) <= 0) break; }
    }
    kill(-child, SIGTERM);
    const struct timespec delay = {0, 100000000};
    for (int i = 0; i < 20; i++) { if (waitpid(child, &status, WNOHANG) == child) { kill(-child, SIGKILL); return 143; } nanosleep(&delay, NULL); }
    kill(-child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return 143;
}
#endif
