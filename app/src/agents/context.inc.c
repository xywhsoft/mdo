/* Role context is data until it is rendered. This same plan is exposed to C
 * programs and the inspector; it is never reconstructed from a UI estimate. */
static bool MdoAgentsPattern(cstr Pattern,cstr Id)
{
    size_t N=Pattern?strlen(Pattern):0;
    return Pattern && Id && (N && Pattern[N-1]=='*' ? !strncmp(Pattern,Id,N-1) : !strcmp(Pattern,Id));
}
static bool MdoAgentsListMatch(const xvalue* List,cstr Id,bool Default)
{
    if(!List)return Default;
    for(size_t I=0;I<xrtValueCount(List);I++){
        xstrview Text;if(xrtValueGetString(xrtValueArrayGet(List,I),&Text)&&MdoAgentsPattern(Text.Data,Id))return true;
    }
    return false;
}
static bool MdoAgentsSelection(const xvalue* Spec,cstr Id)
{
    return MdoAgentsListMatch(MdoAgentConfigGet(Spec,"include"),Id,true) &&
        !MdoAgentsListMatch(MdoAgentConfigGet(Spec,"exclude"),Id,false);
}
static const xvalue* MdoAgentsSourceSpec(const xvalue* Config,cstr Id)
{return MdoAgentConfigGet(MdoAgentConfigGet(Config,"context"),Id);}
static bool MdoAgentsSourceEnabled(const xvalue* Spec,bool Default)
{
    bool Enabled;
    if(!Spec)return Default;
    if(xrtValueGetBool(Spec,&Enabled))return Enabled;
    return MdoAgentConfigBool(Spec,"enabled",Default);
}
static bool MdoAgentsPlanPut(xvalue* Object,cstr Key,cstr Text)
{return MdoAgentConfigSetText(Object,Key,Text);}
static char* MdoAgentsRenderTemplate(cstr Text,const xvalue* Variables,xwork_error* Error)
{
    char* Output=xrtStrDup("");const char* At=Text?Text:"";const char* Slot;
    if(!Output)return NULL;
    while((Slot=strstr(At,"{{"))){
        const char* End=strstr(Slot+2,"}}");
        if(!End || End-Slot>128)goto invalid;
        char Key[128];size_t N=(size_t)(End-Slot-2);memcpy(Key,Slot+2,N);Key[N]=0;
        cstr Replacement=MdoAgentConfigText(Variables,Key);
        if(!Replacement)goto invalid;
        char* Prefix=xrtStrDupN(At,(size_t)(Slot-At));
        bool Ok=Prefix && MdoAgentsAppendPrompt(&Output,Prefix,strlen(Prefix),Error) &&
            MdoAgentsAppendPrompt(&Output,Replacement,strlen(Replacement),Error);
        xrtFree(Prefix);if(!Ok){xrtFree(Output);return NULL;}At=End+2;
    }
    if(!MdoAgentsAppendPrompt(&Output,At,strlen(At),Error)){xrtFree(Output);return NULL;}
    return Output;
invalid:
    xrtFree(Output);MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"unknown or unclosed Agent template variable");return NULL;
}
static char* MdoAgentsContextRead(cstr Workspace,cstr Path)
{
    if(!MdoAgentConfigPathValid(Path))return NULL;
    xroot Root=xrtRootOpen(Workspace);xfileoptions Options;xrtFileOptionsInit(&Options);
    Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
    xfile File=Root?xrtRootFileOpen(Root,Path,&Options):NULL;
    char* Text=NULL;uint64 Bytes=0;
    if(File && xrtFileSize(File,&Bytes) && Bytes<=MDO_EXTENSION_TEXT_LIMIT){
        Text=xrtMalloc((size_t)Bytes+1u);
        if(Text && !xrtReadFull(File,Text,(size_t)Bytes,NULL)){xrtFree(Text);Text=NULL;}
        if(Text){Text[Bytes]=0;if(memchr(Text,0,(size_t)Bytes)||!xrtUtf8Valid(xrtStrViewN(Text,(size_t)Bytes),NULL)){xrtFree(Text);Text=NULL;}}
    }
    if(File && !xrtClose(File)){xrtFree(Text);Text=NULL;}
    if(Root)xrtRootClose(Root);
    return Text;
}
/* The existing publishers emit a single JSON metadata line. Filter that
 * structured line, preserving their surrounding guidance and escaping. */
