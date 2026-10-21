/* Read-only previews and explicit, non-destructive migration from fixed agent
 * configuration paths. No source configuration or credential file is modified. */
typedef struct McpImportSource { const char* Provider;const char* User;const char* Project;bool Toml; } McpImportSource;
static bool McpImportUInt(const xvalue* Value,uint64* Output)
{
    int64 Signed=0;
    return xrtValueGetUInt(Value,Output)||(xrtValueGetInt(Value,&Signed)&&Signed>=0&&(*Output=(uint64)Signed,true));
}
static const McpImportSource McpImportSources[]={
    {"claude",".claude.json",".mcp.json",false},
    {"claude",".claude/settings.json",NULL,false},
    {"codex",".codex/config.toml",".codex/config.toml",true},
    {"zcode",".zcode/cli/config.json",".zcode/config.json",false},
    {"opencode",".config/opencode/opencode.json","opencode.json",false},
    {"agents",".agents/mcp.json",".agents/mcp.json",false}
};

static void McpTomlSpace(const char** P)
{
    for(;;){while(**P==' '||**P=='\t'||**P=='\r'||**P=='\n')++*P;
        if(**P!='#')return;
        while(**P&&**P!='\n')++*P;}
}

/* Deliberately bounded TOML value subset used by MCP: strings, numbers,
 * booleans, arrays and inline maps. Unsupported expressions reject that server
 * instead of silently changing its command or dropping credentials. */
static xvalue* McpTomlValue(const char** P,unsigned Depth)
{
    if(Depth>8)return NULL;
    McpTomlSpace(P);const char* Start=*P;char Quote=**P;
    if(Quote=='\''||Quote=='"') {
        ++*P;while(**P&&**P!=Quote&&**P!='\n') {
            if(Quote=='"'&&**P=='\\'&&(*P)[1])++*P;
            ++*P;
        }
        if(**P!=Quote)return NULL;
        ++*P;
        return Quote=='\''?xrtValueString(xrtStrViewN(Start+1,(size_t)(*P-Start-2))):
            xrtJsonParse(xrtStrViewN(Start,(size_t)(*P-Start)));
    }
    if(Quote=='['||Quote=='{') {
        char End=Quote=='['?']':'}';xvalue* Value=Quote=='['?xrtValueArray():xrtValueObject();++*P;
        for(size_t Count=0;Value&&Count<256;Count++) {
            McpTomlSpace(P);if(**P==End){++*P;return Value;}
            xvalue* Key=NULL;xstrview Name={0};
            if(Quote=='{') {
                if(**P=='\''||**P=='"'){Key=McpTomlValue(P,Depth+1);if(!xrtValueGetString(Key,&Name)){xrtValueRelease(Key);break;}}
                else {const char* K=*P;while((**P>='a'&&**P<='z')||(**P>='A'&&**P<='Z')||(**P>='0'&&**P<='9')||**P=='_'||**P=='-')++*P;
                    Name=xrtStrViewN(K,(size_t)(*P-K));}
                McpTomlSpace(P);if(!Name.Size||**P!='='){xrtValueRelease(Key);break;}++*P;
            }
            xvalue* Child=McpTomlValue(P,Depth+1);
            bool Ok=Child&&(Quote=='['?xrtValueArrayAppend(Value,Child):
                !xrtValueObjectHas(Value,Name)&&xrtValueObjectSet(Value,Name,Child));
            xrtValueRelease(Key);xrtValueRelease(Child);if(!Ok)break;
            McpTomlSpace(P);if(**P==End){++*P;return Value;}if(**P!=',')break;++*P;
        }
        xrtValueRelease(Value);return NULL;
    }
    while(**P&&**P!='\n'&&**P!='\r'&&**P!='#'&&**P!=','&&**P!=']'&&**P!='}'&&**P!=' '&&**P!='\t')++*P;
    if(*P==Start)return NULL;
    return xrtJsonParse(xrtStrViewN(Start,(size_t)(*P-Start)));
}

