#ifndef MDO_APPLICATION_SDK_H
#define MDO_APPLICATION_SDK_H
#include "module.h"

#ifdef __cplusplus
extern "C" {
#endif
#define MDO_SDK_VERSION 1u
typedef struct xvalue xvalue;
typedef struct mdo_sdk_context_v1 mdo_sdk_context_v1;
typedef enum mdo_call_origin {
    MDO_ORIGIN_UNKNOWN = 0, MDO_ORIGIN_USER = 1,
    MDO_ORIGIN_AGENT = 2, MDO_ORIGIN_EXTENSION = 3
} mdo_call_origin;
typedef enum mdo_value_kind {
    MDO_VALUE_NULL, MDO_VALUE_BOOL, MDO_VALUE_INT, MDO_VALUE_UINT,
    MDO_VALUE_NUMBER, MDO_VALUE_STRING, MDO_VALUE_ARRAY, MDO_VALUE_OBJECT,
    MDO_VALUE_OTHER
} mdo_value_kind;
typedef struct mdo_error_v1 {
    uint32_t Size;
    char Code[64], Message[512], Stage[64];
    uint32_t Status;
    bool Retryable, MayHaveCompleted;
    char Cause[1024];
} mdo_error_v1;
typedef struct mdo_values_v1 {
    uint32_t Size;
    xvalue* (*Null)(void);
    xvalue* (*Bool)(bool);
    xvalue* (*Int)(int64_t);
    xvalue* (*UInt)(uint64_t);
    xvalue* (*Number)(double);
    xvalue* (*String)(const char*);
    xvalue* (*Object)(void);
    xvalue* (*Array)(void);
    xvalue* (*Clone)(const xvalue*);
    bool (*Set)(xvalue*,const char*,const xvalue*);
    bool (*Append)(xvalue*,const xvalue*);
    const xvalue* (*Get)(const xvalue*,const char*);
    const xvalue* (*At)(const xvalue*,size_t);
    const char* (*Key)(const xvalue*,size_t);
    size_t (*Count)(const xvalue*);
    mdo_value_kind (*Kind)(const xvalue*);
    const char* (*Text)(const xvalue*);
    bool (*Boolean)(const xvalue*,bool*);
    bool (*Integer)(const xvalue*,int64_t*);
    bool (*Unsigned)(const xvalue*,uint64_t*);
    bool (*Real)(const xvalue*,double*);
    xvalue* (*Parse)(const char*);
    char* (*Json)(const xvalue*);
    void (*Release)(xvalue*);
    void (*Free)(void*);
} mdo_values_v1;
typedef struct mdo_application_v1 {
    uint32_t Size, Version;
    const mdo_values_v1* Values;
    mdo_result (*Call)(const mdo_sdk_context_v1*,const char*,const xvalue*,xvalue**,mdo_error_v1*);
    bool (*Cancelled)(const mdo_sdk_context_v1*);
    uint64_t (*MonotonicMicroseconds)(void);
    /* In-process native modules only. The isolated command worker exposes
     * Call/RPC instead; native function pointers never cross a process. */
    bool (*BindNative)(void* TccState);
    bool (*Builtin)(const char* Method);
} mdo_application_v1;
struct mdo_sdk_context_v1 {
    uint32_t Size, Version;
    const mdo_application_v1* Api;
    void* Opaque;
    const char *ExtensionId,*InvocationId,*DeviceId,*ProjectId,*SessionId,*RunId,*Workspace,*Method;
    uint64_t Deadline;
    const char *Namespace,*MemberId; /* Optional shared execution group and current member. */
    uint32_t Origin; /* Host-set provenance; never accepted from request JSON. */
    /* Host-resolved physical installation, independent of its callable alias.
     * Root is a native directory. Per-device mutable state stays in storage.*. */
    const char *ExtensionKind,*DirectoryId,*ExtensionRoot;
};
/* Provenance describes who initiated the call, not a permission grant. Native
 * plugins are trusted code. Legacy contexts remain readable by Size. */
static inline mdo_call_origin mdoCallOrigin(const mdo_sdk_context_v1* C) {
    if(!C)return MDO_ORIGIN_UNKNOWN;
    if(C->Size>=offsetof(mdo_sdk_context_v1,Origin)+sizeof(C->Origin) && C->Origin)
        return (mdo_call_origin)C->Origin;
    if((C->RunId&&C->RunId[0]) || (C->Size>=offsetof(mdo_sdk_context_v1,MemberId)+sizeof(C->MemberId) && C->MemberId&&C->MemberId[0]))
        return MDO_ORIGIN_AGENT;
    return MDO_ORIGIN_UNKNOWN;
}
/* All arguments are borrowed for the call. Results are owned and released by
 * Values->Release. Set/Append retain their value; no CRT/allocator crosses ABI. */
static inline mdo_result mdoCall(const mdo_sdk_context_v1* C,const char* M,
    const xvalue* P,xvalue** R,mdo_error_v1* E) {
    if(!C||!C->Api||!C->Api->Call)return MDO_RESULT_ERROR;
    return C->Api->Call(C,M,P,R,E);
}
static inline mdo_result mdoServices(const mdo_sdk_context_v1* C,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"sdk.services",NULL,R,E);}
/* Convenience requests use the same described service contract. Body/Path/Query
 * are borrowed; the temporary envelope is released before returning. */