static bool MdoAgentsFilterMetadata(char** Text,const xvalue* Spec)
{
    if(!MdoAgentConfigGet(Spec,"include")&&!MdoAgentConfigGet(Spec,"exclude"))return true;
    char* Line=*Text;
    while(Line && *Line && Line[0]!='[' && Line[0]!='{'){Line=strchr(Line,'\n');if(Line)++Line;}
    if(!Line || !*Line)return true;
    char* End=strchr(Line,'\n');if(!End)End=Line+strlen(Line);
    xvalue* Parsed=xrtJsonParse(xrtStrViewN(Line,(size_t)(End-Line)));
    const xvalue* Items=Parsed && xrtValueType(Parsed)==XVALUE_ARRAY?Parsed:MdoAgentConfigGet(Parsed,"items");
    xvalue* Selected=xrtValueArray();bool Ok=Parsed && Items && Selected;
    for(size_t I=0;Ok && I<xrtValueCount(Items);I++){
        const xvalue* Item=xrtValueArrayGet(Items,I);
        cstr Id=MdoAgentConfigText(Item,"id");if(!Id)Id=MdoAgentConfigText(Item,"name");
        if(Id && MdoAgentsSelection(Spec,Id))Ok=xrtValueArrayAppend(Selected,(xvalue*)Item);
    }
    xvalue* Output=Selected;
    if(Ok && xrtValueType(Parsed)==XVALUE_OBJECT){
        Ok=xrtValueObjectSet(Parsed,XRT_STR_LITERAL("items"),Selected) &&
            xrtValueObjectSetNew(Parsed,XRT_STR_LITERAL("total"),xrtValueUInt(xrtValueCount(Selected)));
        Output=Parsed;
    }
    char* Json=Ok?xrtJsonStringify(Output,false,NULL):NULL;
    char* Prefix=Json?xrtStrDupN(*Text,(size_t)(Line-*Text)):NULL;
    char* Next=Prefix?xrtFormat("%s%s%s",Prefix,Json,End):NULL;
    Ok=Next!=NULL;if(Ok){xrtFree(*Text);*Text=Next;}
    xrtFree(Prefix);xrtFree(Json);xrtValueRelease(Selected);xrtValueRelease(Parsed);return Ok;
}
static bool MdoAgentsPlanSource(xvalue* Plan,cstr Id,char* Text,const xvalue* Spec,
    uint64 Order,bool Enabled,cstr Resource,xwork_error* Error)
{
    xvalue* Item=xrtValueObject();
    cstr Replacement=MdoAgentConfigText(Spec,"text"),Template=MdoAgentConfigText(Spec,"template");
    cstr Append=MdoAgentConfigText(Spec,"append");
    bool Ok=Item && Text;
    if(Ok && Enabled && Replacement){xrtFree(Text);Text=xrtStrDup(Replacement);Ok=Text!=NULL;}
    if(Ok && Enabled && Template){char* Rendered=MdoAgentsRenderTemplate(Template,MdoAgentConfigGet(Plan,"variables"),Error);xrtFree(Text);Text=Rendered;Ok=Text!=NULL;}
    if(Ok && Enabled && Append)Ok=MdoAgentsAppendPrompt(&Text,Append,strlen(Append),Error);
    uint64 Limit=0;
    if(Ok)Ok=MdoAgentConfigNumber(Spec,"max_bytes",0,MDO_AGENT_PROMPT_LIMIT,&Limit) &&
        MdoAgentConfigNumber(Spec,"order",Order,100000,&Order);
    size_t Original=Text?strlen(Text):0,Bytes=Original;
    if(Limit && Bytes>Limit){Bytes=(size_t)Limit;while(Bytes && ((unsigned char)Text[Bytes]&0xc0)==0x80)--Bytes;Text[Bytes]=0;}
    cstr Role=MdoAgentConfigText(Spec,"role"),Refresh=MdoAgentConfigText(Spec,"refresh");
    if(Ok)Ok=MdoAgentsPlanPut(Item,"id",Id) && MdoAgentsPlanPut(Item,"source",Resource?Resource:Id) &&
        MdoAgentsPlanPut(Item,"text",Text) && MdoAgentsPlanPut(Item,"role",Role?Role:"system") &&
        MdoAgentsPlanPut(Item,"refresh",Refresh?Refresh:"run") &&
        xrtValueObjectSetNew(Item,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled)) &&
        xrtValueObjectSetNew(Item,XRT_STR_LITERAL("order"),xrtValueUInt(Order)) &&
        xrtValueObjectSetNew(Item,XRT_STR_LITERAL("original_bytes"),xrtValueUInt(Original)) &&
        xrtValueObjectSetNew(Item,XRT_STR_LITERAL("truncated"),xrtValueBool(Bytes<Original)) &&
        xrtValueArrayAppend((xvalue*)MdoAgentConfigGet(Plan,"sources"),Item);
    xrtFree(Text);xrtValueRelease(Item);
    if(!Ok && (!Error || Error->eCode==XWORK_ERROR_NONE))MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"cannot construct Agent context source");
    return Ok;
}
static bool MdoAgentsFrozenSource(MdoAgentInstance* Instance,xvalue* Plan,cstr Id,const xvalue* Spec,bool* Frozen)
{
    *Frozen=false;
    cstr Refresh=MdoAgentConfigText(Spec,"refresh");
    if(!Instance->SourcePlan || (Refresh&&!strcmp(Refresh,"request")))return true;
    const xvalue* Sources=MdoAgentConfigGet(Instance->SourcePlan,"sources");
    for(size_t I=0;I<xrtValueCount(Sources);I++){
        const xvalue* Source=xrtValueArrayGet(Sources,I);cstr SourceId=MdoAgentConfigText(Source,"id");
        if(SourceId&&!strcmp(SourceId,Id)){*Frozen=true;return xrtValueArrayAppend((xvalue*)MdoAgentConfigGet(Plan,"sources"),(xvalue*)Source);}
    }
    return true;
}
static bool MdoAgentsContextCollect(MdoAgentOwner* Owner,MdoAgentInstance* Instance,
    const MdoModuleAgentInfo* Definition,xwork_agent_extension_context* Native,xvalue** Output,xwork_error* Error)
{
    xvalue* Plan=xrtValueObject();xvalue* Variables=xrtValueObject();xvalue* Sources=xrtValueArray();
    bool Ok=Plan && Variables && Sources && MdoAgentsPlanPut(Plan,"schema","mdo.agent.context.v1") &&
        MdoAgentsPlanPut(Plan,"agent_id",Definition->Id) &&
        MdoAgentsPlanPut(Variables,"agent.id",Definition->Id) && MdoAgentsPlanPut(Variables,"project.id",Owner->ProductProject) &&
        MdoAgentsPlanPut(Variables,"session.id",Owner->ProductSession) && MdoAgentsPlanPut(Variables,"workspace.path",Owner->Workspace) &&
        xrtValueObjectSet(Plan,XRT_STR_LITERAL("variables"),Variables) && xrtValueObjectSet(Plan,XRT_STR_LITERAL("sources"),Sources);
    xrtValueRelease(Variables);xrtValueRelease(Sources);
    if(!Ok)goto done;
    if(Instance->LegacyPrompt){
        Ok=MdoAgentsPlanSource(Plan,"system",xrtStrDup(Instance->LegacyPrompt),NULL,0,true,"recovered-legacy-context",Error);
        goto done;
    }
    static const char* Ids[]={"system","skills.preload","user_instructions","reply_language","environment","external_tools",
        "skills.discovery","knowledge","memory.global","memory.project","file_access","project_rules","mcp.instructions"};
    MdoConfigAgentSettings Settings={0};Settings.Size=sizeof(Settings);
    if(!MdoConfigGetAgentSettings(&Settings)){Ok=false;goto done;}
    for(size_t I=0;Ok && I<sizeof(Ids)/sizeof(*Ids);I++){
        cstr Id=Ids[I];const xvalue* Spec=MdoAgentsSourceSpec(Instance->Configuration,Id);
        bool Frozen=false;Ok=MdoAgentsFrozenSource(Instance,Plan,Id,Spec,&Frozen);if(!Ok||Frozen)continue;
        bool Default=strcmp(Id,"environment") && strcmp(Id,"project_rules") && strcmp(Id,"mcp.instructions");
        if(!strcmp(Id,"skills.discovery"))Default=MdoAgentsToolSelected(Definition,"skill");
        if(!strncmp(Id,"memory.",7))Default=Settings.MemoryEnabled;
        bool Enabled=MdoAgentsSourceEnabled(Spec,Default);
        char* Text=xrtStrDup("");
        if(!Text){Ok=false;break;}
        if(Enabled){
            if(!strcmp(Id,"system")){xrtFree(Text);Text=xrtStrDup(Definition->SystemPrompt);}
            else if(!strcmp(Id,"skills.preload")){
                MdoModuleAgentInfo Selected=*Definition;const char* Names[128];size_t N=0;
                for(size_t J=0;J<Definition->SkillCount;J++)if(MdoAgentsSelection(Spec,Definition->Skills[J]))Names[N++]=Definition->Skills[J];
                Selected.SystemPrompt="";Selected.Skills=Names;Selected.SkillCount=N;
                xrtFree(Text);Text=MdoAgentsComposePrompt(&Selected,Owner->Skills,Error);
            } else if(!strcmp(Id,"user_instructions")){xrtFree(Text);Text=MdoConfigAgentInstructions();}
            else if(!strcmp(Id,"reply_language"))Ok=MdoAgentsReplyLanguage(&Text,Error);
            else if(!strcmp(Id,"external_tools"))Ok=MdoToolPrompt(&Text) && MdoAgentsFilterMetadata(&Text,Spec);
            else if(!strcmp(Id,"skills.discovery"))Ok=MdoSkillCatalogAppendPrompt(Owner->Skills,&Text) && MdoAgentsFilterMetadata(&Text,Spec);
            else if(!strcmp(Id,"knowledge"))Ok=MdoKnowledgeAppendPrompt(&Text) && MdoAgentsFilterMetadata(&Text,Spec);
            else if(!strcmp(Id,"memory.global")){xrtFree(Text);Text=MdoMemoryBuildPrompt(NULL,NULL,NULL,Error);}
            else if(!strcmp(Id,"memory.project")){
                if(Owner->ProductProject[0]){xrtFree(Text);Text=MdoMemoryBuildProjectPrompt(Owner->ProductProject,Error);}
            } else if(!strcmp(Id,"file_access")){
                xrtFree(Text);Text=xrtStrDup("File tools follow live user permissions. In full access mode read may use native paths outside the workspace. Otherwise use workspace paths or explicit @alias/ mounts. Use @memory-global/ and @memory-project/ for memory, @knowledge/<id>/ for reference libraries. Read continuation offsets are authoritative.");
            } else if(!strcmp(Id,"environment")){
                xrtFree(Text);Text=xrtFormat("Workspace: %s\nProject: %s\nRole: %s\nUTC Unix microseconds: %lld",Owner->Workspace,Owner->ProductProject,Definition->Id,(long long)MdoUtcUs());
            } else if(!strcmp(Id,"project_rules")){
                cstr File=MdoAgentConfigText(Spec,"file");
                char* Read=MdoAgentsContextRead(Owner->Workspace,File?File:"AGENTS.md");
                if(Read){xrtFree(Text);Text=Read;}
                else if(MdoAgentConfigBool(Spec,"required",false)){MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"required project rule file cannot be read");Ok=false;}
                else xrtClearError();
            } else if(!strcmp(Id,"mcp.instructions")){
                const xvalue* Selection=MdoAgentConfigGet(Instance->Configuration,"mcp_servers");
                const char* Names[128];size_t Count=xrtValueCount(Selection);
                for(size_t J=0;J<Count;J++){xstrview Name;xrtValueGetString(xrtValueArrayGet(Selection,J),&Name);Names[J]=Name.Data;}
                xrtFree(Text);Text=MdoMcpRolePrompt(Owner->Workspace,Names,Count,Selection!=NULL,Error);
            }
            cstr File=MdoAgentConfigText(Spec,"file");
            if(File && strcmp(Id,"project_rules")){
                cstr Origin=MdoAgentConfigText(Spec,"origin");
                char* Path=xrtFormat("%s/%s",Origin?Origin:!strncmp(Definition->Id,"subagent.",9)?"subagents":"agents",File);
                char* Read=Path?MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,NULL):NULL;xrtFree(Path);
                if(Read){xrtFree(Text);Text=Read;}
                else if(MdoAgentConfigBool(Spec,"required",true)){MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"required role context file cannot be read");Ok=false;}
                else xrtClearError();
            }
        }
        if(Ok)Ok=MdoAgentsPlanSource(Plan,Id,Text,Spec,I*100u,Enabled,Id,Error);else xrtFree(Text);
    }
    const xvalue* Context=MdoAgentConfigGet(Instance->Configuration,"context");
    for(size_t I=0;Ok && I<xrtValueCount(Context);I++){
        xstrview Key;const xvalue* Spec=xrtValueObjectAt(Context,I,&Key);
        if(strncmp(Key.Data,"custom.",7))continue;
        bool Frozen=false;Ok=MdoAgentsFrozenSource(Instance,Plan,Key.Data,Spec,&Frozen);if(!Ok||Frozen)continue;
        char* Text=xrtStrDup("");cstr File=MdoAgentConfigText(Spec,"file");
        bool Enabled=MdoAgentsSourceEnabled(Spec,true);
        if(Enabled && File){cstr Origin=MdoAgentConfigText(Spec,"origin");char* Path=xrtFormat("%s/%s",Origin?Origin:!strncmp(Definition->Id,"subagent.",9)?"subagents":"agents",File);xrtFree(Text);Text=Path?MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,NULL):NULL;xrtFree(Path);if(!Text&&!MdoAgentConfigBool(Spec,"required",true)){xrtClearError();Text=xrtStrDup("");}}
        Ok=MdoAgentsPlanSource(Plan,Key.Data,Text,Spec,2000u+I,Enabled,File?File:Key.Data,Error);
    }
    (void)Native;