static xvalue* McpTomlServers(char* Text)
{
    xvalue* Servers=xrtValueObject();xvalue* Server=NULL,*Current=NULL;const char* P=Text;
    while(Servers&&*P) {
        McpTomlSpace(&P);if(!*P)break;
        if(*P=='[') {
            const char* End=strchr(P,']');if(!End){xrtValueRelease(Servers);return NULL;}
            Current=Server=NULL;
            if(End-P>=13&&!memcmp(P,"[mcp_servers.",13)) {
                const char* Name=P+13;const char* Suffix=Name;char Quote=0;
                while(Suffix<End){if((*Suffix=='\''||*Suffix=='"')&&!Quote)Quote=*Suffix;else if(*Suffix==Quote)Quote=0;else if(*Suffix=='.'&&!Quote)break;Suffix++;}
                xstrview Id=xrtStrViewN(Name,(size_t)(Suffix-Name));
                if(Id.Size>1&&(Id.Data[0]=='\''||Id.Data[0]=='"')&&Id.Data[Id.Size-1]==Id.Data[0])Id=xrtStrViewN(Id.Data+1,Id.Size-2);
                if(Id.Size&&Id.Size<=192) {
                    Server=(xvalue*)xrtValueObjectGet(Servers,Id);
                    if(!Server){Server=xrtValueObject();if(!Server||!xrtValueObjectSetNew(Servers,Id,Server)){xrtValueRelease(Servers);return NULL;}}
                    Current=Server;
                    if(Suffix<End) {
                        xstrview Child=xrtStrViewN(Suffix+1,(size_t)(End-Suffix-1));
                        Current=(xvalue*)xrtValueObjectGet(Server,Child);
                        if(!Current){Current=xrtValueObject();if(!Current||!xrtValueObjectSetNew(Server,Child,Current)){xrtValueRelease(Servers);return NULL;}}
                    }
                }
            }
            P=End+1;
        } else if(Current) {
            const char* Key=P;
            while(*P&&*P!='='&&*P!='\n')++P;
            if(*P=='=') {
                xstrview Name=xrtStrTrim(xrtStrViewN(Key,(size_t)(P-Key)));
                if(Name.Size>1&&(Name.Data[0]=='\''||Name.Data[0]=='"')&&Name.Data[Name.Size-1]==Name.Data[0])Name=xrtStrViewN(Name.Data+1,Name.Size-2);
                ++P;xvalue* Value=McpTomlValue(&P,0);
                while(*P==' '||*P=='\t'||*P=='\r')P++;
                bool Ok=Value&&Name.Size&&(*P==0||*P=='\n'||*P=='#')&&!xrtValueObjectHas(Current,Name)&&xrtValueObjectSet(Current,Name,Value);
                xrtValueRelease(Value);
                if(!Ok)xrtValueObjectSetNew(Server,XRT_STR_LITERAL("_mdo_import_error"),xrtValueBool(true));
            }
        }
        while(*P&&*P!='\n')++P;
    }
    return Servers;
}

static char* McpImportRoot(bool Personal,cstr Workspace)
{
    if(!Personal)return Workspace?xrtStrDup(Workspace):NULL;
    char* Path=xrtEnvGet("USERPROFILE");if(!Path||!Path[0]){xrtFree(Path);Path=xrtEnvGet("HOME");}
    return Path;
}

static xvalue* McpImportRead(size_t Index,bool Personal,cstr Workspace,char Revision[65])
{
    Revision[0]=0;
    const McpImportSource* Source=&McpImportSources[Index];
    cstr Path=Personal?Source->User:Source->Project;
    char* Base=Path?McpImportRoot(Personal,Workspace):NULL;
    xroot Root=Base?xrtRootOpen(Base):NULL;bool Missing=false;
    char* Text=Root?SkillRead(Root,Path,&Missing):NULL;
    if(Root)xrtRootClose(Root);
    xrtFree(Base);
    if(!Text){if(Root&&!Missing)snprintf(Revision,65,"unavailable");xrtClearError();return NULL;}
    if(!MdoExtensionHash(Text,Revision)){xrtFree(Text);return NULL;}
    xvalue* Doc=Source->Toml?McpTomlServers(Text):xrtJsonParse(xrtStrView(Text));
    xrtSecureZero(Text,strlen(Text));xrtFree(Text);
    if(!Doc){xrtClearError();return NULL;}
    const xvalue* Servers=Doc;
    if(!Source->Toml) {
        if(!strcmp(Source->Provider,"zcode"))Servers=xrtValueObjectGet(xrtValueObjectGet(Doc,XRT_STR_LITERAL("mcp")),XRT_STR_LITERAL("servers"));
        else if(!strcmp(Source->Provider,"opencode"))Servers=xrtValueObjectGet(Doc,XRT_STR_LITERAL("mcp"));
        else Servers=xrtValueObjectGet(Doc,XRT_STR_LITERAL("mcpServers"));
    }
    if(!Servers)Revision[0]=0;
    xvalue* Result=xrtValueType(Servers)==XVALUE_OBJECT&&xrtValueCount(Servers)<=128?xrtValueDeepClone(Servers):NULL;
    xrtValueRelease(Doc);return Result;
}

