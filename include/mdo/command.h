#ifndef MDO_PUBLIC_COMMAND_H
#define MDO_PUBLIC_COMMAND_H
#include "module.h"
/* xvalue is opaque. Values belong to the worker's host allocator. */
typedef struct xvalue xvalue;
typedef struct mdo_command_context_v1 mdo_command_context_v1;
typedef struct mdo_command_host_v1 {
    uint32_t Size, AbiVersion;
    size_t (*Count)(const xvalue*);
    const xvalue* (*At)(const xvalue*, size_t);
    const xvalue* (*Get)(const xvalue*, const char*);
    const char* (*Text)(const xvalue*);
    xvalue* (*Parse)(const char*);
    char* (*Json)(const xvalue*);
    void (*Free)(void*);
    void (*Release)(xvalue*);
    /* Result is owned. Failure fills Context->Error; no implicit retries. */
    mdo_result (*Call)(const mdo_command_context_v1*, const char* Method,
        const xvalue* Params, xvalue** Result);
} mdo_command_host_v1;
struct mdo_command_context_v1 {
    uint32_t Size, AbiVersion;
    const char *InvocationId, *CommandId, *ProjectId, *SessionId, *RunId;
    const char* SourceRevision;
    const char *Workspace, *RawArguments;
    uint64_t Deadline;
    const mdo_command_host_v1* Host;
    void* Opaque;
    char* Error;
    size_t ErrorCapacity;
};
typedef mdo_result (*mdo_command_run_v1)(const mdo_command_context_v1*,
    const xvalue* Arguments, xvalue** Result);
#define MDO_COMMAND_ENTRY "mdoCommandRun"
static inline const char* mdoCommandArgText(const mdo_command_context_v1* C,
    const xvalue* A, size_t I) { return C->Host->Text(C->Host->At(A,I)); }
static inline mdo_result mdoCommandCall(const mdo_command_context_v1* C,
    const char* Method, const xvalue* A, xvalue** R) {
    return C->Host->Call(C,Method,A,R);
}
#endif