done:
    if(!Ok){xrtValueRelease(Plan);return false;}*Output=Plan;return true;
}
static int MdoAgentsContextCompare(const void* A,const void* B)
{
    uint64 Left=0,Right=0;MdoAgentConfigNumber(*(const xvalue**)A,"order",0,100000,&Left);
    MdoAgentConfigNumber(*(const xvalue**)B,"order",0,100000,&Right);
    if(Left!=Right)return Left<Right?-1:1;
    cstr L=MdoAgentConfigText(*(const xvalue**)A,"id"),R=MdoAgentConfigText(*(const xvalue**)B,"id");
    return strcmp(L?L:"",R?R:"");
}
static char* MdoAgentsContextRender(const xvalue* Plan,cstr Role,xwork_error* Error)
{
    const xvalue* Sources=MdoAgentConfigGet(Plan,"sources");const xvalue* Sorted[128];size_t N=xrtValueCount(Sources);
    if(xrtValueType(Sources)!=XVALUE_ARRAY || N>128){MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"Agent context requires at most 128 sources");return NULL;}
    for(size_t I=0;I<N;I++)Sorted[I]=xrtValueArrayGet(Sources,I);
    qsort(Sorted,N,sizeof(*Sorted),MdoAgentsContextCompare);
    char* Text=xrtStrDup("");
    for(size_t I=0;Text && I<N;I++){
        cstr SourceRole=MdoAgentConfigText(Sorted[I],"role"),Body=MdoAgentConfigText(Sorted[I],"text");
        if(!SourceRole || (strcmp(SourceRole,"system") && strcmp(SourceRole,"user")) || !Body || !xrtUtf8Valid(xrtStrView(Body),NULL)){
            xrtFree(Text);MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"invalid context source role or UTF-8 text");return NULL;
        }
        if(!strcmp(SourceRole,Role) && MdoAgentConfigBool(Sorted[I],"enabled",true) && Body[0]){
            if((Text[0] && !MdoAgentsAppendPrompt(&Text,"\n\n",2,Error)) || !MdoAgentsAppendPrompt(&Text,Body,strlen(Body),Error)){xrtFree(Text);return NULL;}
        }
    }
    return Text;
}
