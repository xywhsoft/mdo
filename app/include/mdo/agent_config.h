#ifndef MDO_AGENT_CONFIG_H
#define MDO_AGENT_CONFIG_H

#include "prompt_file.h"

/* One normalized object is shared by Markdown, JSON and C definitions. The
 * frontmatter reader accepts JSON objects as scalar values; no YAML evaluator
 * or additional dependency is needed for advanced configuration. */
static inline const xvalue* MdoAgentConfigGet(const xvalue* Object, cstr Key)
{ return Object ? xrtValueObjectGet(Object, xrtStrView(Key)) : NULL; }

static inline cstr MdoAgentConfigText(const xvalue* Object, cstr Key)
{
    xstrview Text;
    return xrtValueGetString(MdoAgentConfigGet(Object, Key), &Text) ? Text.Data : NULL;
}

static inline bool MdoAgentConfigBool(const xvalue* Object, cstr Key, bool Default)
{
    const xvalue* Value = MdoAgentConfigGet(Object, Key);
    bool Result; xstrview Text;
    if (!Value) return Default;
    if (xrtValueGetBool(Value, &Result)) return Result;
    return xrtValueGetString(Value, &Text) && xrtStrEqual(Text, XRT_STR_LITERAL("true"));
}

static inline bool MdoAgentConfigNumber(const xvalue* Object, cstr Key,
    uint64 Default, uint64 Maximum, uint64* Output)
{
    const xvalue* Value = MdoAgentConfigGet(Object, Key);
    int64 Number = 0; xstrview Text;
    *Output = Default;
    if (!Value) return true;
    if (xrtValueGetInt(Value, &Number)) {
        if (Number < 0 || (uint64)Number > Maximum) return false;
        *Output = (uint64)Number; return true;
    }
    if (!xrtValueGetString(Value, &Text) || !Text.Size) return false;
    uint64 Parsed = 0;
    for (size_t i = 0; i < Text.Size; ++i) {
        unsigned Digit = (unsigned char)Text.Data[i] - '0';
        if (Digit > 9u || Parsed > Maximum / 10u ||
            (Parsed == Maximum / 10u && Digit > Maximum % 10u)) return false;
        Parsed = Parsed * 10u + Digit;
    }
    *Output = Parsed; return true;
}

static inline bool MdoAgentConfigSetText(xvalue* Object, cstr Key, cstr Text)
{
    xvalue* Value = xrtValueString(xrtStrView(Text ? Text : ""));
    bool Ok = Value && xrtValueObjectSet(Object, xrtStrView(Key), Value);
    xrtValueRelease(Value); return Ok;
}

static inline xvalue* MdoAgentConfigClone(const xvalue* Value)
{
    char* Json = Value ? xrtJsonStringify(Value, false, NULL) : NULL;
    xvalue* Copy = Json ? xrtJsonParse(xrtStrView(Json)) : NULL;
    xrtFree(Json); return Copy;
}

static inline bool MdoAgentConfigMerge(xvalue* Target, const xvalue* Patch, unsigned Depth)
{
    if (Depth > 16u || xrtValueType(Target) != XVALUE_OBJECT ||
        xrtValueType(Patch) != XVALUE_OBJECT) return false;
    for (size_t i = 0; i < xrtValueCount(Patch); ++i) {
        xstrview Key; const xvalue* Value;
        Value = xrtValueObjectAt(Patch, i, &Key);
        if (!Value) return false;
        const xvalue* Previous = xrtValueObjectGet(Target, Key);
        if (Previous && xrtValueType(Previous) == XVALUE_OBJECT &&
            xrtValueType(Value) == XVALUE_OBJECT) {
            if (!MdoAgentConfigMerge((xvalue*)Previous, Value, Depth + 1u)) return false;
        } else if (!xrtValueObjectSet(Target, Key, (xvalue*)Value)) return false;
    }
    return true;
}

static inline bool MdoAgentConfigListValid(const xvalue* Value, size_t Limit)
{
    if (!Value) return true;
    if (xrtValueType(Value) != XVALUE_ARRAY || xrtValueCount(Value) > Limit) return false;
    for (size_t i = 0; i < xrtValueCount(Value); ++i) {
        xstrview Text;
        if (!xrtValueGetString(xrtValueArrayGet(Value, i), &Text) ||
            !Text.Size || Text.Size > 256u || memchr(Text.Data, 0, Text.Size)) return false;
        for (size_t j = 0; j < i; ++j) {
            xstrview Other; xrtValueGetString(xrtValueArrayGet(Value, j), &Other);
            if (xrtStrEqual(Text, Other)) return false;
        }
    }
    return true;
}

