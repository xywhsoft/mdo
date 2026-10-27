#ifndef MDO_AGENT_FILE_H
#define MDO_AGENT_FILE_H

#include "modules.h"
#include "prompt_file.h"
#include "agent_config.h"
#include "tool_catalog.h"

typedef struct MdoAgentFile {
    mdo_agent_v1 Agent;
    char Id[96];
    char* Name;
    char* Description;
    char* Prompt;
    char* Model;
    char* Effort;
    char* ConfigurationJson;
    char* PermissionProfile;
    char* Tools[128];
    char* Skills[128];
    bool UseCode;
} MdoAgentFile;


static inline void MdoAgentFileUnit(MdoAgentFile* File)
{
    size_t i;
    xrtFree(File->Name); xrtFree(File->Description); xrtFree(File->Prompt);
    xrtFree(File->Model); xrtFree(File->Effort);
    xrtFree(File->ConfigurationJson); xrtFree(File->PermissionProfile);
    for (i = 0u; i < File->Agent.ToolCount; ++i) xrtFree(File->Tools[i]);
    for (i = 0u; i < File->Agent.SkillCount; ++i) xrtFree(File->Skills[i]);
    memset(File, 0, sizeof(*File));
}

static inline bool MdoAgentFileList(const xvalue* Object, cstr Key,
    char** Items, size_t* Count)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    size_t i;
    *Count = 0u;
    if (Value == NULL) return true;
    if (xrtValueType(Value) == XVALUE_STRING) {
        xstrview Text;
        size_t Start = 0u;
        char Quote = 0;
        if (!xrtValueGetString(Value, &Text)) return false;
        Text = xrtStrTrim(Text);
        if (Text.Size >= 2u && Text.Data[0] == '[' && Text.Data[Text.Size - 1u] == ']')
            Text = xrtStrViewN(Text.Data + 1u, Text.Size - 2u);
        for (i = 0u; i <= Text.Size; ++i) {
            char c = i == Text.Size ? 0 : Text.Data[i];
            if (c == '\'' || c == '"') { if (Quote == 0) Quote = c; else if (Quote == c) Quote = 0; }
            if (i == Text.Size || (Quote == 0 && (c == ',' || c == ' '))) {
                xstrview Item = xrtStrTrim(xrtStrViewN(Text.Data + Start, i - Start));
                if (Item.Size != 0u) {
                    xvalue* Scalar = MdoPromptScalar(Item);
                    xstrview Parsed;
                    if (*Count == 128u || Scalar == NULL ||
                        !xrtValueGetString(Scalar, &Parsed) || Parsed.Size == 0u || Parsed.Size > 96u) {
                        xrtValueRelease(Scalar); return false;
                    }
                    Items[(*Count)++] = xrtStrDupN(Parsed.Data, Parsed.Size);
                    xrtValueRelease(Scalar);
                    if (Items[*Count - 1u] == NULL) return false;
                }
                Start = i + 1u;
            }
        }
        if (Quote != 0) return false;
        return true;
    }
    if (xrtValueType(Value) != XVALUE_ARRAY || xrtValueCount(Value) > 128u)
        return false;
    for (i = 0u; i < xrtValueCount(Value); ++i) {
        xstrview Text;
        if (!xrtValueGetString(xrtValueArrayGet(Value, i), &Text) ||
            Text.Size == 0u || Text.Size > 96u) return false;
        Items[(*Count)++] = xrtStrDupN(Text.Data, Text.Size);
        if (Items[*Count - 1u] == NULL) return false;
    }
    return true;
}

