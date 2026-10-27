#include "../../include/mdo/commands.h"
#include <ctype.h>
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_manifest.h"

static cstr CommandText(const xvalue* V,cstr Key)
{
    xstrview S={0};if(!xrtValueGetString(xrtValueObjectGet(V,xrtStrView(Key)),&S)||
        memchr(S.Data,0,S.Size))return NULL;
    return S.Data;
}
static bool CommandSet(xvalue* V,cstr K,cstr S)
{return xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S?S:"")));}
static bool CommandUInt(const xvalue* V,uint64* N) {
    int64 Signed=0;
    if(xrtValueType(V)==XVALUE_UINT)return xrtValueGetUInt(V,N);
    if(xrtValueType(V)==XVALUE_INT&&xrtValueGetInt(V,&Signed)&&Signed>=0){*N=(uint64)Signed;return true;}
    return false;
}

xvalue* MdoCommandMetadata(cstr Id,cstr Source,char Error[512])
{
    if(Error)Error[0]=0;
    if(!MdoExtensionAlias(Id)||!Source||strlen(Source)>MDO_COMMAND_TEXT_LIMIT||
        !xrtUtf8Valid(xrtStrView(Source),NULL))goto invalid;
    while(*Source==' '||*Source=='\r'||*Source=='\n'||*Source=='\t')Source++;
    cstr Prefix="/* mdo-command";
    if(strncmp(Source,Prefix,strlen(Prefix)))goto invalid;
    Source+=strlen(Prefix);cstr End=strstr(Source,"*/");if(!End||End-Source>8192)goto invalid;
    xvalue* Meta=xrtJsonParse(xrtStrViewN(Source,(size_t)(End-Source)));
    if(!Meta||xrtValueType(Meta)!=XVALUE_OBJECT){xrtValueRelease(Meta);goto invalid;}
    cstr Name=CommandText(Meta,"id"),Description=CommandText(Meta,"description");
    if(!Name||strcmp(Name,Id)||!Description||!Description[0]||strlen(Description)>2048)goto bad;
    const char* Keys[]={"id","name","description","argument_hint","effects","requires_session","timeout_ms"};
    for(size_t i=0;i<xrtValueCount(Meta);i++){
        xstrview Key={0};xrtValueObjectAt(Meta,i,&Key);bool Known=false;
        for(size_t j=0;j<sizeof(Keys)/sizeof(*Keys);j++)
            if(Key.Size==strlen(Keys[j])&&!memcmp(Key.Data,Keys[j],Key.Size))Known=true;
        if(!Known)goto bad;
    }
    for(size_t i=1;i<=3;i++)if(xrtValueObjectHas(Meta,xrtStrView(Keys[i]))&&
        (!CommandText(Meta,Keys[i])||strlen(CommandText(Meta,Keys[i]))>2048))goto bad;
    const xvalue* Effects=xrtValueObjectGet(Meta,XRT_STR_LITERAL("effects"));
    if(Effects){
        if(xrtValueType(Effects)!=XVALUE_ARRAY||xrtValueCount(Effects)>8)goto bad;
        for(size_t i=0;i<xrtValueCount(Effects);i++){
            xstrview E={0};if(!xrtValueGetString(xrtValueArrayGet(Effects,i),&E))goto bad;
            const char* Allowed[]={"read","workspace-write","process","network","external-service","secrets","ui","model"};
            bool Known=false;
            for(size_t j=0;j<sizeof(Allowed)/sizeof(*Allowed);j++)
                if(E.Size==strlen(Allowed[j])&&!memcmp(E.Data,Allowed[j],E.Size))Known=true;
            if(!Known)goto bad;
        }
    }
    uint64 Timeout=30000;const xvalue* T=xrtValueObjectGet(Meta,XRT_STR_LITERAL("timeout_ms"));
    if(T&&(!CommandUInt(T,&Timeout)||!Timeout||Timeout>600000))goto bad;
    bool Required=false;T=xrtValueObjectGet(Meta,XRT_STR_LITERAL("requires_session"));
    if(T&&!xrtValueGetBool(T,&Required))goto bad;
    if(!xrtValueObjectSetNew(Meta,XRT_STR_LITERAL("timeout_ms"),xrtValueUInt(Timeout)))goto bad;
    return Meta;
bad:xrtValueRelease(Meta);
invalid:if(Error)snprintf(Error,512,"Invalid C command: require matching id, description and a bounded /* mdo-command JSON */ header");
    return NULL;
}