static bool McpImportCredentials(xvalue* Doc,xvalue* Inputs,const xvalue* Map,cstr Key,bool EnvironmentNames)
{
    if(!Map)return true;
    if(xrtValueType(Map)!=XVALUE_OBJECT)return false;
    xvalue* Transport=(xvalue*)xrtValueObjectGet(Doc,XRT_STR_LITERAL("transport"));
    xvalue* Array=(xvalue*)xrtValueObjectGet(Transport,xrtStrView(Key));
    if(!Array){Array=xrtValueArray();if(!Array||!xrtValueObjectSetNew(Transport,xrtStrView(Key),Array))return false;}
    xvalueiter Iterator={0};xvaluekey Name;xvalue* Value;bool Ok=xrtValueIterBegin(Map,&Iterator);
    while(Ok) {
        xvalueiterresult Next=xrtValueIterAdvance(&Iterator,&Name,&Value);
        if(Next==XVALUE_ITER_END)break;
        if(Next==XVALUE_ITER_ERROR){Ok=false;break;}
        xstrview Text={0};xvalue* Item=xrtValueObject();char Ref[320];
        Ok=Item&&xrtValueGetString(Value,&Text)&&Text.Size&&Text.Size<=4096&&!memchr(Text.Data,0,Text.Size)&&
            MdoApiValueSetStringView(Item,"name",Name.String);
        if(Ok&&EnvironmentNames) {
            if(Text.Size>256)Ok=false;
            else snprintf(Ref,sizeof(Ref),"env:%.*s",(int)Text.Size,Text.Data);
        } else if(Ok) {
            if(xrtValueCount(Inputs)>=32)Ok=false;
            else {snprintf(Ref,sizeof(Ref),"input:%u",(unsigned)xrtValueCount(Inputs));Ok=xrtValueArrayAppend(Inputs,Value);}
        }
        Ok=Ok&&MdoApiValueSetString(Item,"secret_ref",Ref)&&xrtValueArrayAppend(Array,Item);xrtValueRelease(Item);
    }
    xrtValueIterEnd(&Iterator);return Ok;
}

