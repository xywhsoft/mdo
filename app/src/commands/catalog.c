#include "../../include/mdo/commands.h"
#include <ctype.h>

static cstr CommandText(const xvalue* V,cstr Key)
{
    xstrview S={0};if(!xrtValueGetString(xrtValueObjectGet(V,xrtStrView(Key)),&S)||
        memchr(S.Data,0,S.Size))return NULL;
    return S.Data;
}
static bool CommandSet(xvalue* V,cstr K,cstr S)
{return xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S?S:"")));}

xvalue* MdoCommandMetadata(cstr Id,cstr Source,char Error[512])
{
    if(Error)Error[0]=0;
    if(!MdoExtensionIdValid(Id)||!Source||strlen(Source)>MDO_COMMAND_TEXT_LIMIT||
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
    if(T&&(!xrtValueGetUInt(T,&Timeout)||!Timeout||Timeout>600000))goto bad;
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
                (*Raw=='\\'||*Raw=='"'||*Raw=='\''||isspace((unsigned char)*Raw)):
                (Quote=='"'&&(*Raw=='"'||*Raw=='\\')))){Word[N++]=*Raw++;continue;}
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
static void CommandPut(xvalue* Map,cstr Id,cstr Source,cstr Scope,cstr Path,bool Enabled)
{
    if(!MdoExtensionIdValid(Id))return;
    char Error[512]={0},Hash[65]={0};
    xvalue* Item=Source?MdoCommandMetadata(Id,Source,Error):NULL;bool Valid=Item!=NULL;
    if(!Item)Item=xrtValueObject();
    if(!Item)return;
    if(Source)MdoExtensionHash(Source,Hash);
    CommandSet(Item,"id",Id);CommandSet(Item,"scope",Scope);CommandSet(Item,"path",Path);
    CommandSet(Item,"revision",Hash);CommandSet(Item,"diagnostic",Error[0]?Error:Source?"":"Command source cannot be read safely");
    xrtValueObjectSetNew(Item,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled));
    xrtValueObjectSetNew(Item,XRT_STR_LITERAL("valid"),xrtValueBool(Valid));
    xrtValueObjectSetNew(Map,xrtStrView(Id),Item);
}
xvalue* MdoCommandCatalog(cstr Workspace)
{
    xvalue* Map=xrtValueObject();if(!Map)return NULL;
    xdir Dir=xrtVfsDirOpen(MdoHomeApplicationVfs(),"/app/default-home/commands",XDIR_STAT);
    if(!Dir&&!CommandMissing()){xrtValueRelease(Map);return NULL;}
    xdirentry E;xdirnext Next=XDIR_NEXT_END;size_t Count=0;
    while(Dir&&(Next=xrtDirNext(Dir,&E))==XDIR_NEXT_ITEM){
        if(!(E.Flags&XDIR_ENTRY_UTF8)||E.Name.Size<3||E.Name.Size>66||
            memcmp(E.Name.Data+E.Name.Size-2,".c",2))continue;
        if(++Count>MDO_COMMAND_LIMIT)goto fail;
        char Id[65],Path[160];memcpy(Id,E.Name.Data,E.Name.Size-2);Id[E.Name.Size-2]=0;
        if(!MdoExtensionIdValid(Id))continue;
        snprintf(Path,sizeof(Path),"commands/%s.c",Id);bool Missing=false,Enabled=true;
        char* Text=MdoExtensionRead(Path,false,MDO_COMMAND_TEXT_LIMIT,&Missing);
        bool External=false;xfileinfo Info={0};(void)MdoHomeExternalStat(Path,&External,&Info);
        if(!MdoExtensionEnabled("commands",Id,&Enabled)){xrtFree(Text);goto fail;}
        CommandPut(Map,Id,Text,External?"user":"builtin",Path,Enabled);xrtFree(Text);
    }
    if(Dir&&!xrtDirClose(Dir)){Dir=NULL;goto fail;}Dir=NULL;
    if(Next==XDIR_NEXT_ERROR)goto fail;
    if(Workspace&&Workspace[0]){
        xroot Root=xrtRootOpen(Workspace);if(!Root)goto fail;
        Dir=xrtRootDirOpen(Root,".mdo/commands",XDIR_STAT);
        if(!Dir&&!CommandMissing()){xrtRootClose(Root);goto fail;}
        Next=XDIR_NEXT_END;
        while(Dir&&(Next=xrtDirNext(Dir,&E))==XDIR_NEXT_ITEM){
            if(!(E.Flags&XDIR_ENTRY_UTF8)||E.Name.Size<3||E.Name.Size>66||
                memcmp(E.Name.Data+E.Name.Size-2,".c",2))continue;
            if(++Count>2*MDO_COMMAND_LIMIT){xrtRootClose(Root);goto fail;}
            char Id[65],Path[160];memcpy(Id,E.Name.Data,E.Name.Size-2);Id[E.Name.Size-2]=0;
            if(!MdoExtensionIdValid(Id))continue;
            snprintf(Path,sizeof(Path),".mdo/commands/%s.c",Id);
            xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ|XFILE_NOFOLLOW;
            char* Text=CommandReadFile(xrtRootFileOpen(Root,Path,&O));
            CommandPut(Map,Id,Text,"project",Path,true);xrtFree(Text);
        }
        xrtRootClose(Root);
        if(Dir&&!xrtDirClose(Dir)){Dir=NULL;goto fail;}Dir=NULL;
        if(Next==XDIR_NEXT_ERROR)goto fail;
    }
    if(xrtValueCount(Map)>MDO_COMMAND_LIMIT)goto fail;
    return Map;
fail:if(Dir)xrtDirClose(Dir);xrtValueRelease(Map);return NULL;
}
bool MdoCommandResolve(cstr Workspace,cstr Id,char** Source,xvalue** Metadata,char Revision[65],char Error[512])
{
    *Source=NULL;*Metadata=NULL;xvalue* Map=MdoCommandCatalog(Workspace);
    const xvalue* Item=xrtValueObjectGet(Map,xrtStrView(Id));bool Enabled=false,Valid=false;
    xrtValueGetBool(xrtValueObjectGet(Item,XRT_STR_LITERAL("enabled")),&Enabled);
    xrtValueGetBool(xrtValueObjectGet(Item,XRT_STR_LITERAL("valid")),&Valid);
    if(!Item||!Enabled||!Valid){snprintf(Error,512,"Command is missing, disabled or invalid");xrtValueRelease(Map);return false;}
    cstr Scope=CommandText(Item,"scope"),Path=CommandText(Item,"path");
    if(Scope&&!strcmp(Scope,"project")){
        xroot Root=xrtRootOpen(Workspace);xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ|XFILE_NOFOLLOW;
        *Source=Root?CommandReadFile(xrtRootFileOpen(Root,Path,&O)):NULL;if(Root)xrtRootClose(Root);
    }else{bool Missing=false;*Source=MdoExtensionRead(Path,false,MDO_COMMAND_TEXT_LIMIT,&Missing);}
    char Hash[65]={0};bool Ok=*Source&&MdoExtensionHash(*Source,Hash)&&!strcmp(Hash,CommandText(Item,"revision"));
    *Metadata=Ok?MdoCommandMetadata(Id,*Source,Error):NULL;
    if(*Metadata)snprintf(Revision,65,"%s",Hash);
    xrtValueRelease(Map);
    if(!*Metadata){xrtFree(*Source);*Source=NULL;if(!Error[0])snprintf(Error,512,"Command changed while reading; retry discovery");}
    return *Metadata!=NULL;
}