static inline bool MdoAgentConfigPathValid(cstr Path)
{
    if (!Path || !Path[0] || strlen(Path)>1024u || Path[0]=='/' || strchr(Path, '\\') || strchr(Path, ':')) return false;
    const char* Part=Path;
    for (const char* P=Path;;++P) {
        unsigned char C=(unsigned char)*P;
        if (C && (C<32u || C==127u)) return false;
        if (!C || C=='/') {
            size_t N=(size_t)(P-Part);
            if (!N || (N==1u && Part[0]=='.') || (N==2u && Part[0]=='.' && Part[1]=='.')) return false;
            if (!C) break;
            Part=P+1;
        }
    }
    return true;
}

static inline bool MdoAgentConfigSourceKnown(cstr Id)
{
    static const char* Names[] = { "system", "skills.preload", "user_instructions",
        "reply_language", "environment", "external_tools", "skills.discovery",
        "knowledge", "memory.global", "memory.project", "file_access",
        "project_rules", "mcp.instructions" };
    for (size_t i = 0; i < sizeof(Names)/sizeof(*Names); ++i)
        if (!strcmp(Id, Names[i])) return true;
    return !strncmp(Id, "custom.", 7u) && MdoExtensionIdValid(Id + 7u);
}

static inline bool MdoAgentConfigValidate(const xvalue* Object, char* Error, size_t Capacity)
{
    cstr Failure = "invalid Agent configuration";
    cstr Schema = MdoAgentConfigText(Object, "schema");
    if (Schema && strcmp(Schema, "mdo.agent.v1")) { Failure = "unsupported Agent schema"; goto failed; }
    cstr Mode = MdoAgentConfigText(Object, "mode");
    if (Mode && strcmp(Mode,"primary") && strcmp(Mode,"subagent") && strcmp(Mode,"both")) {
        Failure = "mode must be primary, subagent or both"; goto failed;
    }
    static const char* Booleans[] = { "code", "read_only", "allow_delegation", "allow_background", "require_verification_after_write" };
    for (size_t i = 0; i < sizeof(Booleans)/sizeof(*Booleans); ++i) {
        const xvalue* Value = MdoAgentConfigGet(Object, Booleans[i]);
        bool B; xstrview Text;
        if (Value && !xrtValueGetBool(Value, &B) &&
            !(xrtValueGetString(Value, &Text) && (xrtStrEqual(Text,XRT_STR_LITERAL("true")) || xrtStrEqual(Text,XRT_STR_LITERAL("false"))))) {
            Failure = "Agent boolean fields require true or false"; goto failed;
        }
    }
    const xvalue* Context = MdoAgentConfigGet(Object, "context");
    if (Context && xrtValueType(Context) != XVALUE_OBJECT) { Failure = "context must be an object"; goto failed; }
    for (size_t i = 0; Context && i < xrtValueCount(Context); ++i) {
        xstrview Key; const xvalue* Spec; uint64 Number;
        Spec = xrtValueObjectAt(Context,i,&Key);
        if (!Spec || !MdoAgentConfigSourceKnown(Key.Data)) {
            Failure = "unknown context source"; goto failed;
        }
        if (xrtValueType(Spec) == XVALUE_BOOL) continue;
        if (xrtValueType(Spec) != XVALUE_OBJECT) { Failure = "context source must be a boolean or object"; goto failed; }
        cstr Role = MdoAgentConfigText(Spec,"role"), Refresh = MdoAgentConfigText(Spec,"refresh");
        if ((Role && strcmp(Role,"system") && strcmp(Role,"user")) ||
            (Refresh && strcmp(Refresh,"run") && strcmp(Refresh,"request")) ||
            !MdoAgentConfigNumber(Spec,"max_bytes",0,512u*1024u,&Number) ||
            !MdoAgentConfigNumber(Spec,"order",0,100000u,&Number) ||
            !MdoAgentConfigListValid(MdoAgentConfigGet(Spec,"include"),128u) ||
            !MdoAgentConfigListValid(MdoAgentConfigGet(Spec,"exclude"),128u)) {
            Failure = "invalid context role, refresh, budget or selection"; goto failed;
        }
        cstr File = MdoAgentConfigText(Spec,"file");
        if (File && (!MdoAgentConfigPathValid(File) || strlen(File) > 1024u)) {
            Failure = "context files require portable relative paths"; goto failed;
        }
    }
    static const char* Objects[] = { "runtime", "delegation", "model_options", "history" };
    for (size_t i = 0; i < sizeof(Objects)/sizeof(*Objects); ++i) {
        const xvalue* Value = MdoAgentConfigGet(Object,Objects[i]);
        if (Value && xrtValueType(Value) != XVALUE_OBJECT) { Failure = "advanced Agent options must be objects"; goto failed; }
    }
    const xvalue* Delegation = MdoAgentConfigGet(Object,"delegation");
    if (!MdoAgentConfigListValid(MdoAgentConfigGet(Delegation,"allow"),128u) ||
        !MdoAgentConfigListValid(MdoAgentConfigGet(Delegation,"deny"),128u) ||
        !MdoAgentConfigListValid(MdoAgentConfigGet(Object,"mcp_servers"),128u)) {
        Failure = "invalid Agent delegation or MCP server selection"; goto failed;
    }
    return true;
failed:
    if (Error && Capacity) snprintf(Error,Capacity,"%s",Failure);
    return false;
}

