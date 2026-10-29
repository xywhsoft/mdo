/* Shared process output storage. Generated into the locked xwork public header.
 * Options.Directory is an anchored, caller-owned artifact root. Explicit paths
 * must be resolved and authorized by the caller before Create. Returned values
 * are owned xvalues. No result read consumes or advances a shared cursor. */
typedef struct xwork_process_output xwork_process_output;
typedef struct xvalue xvalue;
typedef struct xwork_process_output_options {
    uint32_t Size;
    xroot Directory;
    const char* StdoutPath;
    const char* StderrPath;
    size_t InlineBytes;
    uint64_t MaxBytes;
    bool FilesFromStart;
    bool MergeStderr;
} xwork_process_output_options;
void xworkProcessOutputOptionsInit(xwork_process_output_options* Options);
xwork_process_output* xworkProcessOutputCreate(
    const xwork_process_output_options* Options, xwork_error* Error);
bool xworkProcessOutputWrite(xwork_process_output* Output, bool Stderr,
    const void* Bytes, size_t Size);
void xworkProcessOutputFinish(xwork_process_output* Output);
xvalue* xworkProcessOutputSnapshot(xwork_process_output* Output, bool IncludeText);
void xworkProcessOutputDestroy(xwork_process_output* Output);
/* Removes only recognized, unlocked process-output directories; never follows
 * links or removes explicit output destinations. Age is measured in seconds. */
bool xworkProcessOutputCollect(xroot Directory, uint64_t MaxBytes, uint64_t MaxAgeSeconds);
