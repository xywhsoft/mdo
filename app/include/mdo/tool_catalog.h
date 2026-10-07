#ifndef MDO_TOOL_CATALOG_H
#define MDO_TOOL_CATALOG_H

#include <xsbase.h>
#include <xwork.h>
#include <string.h>

/* Product metadata, shared by discovery and Agent allowlist validation.
 * Execution and parameter schemas remain owned by their existing providers. */
typedef enum MdoToolRequirement {
    MDO_TOOL_ALWAYS, MDO_TOOL_WEB, MDO_TOOL_SUBAGENT, MDO_TOOL_MCP
} MdoToolRequirement;

typedef struct MdoBuiltinTool {
    cstr Id;
    cstr Description;
    xwork_tool_effects Effects;
    MdoToolRequirement Requirement;
    bool MemberOnly;
} MdoBuiltinTool;

static const MdoBuiltinTool MDO_BUILTIN_TOOLS[] = {
    {"read", "Read workspace text and images", XWORK_TOOL_EFFECT_READ, MDO_TOOL_ALWAYS, false},
    {"ls", "List workspace directories", XWORK_TOOL_EFFECT_READ, MDO_TOOL_ALWAYS, false},
    {"glob", "Find files by path pattern", XWORK_TOOL_EFFECT_READ, MDO_TOOL_ALWAYS, false},
    {"grep", "Search text in workspace files", XWORK_TOOL_EFFECT_READ, MDO_TOOL_ALWAYS, false},
    {"write", "Create, overwrite or append files", XWORK_TOOL_EFFECT_WORKSPACE_WRITE, MDO_TOOL_ALWAYS, false},
    {"edit", "Apply exact text edits", XWORK_TOOL_EFFECT_WORKSPACE_WRITE, MDO_TOOL_ALWAYS, false},
    {"exec", "Run a command to completion", XWORK_TOOL_EFFECT_PROCESS, MDO_TOOL_ALWAYS, false},
    {"spawn", "Start a background process task", XWORK_TOOL_EFFECT_PROCESS, MDO_TOOL_ALWAYS, false},
    {"poll", "Read task output and state", XWORK_TOOL_EFFECT_READ, MDO_TOOL_ALWAYS, false},
    {"wait", "Wait for background tasks", XWORK_TOOL_EFFECT_READ, MDO_TOOL_ALWAYS, false},
    {"stdin", "Write to a process task's input", XWORK_TOOL_EFFECT_PROCESS, MDO_TOOL_ALWAYS, false},
    {"stop", "Stop a process or SubAgent task", XWORK_TOOL_EFFECT_PROCESS, MDO_TOOL_ALWAYS, false},
    {"ask_user", "Ask the user a question", XWORK_TOOL_EFFECT_READ, MDO_TOOL_ALWAYS, false},
    {"mdo.todo", "Update the conversation task plan", XWORK_TOOL_EFFECT_READ, MDO_TOOL_ALWAYS, false},
    {"skill", "Discover and read installed Skills", XWORK_TOOL_EFFECT_READ, MDO_TOOL_ALWAYS, false},
    {"agent", "Delegate to an enabled SubAgent", XWORK_TOOL_EFFECT_AGENT_DELEGATION, MDO_TOOL_SUBAGENT, false},
    {"web_search", "Search the web through the account service", XWORK_TOOL_EFFECT_READ | XWORK_TOOL_EFFECT_NETWORK | XWORK_TOOL_EFFECT_EXTERNAL_SERVICE | XWORK_TOOL_EFFECT_SECRETS, MDO_TOOL_WEB, true},
    {"web_open", "Read a public web page", XWORK_TOOL_EFFECT_READ | XWORK_TOOL_EFFECT_NETWORK, MDO_TOOL_WEB, true},
    {"web_find", "Find text in an opened web page", XWORK_TOOL_EFFECT_READ, MDO_TOOL_WEB, true},
    {"tool_search", "Discover connected MCP tools", XWORK_TOOL_EFFECT_EXTERNAL_SERVICE, MDO_TOOL_MCP, false},
    {"tool_load", "Load an MCP tool on demand", XWORK_TOOL_EFFECT_EXTERNAL_SERVICE, MDO_TOOL_MCP, false}
};

static inline const MdoBuiltinTool* MdoBuiltinToolFind(cstr Id)
{
    size_t i;
    for (i = 0u; i < sizeof(MDO_BUILTIN_TOOLS) / sizeof(MDO_BUILTIN_TOOLS[0]); ++i)
        if (strcmp(MDO_BUILTIN_TOOLS[i].Id, Id) == 0) return &MDO_BUILTIN_TOOLS[i];
    return NULL;
}

/* Import compatibility names are reserved alongside their real tool IDs. */
static inline cstr MdoBuiltinToolCanonicalId(cstr Id)
{
    static const char* const Aliases[][2] = {
        {"Read","read"}, {"Grep","grep"}, {"Glob","glob"}, {"Bash","exec"},
        {"Edit","edit"}, {"Write","write"}, {"WebSearch","web_search"},
        {"WebFetch","web_open"}, {"TodoWrite","mdo.todo"}, {"Skill","skill"}
    };
    size_t i;
    for (i=0u;i<sizeof(Aliases)/sizeof(Aliases[0]);++i)
        if (!strcmp(Id,Aliases[i][0])) return Aliases[i][1];
    return Id;
}

#endif