xvalue* MdoCommandArguments(cstr Raw,char Error[512])
{
    if(Error)Error[0]=0;
    if(!Raw)Raw="";
    if(strlen(Raw)>MDO_COMMAND_ARGS_LIMIT||!xrtUtf8Valid(xrtStrView(Raw),NULL))goto invalid;
    while(isspace((unsigned char)*Raw))Raw++;
    if(*Raw=='['){
        xjsonreadconfig Config;xrtJsonReadConfigInit(&Config);Config.MaxInputBytes=MDO_COMMAND_ARGS_LIMIT;
        Config.MaxDepth=16;Config.MaxValues=4096;Config.MaxStringBytes=MDO_COMMAND_ARGS_LIMIT;
        xvalue* A=xrtJsonRead(xrtStrView(Raw),&Config);
        if(A&&xrtValueType(A)==XVALUE_ARRAY&&xrtValueCount(A)<=128)return A;
        xrtValueRelease(A);goto invalid;
    }
    xvalue* A=xrtValueArray();if(!A)return NULL;
    char* Word=xrtMalloc(strlen(Raw)+1);if(!Word){xrtValueRelease(A);return NULL;}
    while(*Raw){
        while(isspace((unsigned char)*Raw))Raw++;
        if(!*Raw)break;
        size_t N=0;char Quote=0;
        while(*Raw&&(Quote||!isspace((unsigned char)*Raw))){
            char C=*Raw++;
            if(C=='\\'&&*Raw&&((Quote!='"'&&Quote!='\'')?
                (*Raw=='"'||*Raw=='\''||isspace((unsigned char)*Raw)):
                (Quote=='"'&&*Raw=='"'))){Word[N++]=*Raw++;continue;}
            if((C=='"'||C=='\'')&&(!Quote||Quote==C)){Quote=Quote?0:C;continue;}
            Word[N++]=C;
        }
        if(Quote||xrtValueCount(A)>=128)goto bad_args;
        Word[N]=0;if(!xrtValueArrayAppendNew(A,xrtValueString(xrtStrViewN(Word,N))))goto bad_args;
    }
    xrtFree(Word);return A;
bad_args:xrtFree(Word);xrtValueRelease(A);
invalid:if(Error)snprintf(Error,512,"Command arguments require valid UTF-8, balanced quotes, at most 128 arguments or one JSON array (64 KiB)");
    return NULL;
}