static xvalue* McpImportDocument(const xvalue* Server,cstr Id,xvalue* Inputs,char Error[256])
{
    const char* Template="{\"schema_version\":1,\"enabled\":true,\"description\":\"Imported MCP server\",\"transport\":{\"type\":\"stdio\",\"inherit_environment\":true,\"environment\":[],\"arguments\":[],\"working_directory\":null},\"protocol_version\":\"2025-11-25\",\"startup_timeout_ms\":10000,\"request_timeout_ms\":60000,\"limits\":{\"message_bytes\":1048576,\"tools\":256},\"tools\":{\"allow\":[],\"deny\":[]},\"security\":{\"default_effects\":[\"external-service\"],\"permission_profile\":\"balanced\",\"trust_read_only_annotations\":false},\"auto_reconnect\":true}";
    xvalue* Doc=xrtJsonParse(xrtStrView(Template));
    xvalue* Transport=Doc?(xvalue*)xrtValueObjectGet(Doc,XRT_STR_LITERAL("transport")):NULL;
    xstrview Url={0},Type={0},Command={0},Cwd={0};bool Enabled=true;
    bool Ok=Doc&&xrtValueType(Server)==XVALUE_OBJECT&&!xrtValueObjectHas(Server,XRT_STR_LITERAL("_mdo_import_error"))&&
        MdoApiValueSetString(Doc,"id",Id)&&MdoApiValueSetString(Doc,"name",Id);
    (void)ExtensionString(Server,"type",&Type);
    bool Disabled=false;(void)xrtValueGetBool(xrtValueObjectGet(Server,XRT_STR_LITERAL("disabled")),&Disabled);Enabled=!Disabled;
    (void)xrtValueGetBool(xrtValueObjectGet(Server,XRT_STR_LITERAL("enabled")),&Enabled);
    Ok=Ok&&MdoApiValueSetBool(Doc,"enabled",Enabled);
    if(ExtensionString(Server,"url",&Url)) {
        const char* Stdio[]={"inherit_environment","environment","arguments","working_directory"};
        for(size_t i=0;i<4;i++)(void)xrtValueObjectRemove(Transport,xrtStrView(Stdio[i]));
        if(Type.Size==3&&!memcmp(Type.Data,"sse",3)){snprintf(Error,256,"Legacy SSE requires a Streamable HTTP endpoint");Ok=false;}
        Ok=Ok&&MdoApiValueSetString(Transport,"type","streamable-http")&&MdoApiValueSetStringView(Transport,"endpoint",Url)&&
            McpImportCredentials(Doc,Inputs,xrtValueObjectGet(Server,XRT_STR_LITERAL("headers")),"headers",false)&&
            McpImportCredentials(Doc,Inputs,xrtValueObjectGet(Server,XRT_STR_LITERAL("http_headers")),"headers",false)&&
            McpImportCredentials(Doc,Inputs,xrtValueObjectGet(Server,XRT_STR_LITERAL("env_http_headers")),"headers",true);
        xstrview TokenEnv={0};
        if(Ok&&ExtensionString(Server,"bearer_token_env_var",&TokenEnv)) {
            /* A bearer token environment variable needs the prefix at runtime;
             * avoid treating a naked token as an Authorization header. */
            snprintf(Error,256,"Bearer token environment reference requires authorization setup");Ok=false;
        }
    } else {
        const xvalue* Program=xrtValueObjectGet(Server,XRT_STR_LITERAL("command"));
        const xvalue* Arguments=xrtValueObjectGet(Server,XRT_STR_LITERAL("args"));
        xvalue* OwnedArgs=NULL;
        if(xrtValueType(Program)==XVALUE_ARRAY&&xrtValueCount(Program)) {
            (void)xrtValueGetString(xrtValueArrayGet(Program,0),&Command);OwnedArgs=xrtValueArray();
            for(size_t i=1;OwnedArgs&&i<xrtValueCount(Program);i++)if(!xrtValueArrayAppend(OwnedArgs,xrtValueArrayGet(Program,i))){xrtValueRelease(OwnedArgs);OwnedArgs=NULL;break;}
            Arguments=OwnedArgs;
        } else (void)xrtValueGetString(Program,&Command);
        Ok=Ok&&Command.Size&&!memchr(Command.Data,0,Command.Size)&&MdoApiValueSetStringView(Transport,"program",Command);
        if(Ok&&Arguments)Ok=xrtValueType(Arguments)==XVALUE_ARRAY&&xrtValueObjectSet(Transport,XRT_STR_LITERAL("arguments"),(xvalue*)Arguments);
        xrtValueRelease(OwnedArgs);
        if(ExtensionString(Server,"cwd",&Cwd))Ok=Ok&&MdoApiValueSetStringView(Transport,"working_directory",Cwd);
        const xvalue* Env=xrtValueObjectGet(Server,XRT_STR_LITERAL("env"));
        if(!Env)Env=xrtValueObjectGet(Server,XRT_STR_LITERAL("environment"));
        Ok=Ok&&McpImportCredentials(Doc,Inputs,Env,"environment",false);
    }
    const xvalue* OAuth=xrtValueObjectGet(Server,XRT_STR_LITERAL("oauth"));bool Auth=false;
    if(OAuth&&(xrtValueType(OAuth)==XVALUE_OBJECT||(xrtValueGetBool(OAuth,&Auth)&&Auth))) {
        snprintf(Error,256,"OAuth server needs authorization configuration");Ok=false;
    }
    if(xrtValueObjectHas(Server,XRT_STR_LITERAL("env_vars"))) {
        snprintf(Error,256,"Selective environment forwarding needs manual configuration");Ok=false;
    }
    const char* TimeKeys[]={"startup_timeout_sec","tool_timeout_sec","timeout"};
    const char* NativeTime[]={"startup_timeout_ms","request_timeout_ms","request_timeout_ms"};
    for(size_t i=0;Ok&&i<3;i++) {
        const xvalue* Value=xrtValueObjectGet(Server,xrtStrView(TimeKeys[i]));
        if(Value){uint64 Timeout=0;Ok=McpImportUInt(Value,&Timeout)&&Timeout>0&&Timeout<=(i==2?300000u:300u)&&
            MdoApiValueSetUInt(Doc,NativeTime[i],Timeout*(i==2?1u:1000u));}
    }
    const char* Lists[]={"enabled_tools","disabled_tools"};const char* Native[]={"allow","deny"};
    xvalue* Tools=(xvalue*)xrtValueObjectGet(Doc,XRT_STR_LITERAL("tools"));
    for(size_t i=0;Ok&&i<2;i++) {
        const xvalue* List=xrtValueObjectGet(Server,xrtStrView(Lists[i]));
        if(List)Ok=xrtValueType(List)==XVALUE_ARRAY&&xrtValueObjectSet(Tools,xrtStrView(Native[i]),(xvalue*)List);
    }
    if(!Ok){if(!Error[0])snprintf(Error,256,"Unsupported or invalid MCP configuration; review the source manually");xrtValueRelease(Doc);return NULL;}
    return Doc;
}