static inline xvalue* MdoAgentConfigResolveDepth(cstr Id, cstr Text, bool Main,
    const char** Stack, unsigned Depth, char* Error, size_t Capacity)
{
    xvalue* Own = NULL; xvalue* Base = NULL; char* ParentText = NULL;
    cstr Failure = "invalid, missing or cyclic Agent inheritance";
    if (Depth == 16u) goto failed;
    char Identity[96]; snprintf(Identity,sizeof(Identity),"%s:%s",Main?"agents":"subagents",Id);
    for (unsigned i=0;i<Depth;++i) if (!strcmp(Identity,Stack[i])) goto failed;
    Stack[Depth] = Identity;
    xstrview Trimmed = xrtStrTrim(xrtStrView(Text));
    Own = Trimmed.Size && Trimmed.Data[0]=='{' ? xrtJsonParse(Trimmed) : MdoPromptParse(Text,true,Error,Capacity);
    if (!Own || xrtValueType(Own)!=XVALUE_OBJECT) goto failed;
    cstr Parent = MdoAgentConfigText(Own,"extends");
    if (Parent && Parent[0]) {
        if (!strcmp(Parent,"mdo.general")) {
            Base=xrtValueObject();
        } else {
            bool ParentMain=Main; cstr ParentId=Parent;
            if (!strncmp(Parent,"agent.",6)) { ParentMain=true; ParentId=Parent+6; }
            else if (!strncmp(Parent,"subagent.",9)) { ParentMain=false; ParentId=Parent+9; }
            else if (!strcmp(Parent,"mdo.default")) { ParentMain=true; ParentId="default"; }
            if (!MdoExtensionIdValid(ParentId)) goto failed;
            char Path[160]; snprintf(Path,sizeof(Path),"%s/%s.md",ParentMain?"agents":"subagents",ParentId);
            ParentText=MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,NULL);
            if (!ParentText) goto failed;
            Base=MdoAgentConfigResolveDepth(ParentId,ParentText,ParentMain,Stack,Depth+1u,Error,Capacity);
            xrtFree(ParentText); ParentText=NULL;
        }
        if (!Base) goto failed;
        cstr Before=MdoAgentConfigText(Base,"prompt"), After=MdoAgentConfigText(Own,"prompt");
        cstr Mode=MdoAgentConfigText(Own,"prompt_mode");
        char* Joined=NULL;
        if (Before && After && After[0] && (!Mode || !strcmp(Mode,"append"))) {
            Joined=xrtFormat("%s\n\n%s",Before,After);
            if (!Joined || strlen(Joined)>MDO_EXTENSION_TEXT_LIMIT || !MdoAgentConfigSetText(Own,"prompt",Joined)) {xrtFree(Joined);goto failed;}
            xrtFree(Joined);
        } else if (Before && (!After || !After[0]) && !MdoAgentConfigSetText(Own,"prompt",Before)) goto failed;
        if (!MdoAgentConfigMerge(Base,Own,0)) goto failed;
        xrtValueRelease(Own);Own=Base;Base=NULL;
    }
    cstr PromptFile=MdoAgentConfigText(Own,"prompt_file");
    if (PromptFile) {
        if (!MdoAgentConfigPathValid(PromptFile)) {Failure="prompt_file requires a portable relative path";goto failed;}
        char* Path=xrtFormat("%s/%s",Main?"agents":"subagents",PromptFile);
        char* Body=Path?MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,NULL):NULL;xrtFree(Path);
        if (!Body || !MdoAgentConfigSetText(Own,"prompt",Body)) {xrtFree(Body);Failure="cannot read Agent prompt_file";goto failed;}
        xrtFree(Body);
    }
    if (!MdoAgentConfigValidate(Own,Error,Capacity)) goto failed;
    return Own;
failed:
    xrtFree(ParentText);xrtValueRelease(Own);xrtValueRelease(Base);
    if (Error && Capacity && !Error[0]) snprintf(Error,Capacity,"%s",Failure);
    return NULL;
}

static inline xvalue* MdoAgentConfigResolve(cstr Id,cstr Text,bool Main,char* Error,size_t Capacity)
{
    const char* Stack[16]={0};
    if (Error && Capacity) Error[0]=0;
    return MdoAgentConfigResolveDepth(Id,Text,Main,Stack,0,Error,Capacity);
}

#endif
