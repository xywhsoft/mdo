#ifndef MDO_PUBLIC_MODULE_H
#define MDO_PUBLIC_MODULE_H

/* Stable C ABI for trusted in-process mdo modules.  This header deliberately
 * depends only on the C standard integer/size types.  Modules receive host
 * capabilities through tables after relocation; they do not include mdo,
 * xs, xrt, xllm, or xwork headers. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MDO_MODULE_ABI_VERSION 1u
#define MDO_MODULE_ENTRY_SYMBOL "mdoModuleEntry"
#ifndef MDO_EXPORT
#define MDO_EXPORT
#endif
#define MDO_V1_HEADER(type) (uint32_t)sizeof(type), MDO_MODULE_ABI_VERSION

typedef enum mdo_result {
    MDO_RESULT_OK = 0,
    MDO_RESULT_ERROR = -1,
    MDO_RESULT_CANCELLED = -2,
    MDO_RESULT_LIMIT = -3,
    MDO_RESULT_TIMEOUT = -4
} mdo_result;

typedef uint64_t mdo_capabilities;
struct mdo_sdk_context_v1;
struct mdo_service_v1;
struct mdo_hook_v1;
typedef enum mdo_capability_flag {
    MDO_CAPABILITY_LOG = UINT64_C(1) << 0,
    MDO_CAPABILITY_CLOCK = UINT64_C(1) << 1,
    MDO_CAPABILITY_APPLICATION = UINT64_C(1) << 2
} mdo_capability_flag;
#define MDO_CAPABILITY_ALL (MDO_CAPABILITY_LOG | MDO_CAPABILITY_CLOCK | MDO_CAPABILITY_APPLICATION)

typedef uint64_t mdo_tool_effects;
typedef enum mdo_tool_effect_flag {
    MDO_TOOL_EFFECT_READ = UINT64_C(1) << 0,
    MDO_TOOL_EFFECT_WORKSPACE_WRITE = UINT64_C(1) << 1,
    MDO_TOOL_EFFECT_PROCESS = UINT64_C(1) << 2,
    MDO_TOOL_EFFECT_NETWORK = UINT64_C(1) << 3,
    MDO_TOOL_EFFECT_EXTERNAL_SERVICE = UINT64_C(1) << 4,
    MDO_TOOL_EFFECT_SECRETS = UINT64_C(1) << 5,
    MDO_TOOL_EFFECT_SCHEDULE = UINT64_C(1) << 6,
    MDO_TOOL_EFFECT_AGENT_DELEGATION = UINT64_C(1) << 7
} mdo_tool_effect_flag;
#define MDO_TOOL_EFFECT_ALL UINT64_C(0xff)

typedef enum mdo_resource_kind {
    MDO_RESOURCE_NONE = 0,
    MDO_RESOURCE_PATH,
    MDO_RESOURCE_COMMAND,
    MDO_RESOURCE_PROCESS,
    MDO_RESOURCE_NETWORK,
    MDO_RESOURCE_EXTERNAL_SERVICE,
    MDO_RESOURCE_SECRET,
    MDO_RESOURCE_SCHEDULE,
    MDO_RESOURCE_AGENT
} mdo_resource_kind;

typedef uint32_t mdo_resource_access;
typedef enum mdo_resource_access_flag {
    MDO_RESOURCE_ACCESS_READ = UINT32_C(1) << 0,
    MDO_RESOURCE_ACCESS_WRITE = UINT32_C(1) << 1,
    MDO_RESOURCE_ACCESS_EXECUTE = UINT32_C(1) << 2,
    MDO_RESOURCE_ACCESS_CONTROL = UINT32_C(1) << 3,
    MDO_RESOURCE_ACCESS_CONNECT = UINT32_C(1) << 4,
    MDO_RESOURCE_ACCESS_USE = UINT32_C(1) << 5
} mdo_resource_access_flag;

typedef struct mdo_tool_context_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    const char* ToolId;
    const char* ToolCallId;
    const char* WorkspaceRoot;
    uint64_t AgentTurn;
    uint64_t AgentId;
    uint64_t RunId;
    uint64_t CatalogGeneration;
    uint64_t Deadline;
    const void* CancelToken;
    const struct mdo_sdk_context_v1* Application;
} mdo_tool_context_v1;

typedef struct mdo_permission_writer_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    void* Context;
    bool (*Add)(void* Context, mdo_resource_kind Kind,
        mdo_resource_access Access, const char* Resource);
} mdo_permission_writer_v1;

typedef struct mdo_result_writer_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    void* Context;
    bool (*Write)(void* Context, const char* Text, size_t Length);
    bool (*WriteText)(void* Context, const char* Text);
    bool (*SetImage)(void* Context, const unsigned char* Data,
        size_t Size, const char* Mime);
    bool (*SetSuccess)(void* Context, bool Success);
} mdo_result_writer_v1;

typedef mdo_result (*mdo_tool_describe_permissions_v1)(
    void* UserData,
    const mdo_tool_context_v1* ToolContext,
    const char* ArgumentsJson,
    mdo_permission_writer_v1* Writer,
    char* ErrorMessage,
    size_t ErrorCapacity);

typedef mdo_result (*mdo_tool_execute_v1)(
    void* UserData,
    const mdo_tool_context_v1* ToolContext,
    const char* ArgumentsJson,
    mdo_result_writer_v1* Writer,
    char* ErrorMessage,
    size_t ErrorCapacity);

typedef void (*mdo_tool_cancel_v1)(void* UserData, uint64_t RunId,
    const char* ToolCallId);

typedef uint32_t mdo_tool_flags;
typedef enum mdo_tool_flag {
    MDO_TOOL_STRICT = UINT32_C(1) << 0,
    MDO_TOOL_PARALLEL_SAFE = UINT32_C(1) << 1,
    MDO_TOOL_BACKGROUND = UINT32_C(1) << 2
} mdo_tool_flag;
#define MDO_TOOL_FLAGS_V1 (MDO_TOOL_STRICT | MDO_TOOL_PARALLEL_SAFE | \
    MDO_TOOL_BACKGROUND)

typedef struct mdo_tool_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    const char* Id;
    const char* Name;
    const char* Description;
    const char* ParametersJson;
    mdo_tool_effects Effects;
    mdo_tool_flags Flags;
    const char* SerialGroup;
    /* Optional constant permission resource.  It is valid only when the tool
     * has one non-read effect.  Tools with multiple non-read effects provide
     * DescribePermissions instead. */
    const char* PermissionResource;
    size_t MaxResultBytes;
    void* UserData;
    mdo_tool_describe_permissions_v1 DescribePermissions;
    mdo_tool_execute_v1 Execute;
    mdo_tool_cancel_v1 Cancel;
} mdo_tool_v1;