bool MdoApiMcpSourcesRoute(MdoApiContext* C)
{
    char* Workspace=NULL;if(!MdoApiSkillScopeMode(C,&Workspace,false))return true;
    xvalue* Data=xrtValueObject(),*Items=xrtValueArray(),*Issues=xrtValueArray();bool Ok=Data&&Items&&Issues;
    for(size_t Origin=0;Ok&&Origin<2;Origin++)for(size_t Index=0;Ok&&Index<sizeof(McpImportSources)/sizeof(*McpImportSources);Index++) {
        if(Origin&&!Workspace)continue;
        char Revision[65];xvalue* Servers=McpImportRead(Index,Origin==0,Workspace,Revision);
        if(!Servers){if(Revision[0]) {
            xvalue* Issue=xrtValueObject();Ok=Issue&&MdoApiValueSetString(Issue,"provider",McpImportSources[Index].Provider)&&
                MdoApiValueSetString(Issue,"origin",Origin?"project":"user")&&MdoApiValueSetUInt(Issue,"source_index",Index)&&
                MdoApiValueSetString(Issue,"message","Configuration is unreadable, unsupported or exceeds limits")&&xrtValueArrayAppend(Issues,Issue);
            xrtValueRelease(Issue);
        }continue;}
        xvalueiter Iterator={0};xvaluekey Key;xvalue* Server;Ok=xrtValueIterBegin(Servers,&Iterator);
        while(Ok) {
            xvalueiterresult Next=xrtValueIterAdvance(&Iterator,&Key,&Server);
            if(Next==XVALUE_ITER_END)break;
            if(Next==XVALUE_ITER_ERROR){Ok=false;break;}
            char* Id=xrtStrDupN(Key.String.Data,Key.String.Size);char Error[256]={0};
            xvalue* Inputs=xrtValueArray();xvalue* Doc=Id&&Inputs?McpImportDocument(Server,Id,Inputs,Error):NULL;
            xvalue* Item=xrtValueObject();
            Ok=Item&&MdoApiValueSetStringView(Item,"id",Key.String)&&MdoApiValueSetUInt(Item,"source_index",Index)&&
                MdoApiValueSetString(Item,"provider",McpImportSources[Index].Provider)&&MdoApiValueSetString(Item,"origin",Origin?"project":"user")&&
                MdoApiValueSetString(Item,"revision",Revision)&&MdoApiValueSetBool(Item,"compatible",Doc!=NULL)&&
                MdoApiValueSetString(Item,"error",Error)&&MdoApiValueSetUInt(Item,"credential_count",xrtValueCount(Inputs))&&xrtValueArrayAppend(Items,Item);
            xrtValueRelease(Item);xrtValueRelease(Doc);xrtValueRelease(Inputs);xrtFree(Id);
        }
        xrtValueIterEnd(&Iterator);xrtValueRelease(Servers);
    }
    xrtFree(Workspace);Ok=Ok&&MdoApiValueSetTake(Data,"items",&Items)&&MdoApiValueSetTake(Data,"issues",&Issues);xrtValueRelease(Items);xrtValueRelease(Issues);
    if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,503,"mcp_sources_unavailable","Cannot enumerate MCP sources",NULL);}
    return MdoApiReplySuccessTake(C,200,Data,NULL);
}