static inline mdo_result mdoRequest(const mdo_sdk_context_v1* C,const char* Method,
    const xvalue* Body,const xvalue* Path,const xvalue* Query,const char* Revision,
    xvalue** Result,mdo_error_v1* Error){
    if(!C||!C->Api||!C->Api->Values)return MDO_RESULT_ERROR;
    const mdo_values_v1* V=C->Api->Values;xvalue* P=V->Object();
    bool Ok=P&&(!Body||V->Set(P,"body",Body))&&(!Path||V->Set(P,"path",Path))&&(!Query||V->Set(P,"query",Query));
    if(Ok&&Revision){xvalue* S=V->String(Revision);Ok=S&&V->Set(P,"revision",S);V->Release(S);}
    mdo_result Code=Ok?mdoCall(C,Method,P,Result,Error):MDO_RESULT_ERROR;V->Release(P);return Code;
}
static inline mdo_result mdoSessionCreate(const mdo_sdk_context_v1* C,const xvalue* Body,xvalue** R,mdo_error_v1* E)
{return mdoRequest(C,"session.create",Body,NULL,NULL,NULL,R,E);}
static inline mdo_result mdoProjectCreate(const mdo_sdk_context_v1* C,const xvalue* Body,xvalue** R,mdo_error_v1* E)
{return mdoRequest(C,"project.create",Body,NULL,NULL,NULL,R,E);}
static inline mdo_result mdoRunStart(const mdo_sdk_context_v1* C,const xvalue* Body,const xvalue* Path,xvalue** R,mdo_error_v1* E)
{return mdoRequest(C,"run.start",Body,Path,NULL,NULL,R,E);}
typedef mdo_result (*mdo_service_execute_v1)(void*,const mdo_sdk_context_v1*,
    const xvalue*,xvalue**,mdo_error_v1*);
typedef struct mdo_service_v1 {
    uint32_t Size, Version;
    const char *Id,*Description,*InputSchema,*OutputSchema;
    mdo_tool_effects Effects;
    void* UserData;
    mdo_service_execute_v1 Execute;
    uint32_t Flags;
} mdo_service_v1;
#define MDO_SERVICE_REPLACE_BUILTIN 1u
typedef mdo_result (*mdo_hook_execute_v1)(void*,const mdo_sdk_context_v1*,
    const char* Topic,const xvalue* Input,xvalue** Replacement,mdo_error_v1*);
typedef struct mdo_hook_v1 {
    uint32_t Size, Version;
    const char *Id,*Topic;
    int32_t Priority;
    bool Transform;
    void* UserData;
    mdo_hook_execute_v1 Execute;
} mdo_hook_v1;

#ifdef __cplusplus
}
#endif
#endif