typedef uint32_t mdo_agent_flags;
typedef enum mdo_agent_flag {
    MDO_AGENT_MAIN = UINT32_C(1) << 0,
    MDO_AGENT_SUBAGENT = UINT32_C(1) << 1,
    MDO_AGENT_READ_ONLY = UINT32_C(1) << 2,
    MDO_AGENT_ALLOW_BACKGROUND = UINT32_C(1) << 3,
    MDO_AGENT_ALLOW_DELEGATION = UINT32_C(1) << 4,
    /* Main-Agent opt-in: require an executable verification after each write.
     * Delegated runs inherit their main Agent's completion policy. */
    MDO_AGENT_REQUIRE_VERIFICATION_AFTER_WRITE = UINT32_C(1) << 5,
    /* An empty tool list means no tools, rather than legacy all-tools. */
    MDO_AGENT_EXPLICIT_TOOLS = UINT32_C(1) << 6
} mdo_agent_flag;
#define MDO_AGENT_FLAGS_V1 (MDO_AGENT_MAIN | MDO_AGENT_SUBAGENT | \
    MDO_AGENT_READ_ONLY | MDO_AGENT_ALLOW_BACKGROUND | \
    MDO_AGENT_ALLOW_DELEGATION | MDO_AGENT_REQUIRE_VERIFICATION_AFTER_WRITE | MDO_AGENT_EXPLICIT_TOOLS)

struct mdo_host_services_v1;
typedef mdo_result (*mdo_agent_acquire_v1)(void* UserData,
    const struct mdo_host_services_v1* Host, char* ErrorMessage,
    size_t ErrorCapacity);
typedef void (*mdo_agent_release_v1)(void* UserData);

typedef enum mdo_agent_phase {
    MDO_AGENT_CONTEXT = 1,
    MDO_AGENT_BEFORE_REQUEST,
    MDO_AGENT_AFTER_RESPONSE,
    MDO_AGENT_BEFORE_TOOL,
    MDO_AGENT_AFTER_TOOL,
    MDO_AGENT_BEFORE_DELEGATE,
    MDO_AGENT_AFTER_DELEGATE,
    MDO_AGENT_INSTANCE_RELEASE,
    MDO_AGENT_RUN_PREPARE,
    MDO_AGENT_RUN_STARTED,
    MDO_AGENT_RUN_FINISHED,
    MDO_AGENT_RUN_FAILED,
    MDO_AGENT_RUN_CANCELLED,
    MDO_AGENT_COMPACTION_STARTED,
    MDO_AGENT_COMPACTION_FINISHED,
    MDO_AGENT_COMPACTION_REJECTED,
    MDO_AGENT_DELEGATE_ACCEPTED,
    MDO_AGENT_DELEGATE_FINISHED,
    MDO_AGENT_PHASE_COUNT
} mdo_agent_phase;

/* ContextPlan and State are xvalue objects when using the native SDK. Native
 * is an xwork_agent_extension_context. Borrowed only during this callback.
 * State is private to this Agent instance, never shared with sibling tasks. */
typedef struct mdo_agent_context_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    const char* AgentId;
    const char* ProjectId;
    const char* SessionId;
    const char* WorkspaceRoot;
    const char* ConfigurationJson;
    const struct mdo_sdk_context_v1* Application;
    const void* CancelToken;
    uint64_t Deadline;
    void* ContextPlan;
    void* State;
    void* Native;
    const void* Event; /* callback-local xwork_event for runtime notifications */
    bool OutcomeAvailable;
    mdo_result Outcome;
    const void* Error; /* callback-local xwork_error after a complete run */
} mdo_agent_context_v1;

