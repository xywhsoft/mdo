static bool MdoSkillsBoolean(const xvalue* Value, bool Default, bool* Result)
{
    xstrview Text={0};*Result=Default;
    if(!Value)return true;
    if(xrtValueGetBool(Value,Result))return true;
    if(!xrtValueGetString(Value,&Text))return false;
    if(xrtStrEqual(Text,XRT_STR_LITERAL("true"))){*Result=true;return true;}
    if(xrtStrEqual(Text,XRT_STR_LITERAL("false"))){*Result=false;return true;}
    return false;
}

static bool MdoSkillsArgumentName(MdoSkillEntry* Entry,xstrview Name,char* Error,size_t Capacity)
{
    bool Valid=Name.Size>0 && Name.Size<=64 && !MdoSkillsViewEqual(Name,"ARGUMENTS");
    for(size_t i=0;Valid && i<Name.Size;i++) {
        unsigned char c=(unsigned char)Name.Data[i];
        Valid=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(i>0&&c>='0'&&c<='9');
    }
    char* Copy=Valid?xrtStrDupN(Name.Data,Name.Size):NULL;
    if(Copy && MdoSkillsStringListAdd(&Entry->Arguments,Copy))return true;
    xrtFree(Copy);
    snprintf(Error,Capacity,"Skill arguments must be unique identifiers of 1-64 ASCII letters, digits or underscores, starting with a letter or underscore; ARGUMENTS is reserved (maximum 128 names)");
    return false;
}

static bool MdoSkillsArgumentNames(MdoSkillEntry* Entry,const xvalue* Value,char* Error,size_t Capacity)
{
    if(!Value)return true;
    xstrview Names={0};
    if(xrtValueGetString(Value,&Names)) {
        Names=xrtStrTrim(Names);
        if(Names.Size && Names.Data[0]=='[') {
            if(Names.Size<2||Names.Data[Names.Size-1]!=']')goto invalid;
            Names=xrtStrTrim(xrtStrViewN(Names.Data+1,Names.Size-2));
            if(!Names.Size)return true;
            size_t Start=0;
            for(size_t i=0;i<=Names.Size;i++)if(i==Names.Size||Names.Data[i]==',') {
                char* Name=NULL;
                bool Ok=MdoSkillsScalar(xrtStrTrim(xrtStrViewN(Names.Data+Start,i-Start)),64,&Name);
                if(Ok)Ok=MdoSkillsArgumentName(Entry,xrtStrView(Name),Error,Capacity);
                xrtFree(Name);if(!Ok)goto invalid;
                Start=i+1;
            }
            return true;
        }
        size_t Start=0;
        for(size_t i=0;i<=Names.Size;i++)if(i==Names.Size||Names.Data[i]==' '||Names.Data[i]=='\t') {
            if(i>Start&&!MdoSkillsArgumentName(Entry,xrtStrViewN(Names.Data+Start,i-Start),Error,Capacity))return false;
            Start=i+1;
        }
        return true;
    }
    if(xrtValueType(Value)==XVALUE_ARRAY) {
        for(size_t i=0;i<xrtValueCount(Value);i++) {
            if(!xrtValueGetString(xrtValueArrayGet(Value,i),&Names))break;
            if(!MdoSkillsArgumentName(Entry,Names,Error,Capacity))return false;
            if(i+1==xrtValueCount(Value))return true;
        }
        if(xrtValueCount(Value)==0)return true;
    }
invalid:
    snprintf(Error,Capacity,"Skill arguments must be a space-separated string or a list of identifier strings");return false;
}

