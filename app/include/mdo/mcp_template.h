#ifndef MDO_MCP_TEMPLATE_H
#define MDO_MCP_TEMPLATE_H
/* Public, inert service recipes. Shared verbatim with the marketplace. No
 * runtime credentials, device paths, hooks or executable payloads belong here. */
#define MDO_MCP_TEMPLATE_FORMAT "mdo.mcp-template.v1"
static bool MdoMcpTemplateKeys(const xvalue* V,cstr Keys)
{
    if(xrtValueType(V)!=XVALUE_OBJECT)return false;
    xvalueiter It={0};xvaluekey K;xvalue* E;bool Ok=true;
    if(xrtValueIterBegin(V,&It)){while((E=xrtValueIterNext(&It,&K))){char Key[100];
        if(K.String.Size>96){Ok=false;break;}snprintf(Key,sizeof(Key),"|%.*s|",(int)K.String.Size,K.String.Data);
        if(!strstr(Keys,Key)){Ok=false;break;}}
        xrtValueIterEnd(&It);}return Ok;
}
static bool MdoMcpTemplateFlag(const xvalue* V,cstr Key,bool Fallback)
{bool B=Fallback;return xrtValueGetBool(xrtValueObjectGet(V,xrtStrView(Key)),&B)?B:Fallback;}
static const xvalue* MdoMcpTemplateInput(const xvalue* T,cstr Id)
{
    const xvalue* A=xrtValueObjectGet(T,XRT_STR_LITERAL("inputs"));
    for(size_t i=0;Id&&i<xrtValueCount(A);i++){const xvalue* E=xrtValueArrayGet(A,i);cstr N=MdoPackageText(E,"id",64);if(N&&!strcmp(N,Id))return E;}return NULL;
}
static bool MdoMcpTemplateString(cstr S)
{if(!S)return false;for(cstr P=S;*P;P++)if((unsigned char)*P<32)return false;return true;}
static bool MdoMcpTemplateUrl(cstr S)
{
    /* User-specific queries, fragments and URL userinfo are inputs, never
     * published defaults. HTTP is allowed only by the local runtime policy. */
    return S&&!strncmp(S,"https://",8)&&S[8]&&!strpbrk(S,"@?#\\ \t\r\n");
}
static bool MdoMcpTemplateLeaf(const xvalue* T,const xvalue* V,bool Secret,bool Url,bool Program)
{
    if(xrtValueType(V)==XVALUE_OBJECT){cstr Id=MdoPackageText(V,"input",64);const xvalue* I=MdoMcpTemplateInput(T,Id);
        return MdoMcpTemplateKeys(V,"|input|")&&I&&MdoMcpTemplateFlag(I,"secret",false)==Secret;}
    xstrview S;if(Secret||!xrtValueGetString(V,&S)||S.Size>4096||memchr(S.Data,0,S.Size)||!MdoMcpTemplateString(S.Data))return false;
    if(Url)return MdoMcpTemplateUrl(S.Data);
    if(Program)return S.Size&&!strpbrk(S.Data,"/\\:");
    return true;
}
static bool MdoMcpTemplateValidate(const xvalue* T,cstr Id,char* Error,size_t Capacity)
{
    cstr Why="Invalid MCP service template";int64 N=0;
    if(!MdoMcpTemplateKeys(T,"|format||id||operator||homepage||source_url||capabilities||inputs||variants|"))goto bad;
    cstr F=MdoPackageText(T,"format",32),Tid=MdoPackageText(T,"id",64);
    if(!F||strcmp(F,MDO_MCP_TEMPLATE_FORMAT)||!Tid||strcmp(Tid,Id))goto bad;
    for(size_t k=0;k<2;k++){cstr S=MdoPackageText(T,k?"source_url":"homepage",1000);if(xrtValueObjectHas(T,xrtStrView(k?"source_url":"homepage"))&&(!S||(*S&&!MdoMcpTemplateUrl(S))))goto bad;}
    if(xrtValueObjectHas(T,XRT_STR_LITERAL("operator"))&&!MdoPackageText(T,"operator",256))goto bad;
    const xvalue* Inputs=xrtValueObjectGet(T,XRT_STR_LITERAL("inputs"));
    Why="Invalid MCP input declaration, sensitive default, or duplicate input ID";
    if(xrtValueType(Inputs)!=XVALUE_ARRAY||xrtValueCount(Inputs)>32)goto bad;
    for(size_t i=0;i<xrtValueCount(Inputs);i++){
        const xvalue* I=xrtValueArrayGet(Inputs,i);cstr Name=MdoPackageText(I,"id",64),Type=MdoPackageText(I,"type",16),Label=MdoPackageText(I,"label",256);
        if(!MdoMcpTemplateKeys(I,"|id||type||label||description||required||secret||default||options||min||max|")||!MdoPackageId(Name)||!Type||!Label||!*Label||!MdoPackageText(I,"description",1000))goto bad;
        if(strcmp(Type,"text")&&strcmp(Type,"number")&&strcmp(Type,"select")&&strcmp(Type,"directory")&&strcmp(Type,"file")&&strcmp(Type,"boolean"))goto bad;
        bool Secret=MdoMcpTemplateFlag(I,"secret",false),B;
        if(!xrtValueGetBool(xrtValueObjectGet(I,XRT_STR_LITERAL("required")),&B)||!xrtValueGetBool(xrtValueObjectGet(I,XRT_STR_LITERAL("secret")),&B))goto bad;
        const xvalue* D=xrtValueObjectGet(I,XRT_STR_LITERAL("default"));
        if(D&&(Secret||!MdoPackageText(I,"default",4096)||!MdoMcpTemplateString(MdoPackageText(I,"default",4096))))goto bad;
        if(Secret&&strcmp(Type,"text"))goto bad;
        for(size_t j=0;j<i;j++)if(!strcmp(Name,MdoPackageText(xrtValueArrayGet(Inputs,j),"id",64)))goto bad;
        const xvalue* Options=xrtValueObjectGet(I,XRT_STR_LITERAL("options"));
        if(!strcmp(Type,"select")&&(xrtValueType(Options)!=XVALUE_ARRAY||!xrtValueCount(Options)||xrtValueCount(Options)>64))goto bad;
        for(size_t j=0;j<xrtValueCount(Options);j++){xstrview S;if(!xrtValueGetString(xrtValueArrayGet(Options,j),&S)||S.Size>256||memchr(S.Data,0,S.Size))goto bad;}
        if((xrtValueObjectHas(I,XRT_STR_LITERAL("min"))&&!MdoPackageNumber(I,"min",&N))||(xrtValueObjectHas(I,XRT_STR_LITERAL("max"))&&!MdoPackageNumber(I,"max",&N)))goto bad;
    }
    const xvalue* Variants=xrtValueObjectGet(T,XRT_STR_LITERAL("variants"));
    Why="MCP templates require 1-8 connection variants";
    if(xrtValueType(Variants)!=XVALUE_ARRAY||!xrtValueCount(Variants)||xrtValueCount(Variants)>8)goto bad;
    for(size_t i=0;i<xrtValueCount(Variants);i++){
        const xvalue* V=xrtValueArrayGet(Variants,i),*C=xrtValueObjectGet(V,XRT_STR_LITERAL("config")),*Tr=xrtValueObjectGet(C,XRT_STR_LITERAL("transport"));
        cstr Vid=MdoPackageText(V,"id",64),Label=MdoPackageText(V,"label",256),Type=MdoPackageText(Tr,"type",32);
        Why="Invalid MCP connection variant or runtime config";
        if(!MdoMcpTemplateKeys(V,"|id||label||platforms||config||requirements|")||!MdoPackageId(Vid)||!Label||!*Label||!Type)goto bad;
        for(size_t j=0;j<i;j++)if(!strcmp(Vid,MdoPackageText(xrtValueArrayGet(Variants,j),"id",64)))goto bad;
        const xvalue* Ps=xrtValueObjectGet(V,XRT_STR_LITERAL("platforms"));if(xrtValueType(Ps)!=XVALUE_ARRAY||!xrtValueCount(Ps)||xrtValueCount(Ps)>8)goto bad;
        for(size_t j=0;j<xrtValueCount(Ps);j++){xstrview S;if(!xrtValueGetString(xrtValueArrayGet(Ps,j),&S)||S.Size>40||memchr(S.Data,0,S.Size))goto bad;}
        if(xrtValueObjectHas(V,XRT_STR_LITERAL("requirements"))&&!MdoPackageText(V,"requirements",4000))goto bad;
        if(!MdoMcpTemplateKeys(C,"|schema_version||name||description||transport||protocol_version||startup_timeout_ms||request_timeout_ms||limits||tools||security||auto_reconnect||oauth|")||!MdoPackageNumber(C,"schema_version",&N)||N!=1||!MdoPackageText(C,"name",256)||!MdoPackageText(C,"description",2048))goto bad;
        cstr Protocol=MdoPackageText(C,"protocol_version",32);if(!Protocol||(strcmp(Protocol,"2025-11-25")&&strcmp(Protocol,"2026-07-28")))goto bad;
        if(!MdoPackageNumber(C,"startup_timeout_ms",&N)||N<100||N>300000||!MdoPackageNumber(C,"request_timeout_ms",&N)||N<100||N>300000)goto bad;
        if(!strcmp(Type,"streamable-http")){
            if(!MdoMcpTemplateKeys(Tr,"|type||endpoint||headers|")||!MdoMcpTemplateLeaf(T,xrtValueObjectGet(Tr,XRT_STR_LITERAL("endpoint")),false,true,false))goto bad;
        }else if(!strcmp(Type,"stdio")){
            if(!MdoMcpTemplateKeys(Tr,"|type||program||arguments||environment||inherit_environment||working_directory|")||!MdoMcpTemplateLeaf(T,xrtValueObjectGet(Tr,XRT_STR_LITERAL("program")),false,false,true))goto bad;
            const xvalue* A=xrtValueObjectGet(Tr,XRT_STR_LITERAL("arguments"));if(xrtValueType(A)!=XVALUE_ARRAY||xrtValueCount(A)>256)goto bad;
            for(size_t j=0;j<xrtValueCount(A);j++)if(!MdoMcpTemplateLeaf(T,xrtValueArrayGet(A,j),false,false,false))goto bad;
            const xvalue* Cwd=xrtValueObjectGet(Tr,XRT_STR_LITERAL("working_directory"));
            if(Cwd&&xrtValueType(Cwd)!=XVALUE_NULL&&(xrtValueType(Cwd)!=XVALUE_OBJECT||!MdoMcpTemplateLeaf(T,Cwd,false,false,false)))goto bad;
        }else goto bad;
        for(size_t k=0;k<2;k++){
            const xvalue* A=xrtValueObjectGet(Tr,xrtStrView(k?"headers":"environment"));
            if(A&&(xrtValueType(A)!=XVALUE_ARRAY||xrtValueCount(A)>32))goto bad;
            for(size_t j=0;j<xrtValueCount(A);j++){
                const xvalue* E=xrtValueArrayGet(A,j);cstr Name=MdoPackageText(E,"name",256),Ref=MdoPackageText(E,"secret_ref",80);
                if(!MdoMcpTemplateKeys(E,"|name||secret_ref|")||!Name||!*Name||!Ref||strncmp(Ref,"input:",6)||!MdoMcpTemplateFlag(MdoMcpTemplateInput(T,Ref+6),"secret",false))goto bad;
                /* Header/env values, including non-secret constants, are supplied
                 * privately by the installing user. */
            }
        }
        const xvalue* O=xrtValueObjectGet(C,XRT_STR_LITERAL("oauth"));
        if(O){
            if(!MdoMcpTemplateKeys(O,"|enabled||client_id||client_metadata_url||client_secret_ref||scope||resource_metadata_url||authorization_server||redirect_uri||auth_method|"))goto bad;
            const char* Keys[]={"client_id","client_metadata_url","scope","resource_metadata_url","authorization_server","redirect_uri","auth_method"};
            for(size_t j=0;j<7;j++)if(xrtValueObjectHas(O,xrtStrView(Keys[j]))&&!MdoMcpTemplateLeaf(T,xrtValueObjectGet(O,xrtStrView(Keys[j])),false,j==1||j==3||j==4||j==5,false))goto bad;
            cstr R=MdoPackageText(O,"client_secret_ref",80);if(xrtValueObjectHas(O,XRT_STR_LITERAL("client_secret_ref"))&&(!R||strncmp(R,"input:",6)||!MdoMcpTemplateFlag(MdoMcpTemplateInput(T,R+6),"secret",false)))goto bad;
        }
        const xvalue* Limits=xrtValueObjectGet(C,XRT_STR_LITERAL("limits")),*Tools=xrtValueObjectGet(C,XRT_STR_LITERAL("tools")),*Security=xrtValueObjectGet(C,XRT_STR_LITERAL("security"));bool B;
        if(!MdoMcpTemplateKeys(Limits,"|message_bytes||tools|")||!MdoPackageNumber(Limits,"message_bytes",&N)||N<1024||N>16777216||!MdoPackageNumber(Limits,"tools",&N)||N<1||N>4096||
           !MdoMcpTemplateKeys(Tools,"|allow||deny|")||!MdoMcpTemplateKeys(Security,"|default_effects||permission_profile||trust_read_only_annotations|")||!MdoPackageText(Security,"permission_profile",32)||
           !xrtValueGetBool(xrtValueObjectGet(Security,XRT_STR_LITERAL("trust_read_only_annotations")),&B)||!xrtValueGetBool(xrtValueObjectGet(C,XRT_STR_LITERAL("auto_reconnect")),&B))goto bad;
        for(size_t k=0;k<3;k++){const xvalue* A=xrtValueObjectGet(k<2?Tools:Security,xrtStrView(k==0?"allow":k==1?"deny":"default_effects"));if(xrtValueType(A)!=XVALUE_ARRAY||xrtValueCount(A)>256)goto bad;
            for(size_t j=0;j<xrtValueCount(A);j++){xstrview S;if(!xrtValueGetString(xrtValueArrayGet(A,j),&S)||S.Size>256||memchr(S.Data,0,S.Size))goto bad;}}
    }
    if(xrtValueObjectHas(T,XRT_STR_LITERAL("capabilities"))){const xvalue* A=xrtValueObjectGet(T,XRT_STR_LITERAL("capabilities"));if(xrtValueType(A)!=XVALUE_ARRAY||xrtValueCount(A)>128)goto bad;for(size_t i=0;i<xrtValueCount(A);i++){xstrview S;if(!xrtValueGetString(xrtValueArrayGet(A,i),&S)||S.Size>1000||memchr(S.Data,0,S.Size))goto bad;}}
    return true;
bad:if(Error&&Capacity)snprintf(Error,Capacity,"%s",Why);return false;
}
#endif
