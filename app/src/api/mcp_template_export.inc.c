/* Deliberately conservative export: only whitelisted runtime fields survive;
 * every user supplied endpoint, argument, path and OAuth value becomes a blank
 * declared input. Authors can explicitly replace inputs with public constants
 * in the template preview. Credential values are never resolved here. */
static bool McpTemplateDeclare(xvalue* Inputs,cstr Id,cstr Label,bool Secret)
{
    xvalue* I=xrtValueObject();bool Ok=I&&MdoApiValueSetString(I,"id",Id)&&MdoApiValueSetString(I,"label",Label)&&MdoApiValueSetString(I,"type","text")&&
        MdoApiValueSetString(I,"description",Secret?"在安装设备填写，存入系统凭据库":"由安装用户填写；发布前可改为公开常量")&&MdoApiValueSetBool(I,"required",true)&&MdoApiValueSetBool(I,"secret",Secret)&&xrtValueArrayAppend(Inputs,I);xrtValueRelease(I);return Ok;
}
static bool McpTemplateReplace(xvalue* O,cstr Key,xvalue* Inputs,cstr Id,cstr Label,bool Secret,bool Ref)
{
    if(!McpTemplateDeclare(Inputs,Id,Label,Secret))return false;
    if(Ref){char R[80];snprintf(R,sizeof(R),"input:%s",Id);return MdoApiValueSetString(O,Key,R);}
    xvalue* V=xrtValueObject();bool Ok=V&&MdoApiValueSetString(V,"input",Id)&&xrtValueObjectSet(O,xrtStrView(Key),V);xrtValueRelease(V);return Ok;
}
static char* McpTemplateExport(cstr Content,cstr Id)
{
    xvalue* D=xrtJsonParse(xrtStrView(Content)),*T=xrtValueObject(),*Inputs=xrtValueArray(),*Variants=xrtValueArray(),*V=xrtValueObject(),*C=xrtValueObject();
    const xvalue* Original=xrtValueObjectGet(D,XRT_STR_LITERAL("transport"));xvalue* Tr=xrtValueObject();cstr Type=MdoPackageText(Original,"type",32);
    bool Ok=D&&T&&Inputs&&Variants&&V&&C&&Tr&&Type&&!xrtValueObjectHas(D,XRT_STR_LITERAL("mdo_template"));
    const char* Keys[]={"schema_version","name","description","protocol_version","startup_timeout_ms","request_timeout_ms","limits","tools","security","auto_reconnect"};
    for(size_t i=0;Ok&&i<10;i++)Ok=xrtValueObjectSet(C,xrtStrView(Keys[i]),xrtValueObjectGet(D,xrtStrView(Keys[i])));
    Ok=Ok&&MdoApiValueSetString(Tr,"type",Type);
    if(Ok&&!strcmp(Type,"streamable-http"))Ok=McpTemplateReplace(Tr,"endpoint",Inputs,"endpoint","服务地址（HTTPS）",false,false);
    else if(Ok&&!strcmp(Type,"stdio")){
        Ok=McpTemplateReplace(Tr,"program",Inputs,"program","可执行文件路径或已安装的命令",false,false)&&MdoApiValueSetBool(Tr,"inherit_environment",false);
        const xvalue* Old=xrtValueObjectGet(Original,XRT_STR_LITERAL("arguments"));xvalue* A=xrtValueArray();
        for(size_t i=0;Ok&&i<xrtValueCount(Old);i++){char Key[32],Label[64];snprintf(Key,sizeof(Key),"argument-%zu",i);snprintf(Label,sizeof(Label),"启动参数 %zu",i+1);xvalue* E=xrtValueObject();
            Ok=McpTemplateDeclare(Inputs,Key,Label,false)&&MdoApiValueSetString(E,"input",Key)&&xrtValueArrayAppend(A,E);xrtValueRelease(E);}
        Ok=Ok&&xrtValueObjectSet(Tr,XRT_STR_LITERAL("arguments"),A);xrtValueRelease(A);
        if(xrtValueType(xrtValueObjectGet(Original,XRT_STR_LITERAL("working_directory")))==XVALUE_STRING)Ok=Ok&&McpTemplateReplace(Tr,"working_directory",Inputs,"directory","工作目录",false,false);
    }else Ok=false;
    for(size_t k=0;Ok&&k<2;k++){
        cstr Key=k?"headers":"environment";const xvalue* Old=xrtValueObjectGet(Original,xrtStrView(Key));xvalue* A=xrtValueArray();
        for(size_t i=0;Ok&&i<xrtValueCount(Old);i++){const xvalue* E=xrtValueArrayGet(Old,i);bool Remove=false;(void)xrtValueGetBool(xrtValueObjectGet(E,XRT_STR_LITERAL("remove")),&Remove);if(Remove)continue;
            char Input[32];snprintf(Input,sizeof(Input),"%s-%zu",k?"header":"env",i);xvalue* N=xrtValueObject();cstr Name=MdoPackageText(E,"name",256);
            Ok=Name&&MdoApiValueSetString(N,"name",Name)&&McpTemplateReplace(N,"secret_ref",Inputs,Input,Name,true,true)&&xrtValueArrayAppend(A,N);xrtValueRelease(N);}
        Ok=Ok&&(!xrtValueCount(A)||xrtValueObjectSet(Tr,xrtStrView(Key),A));xrtValueRelease(A);
    }
    const xvalue* OAuth=xrtValueObjectGet(D,XRT_STR_LITERAL("oauth"));
    if(Ok&&OAuth){xvalue* O=xrtValueObject();Ok=MdoApiValueSetBool(O,"enabled",true);
        if(xrtValueType(OAuth)==XVALUE_OBJECT){const char* Fields[]={"client_id","client_metadata_url","client_secret_ref","scope","resource_metadata_url","authorization_server","redirect_uri","auth_method"};
            for(size_t i=0;Ok&&i<8;i++)if(xrtValueObjectHas(OAuth,xrtStrView(Fields[i]))){char Key[64];snprintf(Key,sizeof(Key),"oauth-%zu",i);
                Ok=McpTemplateReplace(O,Fields[i],Inputs,Key,Fields[i],i==2,i==2);}}
        Ok=Ok&&xrtValueObjectSet(C,XRT_STR_LITERAL("oauth"),O);xrtValueRelease(O);}
    xvalue* Ps=xrtValueArray();Ok=Ok&&xrtValueArrayAppendNew(Ps,xrtValueString(xrtStrView(MdoToolPlatform())))&&xrtValueObjectSet(C,XRT_STR_LITERAL("transport"),Tr)&&
        MdoApiValueSetString(V,"id","default")&&MdoApiValueSetString(V,"label","默认连接")&&xrtValueObjectSet(V,XRT_STR_LITERAL("platforms"),Ps)&&xrtValueObjectSet(V,XRT_STR_LITERAL("config"),C)&&xrtValueArrayAppend(Variants,V)&&
        MdoApiValueSetString(T,"format",MDO_MCP_TEMPLATE_FORMAT)&&MdoApiValueSetString(T,"id",Id)&&xrtValueObjectSet(T,XRT_STR_LITERAL("inputs"),Inputs)&&xrtValueObjectSet(T,XRT_STR_LITERAL("variants"),Variants);
    char Error[256];char* Text=Ok&&MdoMcpTemplateValidate(T,Id,Error,sizeof(Error))?xrtJsonStringify(T,true,NULL):NULL;
    xrtValueRelease(Ps);xrtValueRelease(D);xrtValueRelease(T);xrtValueRelease(Inputs);xrtValueRelease(Variants);xrtValueRelease(V);xrtValueRelease(C);xrtValueRelease(Tr);return Text;
}
