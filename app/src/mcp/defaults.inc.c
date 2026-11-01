/* Installed recipes own their default service. Runtime configuration is
 * derived from the approved directory; only user overrides are persisted. */
static bool McpInputUsed(const xvalue* Value,cstr Id,unsigned Depth)
{
    if(!Value||Depth>20)return false;
    cstr Input=MdoExtensionText(Value,"input",64);if(Input&&!strcmp(Input,Id))return true;
    xstrview Text={0};if(xrtValueGetString(Value,&Text))return Text.Size>6&&!strncmp(Text.Data,"input:",6)&&!strcmp(Text.Data+6,Id);
    for(size_t I=0;I<xrtValueCount(Value);I++){
        const xvalue* Child=xrtValueType(Value)==XVALUE_ARRAY?xrtValueArrayGet(Value,I):xrtValueObjectAt(Value,I,NULL);
        if(McpInputUsed(Child,Id,Depth+1))return true;
    }
    return false;
}
static bool McpDefaultId(cstr Ref,char Id[97])
{
    uint8 Digest[32];if(!Ref||!xrtSha256(Ref,strlen(Ref),Digest))return false;
    memcpy(Id,"auto-",5);for(size_t I=0;I<16;I++)snprintf(Id+5+I*2,3,"%02x",Digest[I]);return true;
}
static bool McpDefaultTrusted(const xvalue* Meta,char Digest[65],char Error[512])
{
    cstr Id=MdoExtensionText(Meta,"id",MDO_EXTENSION_REF_BYTES),Directory=MdoExtensionText(Meta,"directory_id",96);
    cstr Approved=MdoExtensionText(xrtValueObjectGet(Meta,XRT_STR_LITERAL("installation")),"directory_digest",64);char Actual[65];
    return Approved&&Directory&&MdoExtensionsVerify("mcp",Directory,NULL,Error)&&MdoExtensionsFingerprint("mcp",Directory,Actual,NULL,Error)&&
        !strcmp(Approved,Actual)&&MdoExtensionsApprovalDigest(Id,Digest,NULL,Error);
}
static xvalue* McpDefaultDocument(const xvalue* Meta,xvalue** Missing,const char** State,char Error[512])
{
    *Missing=xrtValueArray();*State="needs_configuration";char Id[97],Digest[65]={0};
    cstr Ref=MdoExtensionText(Meta,"id",MDO_EXTENSION_REF_BYTES),Directory=MdoExtensionText(Meta,"directory_id",96),Alias=MdoExtensionText(Meta,"alias",64);
    char* Entry=Directory?MdoExtensionsEntryPath("mcp",Directory,Error):NULL,*Text=Entry?MdoExtensionRead(Entry,true,MDO_MCP_FILE_LIMIT,NULL):NULL;
    xvalue* Template=Text?xrtJsonParse(xrtStrView(Text)):NULL,*Doc=NULL,*Values=xrtValueObject();xrtFree(Entry);xrtFree(Text);
    bool Ok=*Missing&&Values&&Template&&MdoMcpTemplateValidate(Template,Alias,Error,512)&&McpDefaultId(Ref,Id);
    const xvalue* Variant=Ok?MdoMcpTemplateDefault(Template,MdoExtensionRuntimePlatform()):NULL;
    if(Ok&&!Variant){*State="unsupported";Ok=false;}
    const xvalue* Config=xrtValueObjectGet(Variant,XRT_STR_LITERAL("config")),*Inputs=xrtValueObjectGet(Template,XRT_STR_LITERAL("inputs"));
    for(size_t I=0;Ok&&I<xrtValueCount(Inputs);I++){
        const xvalue* Input=xrtValueArrayGet(Inputs,I);cstr Key=MdoExtensionText(Input,"id",64),Default=MdoExtensionText(Input,"default",4096);
        if(!McpInputUsed(Config,Key,0))continue;
        bool Required=MdoMcpTemplateFlag(Input,"required",true),Secret=MdoMcpTemplateFlag(Input,"secret",false);
        if(Required&&(!Default||!Default[0]||Secret))Ok=xrtValueArrayAppendNew(*Missing,xrtValueString(xrtStrView(Key)));
        if(Ok)Ok=McpDirectoryPut(Values,Key,Secret?"":Default?Default:"");
    }
    bool Enabled=MdoMcpTemplateFlag(Meta,"enabled",false),Trusted=Ok&&McpDefaultTrusted(Meta,Digest,Error);
    if(Ok){Doc=xrtValueObject();Ok=Doc&&McpDirectoryPut(Doc,"id",Id)&&McpDirectoryPut(Doc,"extension_id",Ref)&&McpDirectoryPut(Doc,"directory_id",Directory)&&
        McpDirectoryPut(Doc,"variant",MdoExtensionText(Variant,"id",64))&&McpDirectoryPut(Doc,"approval_digest",Digest)&&
        McpDirectoryPut(Doc,"name",MdoExtensionText(Meta,"name",256))&&McpDirectoryPut(Doc,"project_id","")&&
        xrtValueObjectSet(Doc,XRT_STR_LITERAL("values"),Values)&&xrtValueObjectSetNew(Doc,XRT_STR_LITERAL("managed_default"),xrtValueBool(true))&&
        xrtValueObjectSetNew(Doc,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled));
        *State=!Enabled?"disabled":!Trusted?"needs_approval":xrtValueCount(*Missing)?"needs_configuration":"starting";
        char Identity[65];Ok=Ok&&McpCredentialIdentity(Template,MdoExtensionText(Variant,"id",64),Values,Identity)&&McpDirectoryPut(Doc,"credential_identity",Identity);
    }
    xrtValueRelease(Values);xrtValueRelease(Template);if(!Ok){xrtValueRelease(Doc);Doc=NULL;}return Doc;
}
static bool McpDefaultsAppend(const xvalue* Meta,MdoMcpSource** Sources,size_t* Count,size_t* Capacity,xvalue* Defaults)
{
    cstr Ref=MdoExtensionText(Meta,"id",MDO_EXTENSION_REF_BYTES),Directory=MdoExtensionText(Meta,"directory_id",96);
    if(!Ref||!Directory||!MdoMcpTemplateFlag(Meta,"valid",false))return true;
    // An explicit private connection, including one deliberately disabled by
    // its owner, always suppresses automatic creation for this installation.
    for(size_t I=0;I<*Count;I++){
        char* Text=(*Sources)[I].Connection?NULL:MdoExtensionRead((*Sources)[I].RelativePath,true,MDO_MCP_FILE_LIMIT,NULL);xvalue* Doc=(*Sources)[I].Connection?xrtValueClone((*Sources)[I].Connection):Text?xrtJsonParse(xrtStrView(Text)):NULL;
        cstr Found=MdoExtensionText(Doc,"extension_id",MDO_EXTENSION_REF_BYTES);bool Same=Found&&!strcmp(Found,Ref);
        if(Text){xrtSecureZero(Text,strlen(Text));xrtFree(Text);}xrtValueRelease(Doc);if(Same)return true;
    }
    char Error[512]={0};xvalue* Missing=NULL;const char* State=NULL;
    xvalue* Doc=McpDefaultDocument(Meta,&Missing,&State,Error),*Row=xrtValueObject();
    bool Ok=Row&&McpDirectoryPut(Row,"extension_id",Ref)&&McpDirectoryPut(Row,"directory_id",Directory)&&McpDirectoryPut(Row,"state",Doc?State:State&&!strcmp(State,"unsupported")?State:"failed")&&
        McpDirectoryPut(Row,"error",Error)&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("derived"),xrtValueBool(true))&&xrtValueObjectSet(Row,XRT_STR_LITERAL("missing_inputs"),Missing);
    if(Ok&&Doc)Ok=xrtValueObjectSet(Row,XRT_STR_LITERAL("connection"),Doc);
    if(Ok)Ok=xrtValueArrayAppend(Defaults,Row);
    if(Ok&&Doc&&!strcmp(State,"starting")){
        Ok=MdoMcpGrow((void**)Sources,Capacity,*Count+1,sizeof(**Sources),MDO_MCP_SERVER_LIMIT);
        if(Ok){MdoMcpSource* S=&(*Sources)[*Count];memset(S,0,sizeof(*S));S->External=true;S->Id=xrtStrDup(MdoExtensionText(Doc,"id",96));
            S->RelativePath=xrtFormat(MDO_MCP_CONNECTIONS "/%s.json",S->Id);S->VirtualPath=S->RelativePath?xrtFormat("/app/default-home/%s",S->RelativePath):NULL;
            S->Connection=xrtValueClone(Doc);Ok=S->Id&&S->RelativePath&&S->VirtualPath&&S->Connection;
            if(Ok)(*Count)++;else MdoMcpSourceUnit(S);
        }
    }
    xrtValueRelease(Missing);xrtValueRelease(Doc);xrtValueRelease(Row);xrtClearError();return Ok;
}
static xvalue* McpDefaultLookup(cstr Id)
{
    MdoMcpCatalog* Catalog=MdoMcpCatalogSnapshot();xvalue* Result=NULL;
    for(size_t I=0;Catalog&&I<xrtValueCount(Catalog->Defaults);I++){
        const xvalue* Row=xrtValueArrayGet(Catalog->Defaults,I);if(!MdoMcpTemplateFlag(Row,"derived",false))continue;
        const xvalue* Doc=xrtValueObjectGet(Row,XRT_STR_LITERAL("connection"));cstr Found=MdoExtensionText(Doc,"id",96);
        if(Found&&!strcmp(Found,Id)){Result=xrtValueClone(Doc);break;}
    }
    MdoMcpCatalogRelease(Catalog);return Result;
}
