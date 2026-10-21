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

static bool MdoSkillsCompatibility(MdoSkillEntry* Entry,const xvalue* Doc,char* Error,size_t Capacity)
{
    static const char* const Unsupported[]={"context","agent","model","effort","hooks","shell","paths","disallowed-tools","arguments"};
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

char* MdoSkillRender(cstr Text,cstr Directory,cstr Arguments)
{
    static const char* const Keys[]={"${MDO_SKILL_DIR}","${CLAUDE_SKILL_DIR}","${ZCODE_SKILL_DIR}","$ARGUMENTS"};
    const char* Values[]={Directory,Directory,Directory,Arguments?Arguments:""};
    if(!Text||!Directory)return NULL;
    size_t Capacity=strlen(Text)+1;
    for(size_t i=0;i<4;i++)for(const char* p=Text;(p=strstr(p,Keys[i]))!=NULL;p+=strlen(Keys[i])) {
        size_t Added=strlen(Values[i]);if(Added>2u*1024u*1024u-Capacity)return NULL;Capacity+=Added;
    }
    char* Result=xrtMalloc(Capacity);if(!Result)return NULL;
    char* Out=Result;
    for(const char* p=Text;*p;) {
        bool Replaced=false;
        for(size_t i=0;i<4;i++)if(!strncmp(p,Keys[i],strlen(Keys[i]))) {
            size_t n=strlen(Values[i]);memcpy(Out,Values[i],n);Out+=n;p+=strlen(Keys[i]);Replaced=true;break;
        }
        if(!Replaced)*Out++=*p++;
    }
    *Out=0;return Result;
}