static inline bool MdoAgentFileParseAt(cstr Id, cstr Text, bool Main,cstr Root,
    MdoAgentFile* File, char* Error, size_t Capacity)
{
    xvalue* Document;
    size_t i;
    bool Ok;
    bool RequireVerification = false;
    memset(File, 0, sizeof(*File));
    if (!MdoExtensionIdValid(Id)) return false;
    Document = Root?MdoAgentConfigResolveAt(Id,Text,Main,Root,Error,Capacity):MdoAgentConfigResolve(Id,Text,Main,Error,Capacity);
    if (Document == NULL) return false;
    RequireVerification = MdoAgentConfigBool(Document,"require_verification_after_write",false);
    File->Agent.Size = sizeof(File->Agent);
    File->Agent.AbiVersion = MDO_MODULE_ABI_VERSION;
    snprintf(File->Id, sizeof(File->Id), "%s.%s", Main ? (!strcmp(Id,"default")?"mdo":"agent") : "subagent", Id);
    File->Name = MdoPromptString(Document, "name");
    File->Description = MdoPromptString(Document, "description");
    if (!File->Name) File->Name=xrtStrDup(Id);
    if (!File->Description) File->Description=xrtStrDup("Custom mdo role");
    File->Prompt = MdoPromptString(Document, "prompt");
    if ((!File->Prompt || !xrtStrTrim(xrtStrView(File->Prompt)).Size)) {
        xrtFree(File->Prompt); File->Prompt=xrtStrDup(MDO_AGENT_SYSTEM_PROMPT);
    }
    File->UseCode=MdoAgentConfigBool(Document,"code",false);
    File->PermissionProfile=MdoPromptString(Document,"permission_profile");
    if(File->PermissionProfile && !strcmp(File->PermissionProfile,"inherit")) {xrtFree(File->PermissionProfile);File->PermissionProfile=NULL;}
    File->ConfigurationJson=xrtJsonStringify(Document,false,NULL);
    File->Model = MdoPromptString(Document, "model");
    File->Effort = MdoPromptString(Document, "reasoning_effort");
    if (File->Effort == NULL) File->Effort = MdoPromptString(Document, "thoughtLevel");
    if (File->Model != NULL && (strcmp(File->Model, "inherit") == 0 || File->Model[0] == '\0')) {
        xrtFree(File->Model); File->Model = NULL;
    }
    Ok = File->Name != NULL && File->Name[0] != '\0' && strlen(File->Name) <= 256u &&
        File->Description != NULL && File->Description[0] != '\0' &&
        strlen(File->Description) <= 4096u && File->Prompt != NULL &&
        xrtStrTrim(xrtStrView(File->Prompt)).Size != 0u &&
        (File->Model == NULL || strlen(File->Model) <= 256u) &&
        (File->Effort == NULL || strlen(File->Effort) <= 64u) &&
        (MdoAgentConfigText(Document,"tools") &&
         (!strcmp(MdoAgentConfigText(Document,"tools"),"all") || !strcmp(MdoAgentConfigText(Document,"tools"),"inherit") || !strcmp(MdoAgentConfigText(Document,"tools"),"none")) ? true :
         MdoAgentFileList(Document, "tools", File->Tools, &File->Agent.ToolCount)) &&
        MdoAgentFileList(Document, "skills", File->Skills, &File->Agent.SkillCount);
    for (i = 0u; Ok && i < File->Agent.ToolCount; ++i) {
        cstr Canonical=MdoBuiltinToolCanonicalId(File->Tools[i]);
        if (strcmp(File->Tools[i],Canonical)) {
            char* Replacement=xrtStrDup(Canonical);
            if (!Replacement) { Ok=false; break; }
            xrtFree(File->Tools[i]); File->Tools[i]=Replacement;
        }
    }
    File->Agent.Id = File->Id;
    File->Agent.Name = File->Name; File->Agent.Description = File->Description;
    File->Agent.SystemPrompt = File->Prompt; File->Agent.Model = File->Model;
    File->Agent.ReasoningEffort = File->Effort;
    File->Agent.PermissionProfile=File->PermissionProfile;
    File->Agent.ConfigurationJson=File->ConfigurationJson;
    File->Agent.Tools = (const char* const*)File->Tools;
    File->Agent.Skills = (const char* const*)File->Skills;
    File->Agent.Flags = (Main?MDO_AGENT_MAIN:MDO_AGENT_SUBAGENT) | MDO_AGENT_ALLOW_BACKGROUND;
    cstr Mode=MdoAgentConfigText(Document,"mode");
    if (Mode) File->Agent.Flags=(File->Agent.Flags&~(MDO_AGENT_MAIN|MDO_AGENT_SUBAGENT)) |
        (!strcmp(Mode,"both")?MDO_AGENT_MAIN|MDO_AGENT_SUBAGENT:!strcmp(Mode,"primary")?MDO_AGENT_MAIN:MDO_AGENT_SUBAGENT);
    if (MdoAgentConfigBool(Document, "read_only",false)) File->Agent.Flags |= MDO_AGENT_READ_ONLY;
    if (MdoAgentConfigBool(Document, "allow_delegation",false)) File->Agent.Flags |= MDO_AGENT_ALLOW_DELEGATION;
    if (!MdoAgentConfigBool(Document,"allow_background",true)) File->Agent.Flags &= ~MDO_AGENT_ALLOW_BACKGROUND;
    cstr ToolMode=MdoAgentConfigText(Document,"tools");
    if ((ToolMode&&!strcmp(ToolMode,"none")) ||
        (MdoAgentConfigText(Document,"schema") && xrtValueType(MdoAgentConfigGet(Document,"tools"))==XVALUE_ARRAY))
        File->Agent.Flags |= MDO_AGENT_EXPLICIT_TOOLS;
    if (RequireVerification)
        File->Agent.Flags |= MDO_AGENT_REQUIRE_VERIFICATION_AFTER_WRITE;
    File->Agent.AllowedEffects = (File->Agent.Flags & MDO_AGENT_READ_ONLY)
        ? MDO_TOOL_EFFECT_READ | ((File->Agent.Flags & MDO_AGENT_ALLOW_DELEGATION)?MDO_TOOL_EFFECT_AGENT_DELEGATION:0u) : MDO_TOOL_EFFECT_ALL;
    File->Agent.MaxTurns = Main?128u:64u; File->Agent.TimeoutMilliseconds = 120000u;
    File->Agent.MaxFinalBytes = 64u * 1024u; File->Agent.MaxDepth = (File->Agent.Flags & MDO_AGENT_ALLOW_DELEGATION) ? (Main?4u:2u) : 1u;
    const xvalue* Runtime=MdoAgentConfigGet(Document,"runtime"); uint64 Number;
#define MDO_AGENT_LIMIT(key,member,maximum) do { if(!MdoAgentConfigNumber(Runtime,key,File->Agent.member,maximum,&Number)) Ok=false; else File->Agent.member=Number; } while(0)
    MDO_AGENT_LIMIT("max_turns",MaxTurns,1024u);
    MDO_AGENT_LIMIT("timeout_ms",TimeoutMilliseconds,86400000u);
    MDO_AGENT_LIMIT("max_final_bytes",MaxFinalBytes,16u*1024u*1024u);
    MDO_AGENT_LIMIT("max_depth",MaxDepth,16u);
    MDO_AGENT_LIMIT("context_tokens",ContextWindowTokens,UINT32_MAX);
    MDO_AGENT_LIMIT("input_tokens",MaxInputTokens,UINT32_MAX);
    MDO_AGENT_LIMIT("output_tokens",MaxOutputTokens,UINT32_MAX);
#undef MDO_AGENT_LIMIT
    if(!File->ConfigurationJson || !File->Agent.MaxTurns || !File->Agent.MaxFinalBytes || !File->Agent.MaxDepth) Ok=false;
    xrtValueRelease(Document);
    if (!Ok) {
        if (Error != NULL && Capacity != 0u)
            snprintf(Error, Capacity, "%s requires name, description and valid instructions/tool/Skill lists",Main?"Agent":"SubAgent");
        MdoAgentFileUnit(File);
    }
    return Ok;
}

static inline bool MdoAgentFileParse(cstr Id,cstr Text,bool Main,MdoAgentFile* File,char* Error,size_t Capacity)
{return MdoAgentFileParseAt(Id,Text,Main,NULL,File,Error,Capacity);}
#endif