static bool MdoSkillsCompatibility(MdoSkillEntry* Entry,const xvalue* Doc,char* Error,size_t Capacity)
{
    static const char* const Unsupported[]={"context","agent","model","effort","hooks","shell","paths","disallowed-tools"};
    for(size_t i=0;i<sizeof(Unsupported)/sizeof(Unsupported[0]);i++)if(xrtValueObjectGet(Doc,xrtStrView(Unsupported[i]))) {
        snprintf(Error,Capacity,"Skill field '%s' requires unsupported execution behavior; use an Agent/SubAgent configuration instead",Unsupported[i]);return false;
    }
    bool Disabled=false;
    const xvalue* Meta=xrtValueObjectGet(Doc,XRT_STR_LITERAL("metadata"));
    const xvalue* Automatic=Meta?xrtValueObjectGet(Meta,XRT_STR_LITERAL("opencode/autoinvoke")):NULL;
    bool Auto=true;
    if(!MdoSkillsBoolean(xrtValueObjectGet(Doc,XRT_STR_LITERAL("disable-model-invocation")),false,&Disabled) ||
       !MdoSkillsBoolean(xrtValueObjectGet(Doc,XRT_STR_LITERAL("user-invocable")),true,&Entry->UserInvocable) ||
       !MdoSkillsBoolean(Automatic,true,&Auto)) {
        snprintf(Error,Capacity,"Skill invocation controls must be true or false");return false;
    }
    Entry->AutoInvoke=Auto&&!Disabled;
    if(!MdoSkillsArgumentNames(Entry,xrtValueObjectGet(Doc,XRT_STR_LITERAL("arguments")),Error,Capacity))return false;
    Entry->ArgumentHint=MdoPromptString(Doc,"argument-hint");
    if(Entry->ArgumentHint && (strlen(Entry->ArgumentHint)>256 || strchr(Entry->ArgumentHint,'\n'))) {
        snprintf(Error,Capacity,"Skill argument-hint must be a single line of at most 256 bytes");return false;
    }
    char* When=MdoPromptString(Doc,"when_to_use");
    if(When) {
        char* Description=xrtFormat("%s %s",Entry->Description,When);xrtFree(When);
        if(!Description || strlen(Description)>MDO_SKILL_DESCRIPTION_LIMIT){xrtFree(Description);return false;}
        xrtFree(Entry->Description);Entry->Description=Description;
    }
    const xvalue* Allowed=xrtValueObjectGet(Doc,XRT_STR_LITERAL("allowed-tools"));
    if(Allowed && xrtValueObjectGet(Doc,XRT_STR_LITERAL("tools"))) {
        snprintf(Error,Capacity,"Use tools or allowed-tools, not both");return false;
    }
    if(Allowed && !xrtValueObjectGet(Doc,XRT_STR_LITERAL("tools"))) {
        xstrview Text={0};char* Json=NULL;
        if(xrtValueType(Allowed)==XVALUE_ARRAY)Json=xrtJsonStringify(Allowed,false,NULL);
        if(Json) {
            bool Ok=MdoSkillsInlineList(Entry,MDO_SKILL_FIELD_TOOLS,xrtStrView(Json),Error,Capacity);xrtFree(Json);if(!Ok)return false;
        } else if(xrtValueGetString(Allowed,&Text)) {
            size_t Start=0;
            for(size_t i=0;i<=Text.Size;i++)if(i==Text.Size||Text.Data[i]==' '||Text.Data[i]==',') {
                if(i>Start && !MdoSkillsListValue(Entry,MDO_SKILL_FIELD_TOOLS,xrtStrViewN(Text.Data+Start,i-Start),Error,Capacity))return false;
                Start=i+1;
            }
        } else {snprintf(Error,Capacity,"allowed-tools must contain plain tool IDs; command permission patterns are unsupported");return false;}
        for(size_t i=0;i<Entry->Tools.Count;i++) {
            cstr Canonical=MdoBuiltinToolCanonicalId(Entry->Tools.Items[i]);
            char* Copy=xrtStrDup(Canonical);if(!Copy)return false;
            xrtFree(Entry->Tools.Items[i]);Entry->Tools.Items[i]=Copy;
        }
    }
    return true;
}

#include "arguments.inc.c"