bool MdoApiMcpMigrateRoute(MdoApiContext* C)
{
    char* Workspace=NULL;if(!MdoApiSkillScopeMode(C,&Workspace,true))return true;
    MdoApiJsonBody Body={0};MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);
    if(Status!=MDO_API_BODY_OK){xrtFree(Workspace);return MdoApiReplyBodyError(C,Status);}
    xstrview Origin={0},Id={0},Scope={0},Target={0},Expected={0};uint64 Index=99;char Revision[65],Error[256]={0};
    bool Ok=ExtensionString(Body.Value,"origin",&Origin)&&ExtensionString(Body.Value,"id",&Id)&&Id.Size<=192&&
        ExtensionString(Body.Value,"scope",&Scope)&&ExtensionString(Body.Value,"revision",&Expected)&&Expected.Size==64&&
        McpImportUInt(xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("source_index")),&Index)&&Index<sizeof(McpImportSources)/sizeof(*McpImportSources)&&
        (!strcmp(Origin.Data,"user")||!strcmp(Origin.Data,"project"))&&(!strcmp(Scope.Data,"user")||!strcmp(Scope.Data,"project"));
    if(!ExtensionString(Body.Value,"target_id",&Target))Target=Id;
    Ok=Ok&&Target.Size<=64&&MdoExtensionIdValid(Target.Data)&&(!strcmp(Scope.Data,"user")||Workspace);
    xvalue* Servers=Ok?McpImportRead((size_t)Index,!strcmp(Origin.Data,"user"),Workspace,Revision):NULL;
    const xvalue* Server=Servers?xrtValueObjectGet(Servers,Id):NULL;
    bool Changed=Servers&&memcmp(Revision,Expected.Data,64);
    xvalue* Inputs=xrtValueArray();xvalue* Doc=Server&&!Changed&&Inputs?McpImportDocument(Server,Target.Data,Inputs,Error):NULL;
    char* Text=Doc?xrtJsonStringify(Doc,true,NULL):NULL;
    char References[32][MDO_SECRET_VAULT_REFERENCE_CAPACITY];size_t RefCount=0;
    bool Saved=false;uint16 Code=400;
    MdoProjectLease* Lease=NULL;
    if(Ok&&Workspace&&(!strcmp(Scope.Data,"project")||!strcmp(Origin.Data,"project"))) {
        char Project[65];size_t Size=0;xwork_error LeaseError;
        (void)xrtPercentDecode(xrtStrViewN(C->Target.Query.Data+8,C->Target.Query.Size-8),Project,64,&Size);Project[Size]=0;
        Lease=MdoProjectLeaseAcquire(Project,MDO_PROJECT_LEASE_SHARED,&LeaseError);
        if(!Lease){Ok=false;Code=409;snprintf(Error,sizeof(Error),"Project is busy");}
    }
    xrtMutexLock(g_MdoExtensionLock);
    Ok=Ok&&Text&&ExtensionMcpCredentials(&Text,Inputs,References,&RefCount)&&MdoMcpValidateText(Target.Data,Text,Error,sizeof(Error));
    if(Ok&&!strcmp(Scope.Data,"user")) {
        char Path[100];snprintf(Path,sizeof(Path),"mcp/%s.json",Target.Data);bool Exists=false;xfileinfo Info;
        Ok=MdoHomeExternalStat(Path,&Exists,&Info)&&!Exists;
        if(!Ok)Code=412;
        if(Ok){Saved=MdoHomeAtomicWrite(Path,Text,strlen(Text),false);Ok=Saved&&MdoMcpManagerReload();}
        if(Saved&&!Ok){(void)MdoHomeRemove(Path,false);Saved=false;(void)MdoMcpManagerReload();}
    } else if(Ok) {
        char Path[100];snprintf(Path,sizeof(Path),".mdo/mcp/%s.json",Target.Data);xroot Root=xrtRootOpen(Workspace);
        xfile Lock=Root?McpProjectWriterLock(Root):NULL;bool Missing=false;char* Old=Root?SkillRead(Root,Path,&Missing):NULL;
        Ok=Root&&Lock&&!Old&&Missing;if(!Ok)Code=Root&&Lock?412:409;
        if(Ok)Ok=Saved=SkillWrite(Root,Path,Text,strlen(Text));
        xrtFree(Old);if(Lock)xrtClose(Lock);if(Root)xrtRootClose(Root);
    }
    if(!Saved)for(size_t i=0;i<RefCount;i++)MdoSecretDiscard(References[i]);
    xrtMutexUnlock(g_MdoExtensionLock);
    MdoProjectLeaseRelease(Lease);
    xrtFree(Text);xrtValueRelease(Doc);xrtValueRelease(Inputs);xrtValueRelease(Servers);xrtFree(Workspace);MdoApiJsonBodyUnit(&Body);
    if(Changed)return MdoApiReplyError(C,412,"mcp_source_changed","Source configuration changed; scan it again",NULL);
    return Ok?MdoApiReplySuccessTake(C,200,xrtValueObject(),NULL):MdoApiReplyError(C,Code,"mcp_migration_failed",Error[0]?Error:"Cannot import MCP; check the source and choose a new target name",NULL);
}