typedef mdo_result (*mdo_agent_event_v1)(void* UserData,
    mdo_agent_phase Phase, const mdo_agent_context_v1* Context,
    char* ErrorMessage, size_t ErrorCapacity);
typedef mdo_result (*mdo_agent_drive_v1)(void* UserData,
    const mdo_agent_context_v1* Context, char* ErrorMessage, size_t ErrorCapacity);

typedef struct mdo_agent_program_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    mdo_agent_event_v1 OnEvent;
    mdo_agent_drive_v1 Run;
} mdo_agent_program_v1;

typedef struct mdo_agent_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    const char* Id;
    const char* Name;
    const char* Description;
    const char* Model;
    const char* ReasoningEffort;
    const char* SystemPrompt;
    const char* PermissionProfile;
    const char* const* Tools;
    size_t ToolCount;
    const char* const* Skills;
    size_t SkillCount;
    uint64_t ContextWindowTokens;
    uint64_t MaxInputTokens;
    uint32_t MaxOutputTokens;
    uint32_t MaxTurns;
    uint32_t TimeoutMilliseconds;
    size_t MaxFinalBytes;
    mdo_tool_effects AllowedEffects;
    uint32_t MaxDepth;
    mdo_agent_flags Flags;
    void* UserData;
    mdo_agent_acquire_v1 Acquire;
    mdo_agent_release_v1 Release;
    /* Optional normalized mdo.agent.v1 options. Copied by the host. */
    const char* ConfigurationJson;
    const mdo_agent_program_v1* Program;
} mdo_agent_v1;

/* Global lifecycle hooks are pinned with the Agent catalog. Observer failures
 * are reported and isolated. Control hooks may reject preparation/request/tool
 * phases; completed/released runtime notifications are always observational. */
#define MDO_AGENT_PHASE_BIT(Phase) (UINT64_C(1) << ((unsigned)(Phase)-1u))
#define MDO_AGENT_PHASES_ALL ((UINT64_C(1) << (MDO_AGENT_PHASE_COUNT-1u))-1u)
typedef struct mdo_lifecycle_v1 {
    uint32_t Size,AbiVersion;
    const char* Id;
    const char* AgentPattern; /* exact id or one trailing '*'; NULL selects all */
    int32_t Priority;
    bool ObserveOnly;
    uint64_t Phases;
    void* UserData;
    mdo_agent_event_v1 OnEvent;
} mdo_lifecycle_v1;

typedef enum mdo_log_level {
    MDO_LOG_DEBUG = 0,
    MDO_LOG_INFO,
    MDO_LOG_WARNING,
    MDO_LOG_ERROR
} mdo_log_level;

typedef struct mdo_host_core_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    void* Context;
    void (*Log)(void* Context, mdo_log_level Level, const char* Message);
    uint64_t (*MonotonicMicroseconds)(void* Context);
    int64_t (*UnixMicroseconds)(void* Context);
    bool (*CancelRequested)(void* Context, const void* CancelToken);
} mdo_host_core_v1;

typedef struct mdo_host_services_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    mdo_capabilities GrantedCapabilities;
    const mdo_host_core_v1* Core;
    const struct mdo_sdk_context_v1* Application;
} mdo_host_services_v1;

typedef struct mdo_registrar_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    void* Context;
    mdo_result (*AddTool)(void* Context, const mdo_tool_v1* Tool,
        char* ErrorMessage, size_t ErrorCapacity);
    mdo_result (*AddAgent)(void* Context, const mdo_agent_v1* Agent,
        char* ErrorMessage, size_t ErrorCapacity);
    mdo_result (*AddService)(void* Context, const struct mdo_service_v1*, char*, size_t);
    mdo_result (*AddHook)(void* Context, const struct mdo_hook_v1*, char*, size_t);
    mdo_result (*AddLifecycle)(void* Context,const mdo_lifecycle_v1*,char*,size_t);
} mdo_registrar_v1;

typedef mdo_result (*mdo_module_register_v1)(
    const mdo_host_services_v1* Host,
    const mdo_registrar_v1* Registrar,
    void** ModuleData,
    char* ErrorMessage,
    size_t ErrorCapacity);

typedef void (*mdo_module_unregister_v1)(void* ModuleData);

typedef struct mdo_module_v1 {
    uint32_t Size;
    uint32_t AbiVersion;
    const char* Id;
    const char* Name;
    const char* Description;
    const char* Version;
    mdo_capabilities RequiredCapabilities;
    const char* const* Dependencies;
    size_t DependencyCount;
    mdo_module_register_v1 Register;
    mdo_module_unregister_v1 Unregister;
    /* Stop only owned producers/workers; shared callback data stays valid
     * until Unregister. Called once on retirement/shutdown, outside locks. */
    mdo_module_unregister_v1 Stop;
} mdo_module_v1;

typedef const mdo_module_v1* (*mdo_module_entry_v1)(void);

#ifdef __cplusplus
}
#endif

#endif