static bool CommandMissing(void)
{
    bool Missing=xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND;
    if(Missing)xrtClearError();
    return Missing;
}
static char* CommandReadFile(xfile F)
{
    if(!F)return NULL;
    uint64 Size=0;char* Text=NULL;
    bool Ok=xrtFileSize(F,&Size)&&Size<=MDO_COMMAND_TEXT_LIMIT;
    if(Ok)Text=xrtMalloc((size_t)Size+1);
    Ok=Ok&&Text&&xrtReadFull(F,Text,(size_t)Size,NULL);if(!xrtClose(F))Ok=false;
    if(Ok){Text[Size]=0;if(memchr(Text,0,(size_t)Size))Ok=false;}
    if(!Ok){xrtFree(Text);return NULL;}return Text;
}
xvalue* MdoCommandProjectSettings(xroot Root) {
    xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ|XFILE_NOFOLLOW;
    xfile F=xrtRootFileOpen(Root,".mdo/commands.json",&O);
    if(!F&&CommandMissing())return xrtJsonParse(XRT_STR_LITERAL("{\"schema_version\":1,\"disabled\":{}}"));
    char* Text=CommandReadFile(F);xvalue* V=Text?xrtJsonParse(xrtStrView(Text)):NULL;xrtFree(Text);
    uint64 Schema=0;const xvalue* Disabled=xrtValueObjectGet(V,XRT_STR_LITERAL("disabled"));
    bool Ok=xrtValueType(V)==XVALUE_OBJECT&&xrtValueCount(V)==2&&CommandUInt(xrtValueObjectGet(V,XRT_STR_LITERAL("schema_version")),&Schema)&&
        Schema==1&&xrtValueType(Disabled)==XVALUE_OBJECT&&xrtValueCount(Disabled)<=MDO_COMMAND_LIMIT;
    for(size_t i=0;Ok&&i<xrtValueCount(Disabled);i++){
        xstrview Key={0};const xvalue* Item=xrtValueObjectAt(Disabled,i,&Key);bool Value=false;
        Ok=Key.Size>0&&Key.Size<=64&&!memchr(Key.Data,0,Key.Size)&&MdoExtensionIdValid(Key.Data)&&xrtValueGetBool(Item,&Value);
    }
    if(!Ok){xrtValueRelease(V);return NULL;}return V;
}
xvalue* MdoCommandCatalog(cstr Workspace)
{
    char Error[512]={0};xvalue* Rows=MdoExtensionsList("commands",Error),*Map=xrtValueObject();
    if(!Rows||!Map||xrtValueCount(Rows)>MDO_COMMAND_LIMIT)goto fail;
    for(size_t I=0;I<xrtValueCount(Rows);I++){
        const xvalue* Row=xrtValueArrayGet(Rows,I);cstr Id=CommandText(Row,"id"),Alias=CommandText(Row,"alias");
        bool Valid=false,Enabled=false;xrtValueGetBool(xrtValueObjectGet(Row,XRT_STR_LITERAL("valid")),&Valid);
        xrtValueGetBool(xrtValueObjectGet(Row,XRT_STR_LITERAL("enabled")),&Enabled);
        if(!Alias||!MdoExtensionAlias(Alias))continue;
        char* Path=Valid?MdoExtensionsEntryPath("commands",Id,Error):NULL;
        bool Missing=false;char* Source=Path?MdoExtensionRead(Path,true,MDO_COMMAND_TEXT_LIMIT,&Missing):NULL;
        xvalue* Item=Source?MdoCommandMetadata(Alias,Source,Error):NULL;Valid=Item!=NULL;
        if(!Item)Item=xrtValueObject();char Revision[65]={0};
        if(Valid)Valid=MdoExtensionsFingerprint("commands",Id,Revision,NULL,Error);
        bool Ok=Item&&CommandSet(Item,"id",Alias)&&CommandSet(Item,"directory_id",Id)&&CommandSet(Item,"scope",CommandText(Row,"source"))&&
            CommandSet(Item,"directory_root",CommandText(Row,"root"))&&CommandSet(Item,"path",Path)&&CommandSet(Item,"revision",Revision)&&
            CommandSet(Item,"diagnostic",Valid?"":Error[0]?Error:"Command directory is invalid")&&
            xrtValueObjectSetNew(Item,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled))&&
            xrtValueObjectSetNew(Item,XRT_STR_LITERAL("valid"),xrtValueBool(Valid));
        const xvalue* Previous=xrtValueObjectGet(Map,xrtStrView(Alias));
        if(Previous){
            Ok=Ok&&xrtValueObjectSetNew(Item,XRT_STR_LITERAL("valid"),xrtValueBool(false))&&
                CommandSet(Item,"diagnostic","Multiple installed directories declare this command alias; rename or uninstall one");
        }
        if(Ok)Ok=xrtValueObjectSet(Map,xrtStrView(Alias),Item);
        xrtValueRelease(Item);xrtFree(Source);xrtFree(Path);if(!Ok)goto fail;
    }
    if(Workspace&&Workspace[0]){
        xroot Root=xrtRootOpen(Workspace);if(!Root){if(CommandMissing())goto done;goto fail;}
        xvalue* Settings=MdoCommandProjectSettings(Root);xrtRootClose(Root);if(!Settings)goto fail;
        const xvalue* Disabled=xrtValueObjectGet(Settings,XRT_STR_LITERAL("disabled"));
        for(size_t I=0;I<xrtValueCount(Disabled);I++){
            xstrview Key={0};bool Value=false;xrtValueGetBool(xrtValueObjectAt(Disabled,I,&Key),&Value);
            xvalue* Item=xrtValueObjectGet(Map,Key);if(Item&&Value)xrtValueObjectSetNew(Item,XRT_STR_LITERAL("enabled"),xrtValueBool(false));
        }
        xrtValueRelease(Settings);
    }
done:xrtValueRelease(Rows);return Map;
fail:xrtValueRelease(Rows);xrtValueRelease(Map);return NULL;
}
bool MdoCommandResolve(cstr Workspace,cstr Id,char** Source,xvalue** Metadata,char Revision[65],char Error[512])
{
    *Source=NULL;*Metadata=NULL;Error[0]=0;xvalue* Map=MdoCommandCatalog(Workspace);
    const xvalue* Item=xrtValueObjectGet(Map,xrtStrView(Id));bool Enabled=false,Valid=false;
    xrtValueGetBool(xrtValueObjectGet(Item,XRT_STR_LITERAL("enabled")),&Enabled);
    xrtValueGetBool(xrtValueObjectGet(Item,XRT_STR_LITERAL("valid")),&Valid);
    if(!Item||!Enabled||!Valid){snprintf(Error,512,"Command is missing, disabled, ambiguous or invalid");xrtValueRelease(Map);return false;}
    cstr Directory=CommandText(Item,"directory_id"),Path=CommandText(Item,"path"),Expected=CommandText(Item,"revision");
    bool Ok=MdoExtensionsVerify("commands",Directory,NULL,Error),Missing=false;
    if(Ok)*Source=MdoExtensionRead(Path,true,MDO_COMMAND_TEXT_LIMIT,&Missing);
    char Hash[65]={0};Ok=Ok&&*Source&&MdoExtensionsFingerprint("commands",Directory,Hash,NULL,Error)&&Expected&&!strcmp(Hash,Expected);
    if(Ok){*Metadata=xrtValueClone(Item);snprintf(Revision,65,"%s",Hash);}
    xrtValueRelease(Map);
    if(!*Metadata){xrtFree(*Source);*Source=NULL;if(!Error[0])snprintf(Error,512,"Command directory changed while reading; refresh the catalog");}
    return *Metadata!=NULL;
}
