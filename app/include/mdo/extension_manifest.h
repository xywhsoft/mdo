#ifndef MDO_EXTENSION_MANIFEST_H
#define MDO_EXTENSION_MANIFEST_H
#include "extension_zip.h"
#include "extension_identity.h"
#include "extension_settings_form.h"

#define MDO_EXTENSION_MANIFEST_BYTES (64u*1024u)
static cstr MdoExtensionText(const xvalue* O,cstr Key,size_t Limit)
{
    xstrview V={0};return xrtValueGetString(xrtValueObjectGet(O,xrtStrView(Key)),&V)&&V.Size<=Limit&&!memchr(V.Data,0,V.Size)&&xrtUtf8Valid(V,NULL)?V.Data:NULL;
}
static bool MdoExtensionNumber(const xvalue* Object,cstr Key,int64* Result)
{
    const xvalue* Value=xrtValueObjectGet(Object,xrtStrView(Key));uint64 N=0;
    if(xrtValueGetInt(Value,Result))return true;
    if(!xrtValueGetUInt(Value,&N)||N>INT64_MAX)return false;
    *Result=(int64)N;
    return true;
}
static bool MdoExtensionPlatform(cstr P)
{
    const char* const Names[]={"any","windows-x86_64","linux-x86_64","linux-x86_64-glibc","linux-x86_64-musl","linux-arm64","android-arm64-v8a","macos-arm64"};
    for(size_t I=0;
    I<sizeof(Names)/sizeof(*Names);
    I++)if(P&&!strcmp(P,Names[I]))return true;
    return false;
}
static bool MdoExtensionSupportsPlatform(const xvalue* Manifest,cstr Platform)
{
    if(!Platform)return false;
    const xvalue* Ps=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("platforms"));
    for(size_t I=0;I<xrtValueCount(Ps);I++){xstrview P={0};if(!xrtValueGetString(xrtValueArrayGet(Ps,I),&P))continue;
        if(xrtStrEqual(P,XRT_STR_LITERAL("any"))||xrtStrEqual(P,xrtStrView(Platform))||(!strncmp(Platform,"linux-x86_64-",13)&&xrtStrEqual(P,XRT_STR_LITERAL("linux-x86_64"))))return true;}
    return false;
}
static cstr MdoExtensionRuntimePlatform(void)
{
#if defined(__ANDROID__)
    return "android-arm64-v8a";
#elif defined(_WIN32) || defined(_WIN64)
    return "windows-x86_64";
#elif defined(__linux__) && defined(__x86_64__) && defined(MDO_PRODUCT_LIBC)
    return "linux-x86_64-" MDO_PRODUCT_LIBC;
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-arm64";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos-arm64";
#else
    return "unsupported";
#endif
}
static bool MdoExtensionRelations(const xvalue* M,cstr Field,bool Optional,char* Error,size_t Capacity)
{
    const xvalue* List=xrtValueObjectGet(M,xrtStrView(Field));if(!List)return true;
    cstr Self=MdoExtensionText(M,"id",MDO_EXTENSION_REF_BYTES),Kind=MdoExtensionText(M,"kind",32);
    if(xrtValueType(List)!=XVALUE_ARRAY||xrtValueCount(List)>MDO_EXTENSION_RELATIONS)goto bad;
    for(size_t I=0;I<xrtValueCount(List);I++){
        const xvalue* R=xrtValueArrayGet(List,I);cstr Id=MdoExtensionText(R,"id",MDO_EXTENSION_REF_BYTES),Target=MdoExtensionRefKind(Id);bool Flag=false;
        if(xrtValueType(R)!=XVALUE_OBJECT||!Target||(Self&&!strcmp(Id,Self))||(Kind&&!strcmp(Kind,"knowledge")&&strcmp(Target,"knowledge")))goto bad;
        const xvalue* V=xrtValueObjectGet(R,XRT_STR_LITERAL("optional"));if(V&&(!Optional||!xrtValueGetBool(V,&Flag)))goto bad;
        if(xrtValueObjectHas(R,XRT_STR_LITERAL("reason"))&&!MdoExtensionText(R,"reason",600))goto bad;
        for(size_t J=0;J<xrtValueCount(R);J++){xstrview K;xrtValueObjectAt(R,J,&K);if(!xrtStrEqual(K,XRT_STR_LITERAL("id"))&&!xrtStrEqual(K,XRT_STR_LITERAL("reason"))&&!(Optional&&xrtStrEqual(K,XRT_STR_LITERAL("optional"))))goto bad;}
        for(size_t J=0;J<I;J++)if(!strcmp(Id,MdoExtensionText(xrtValueArrayGet(List,J),"id",MDO_EXTENSION_REF_BYTES)))goto bad;
    }
    return true;
bad:if(Error&&Capacity)snprintf(Error,Capacity,"Invalid %s: use unique typed extension IDs, optional flags and reasons; knowledge can reference only knowledge",Field);return false;
}
static bool MdoExtensionPresentation(const xvalue* M,char* Error,size_t Capacity)
{
    int64 Rank=0;const xvalue* Sort=xrtValueObjectGet(M,XRT_STR_LITERAL("sort_order")),*Tags=xrtValueObjectGet(M,XRT_STR_LITERAL("tags"));
    if(Sort&&(!MdoExtensionNumber(M,"sort_order",&Rank)||Rank<INT32_MIN||Rank>INT32_MAX))goto bad;
    if(!Tags)return true;
    if(xrtValueType(Tags)!=XVALUE_ARRAY||xrtValueCount(Tags)>16)goto bad;
    for(size_t I=0;I<xrtValueCount(Tags);I++){xstrview Tag={0};if(!xrtValueGetString(xrtValueArrayGet(Tags,I),&Tag)||!Tag.Size||Tag.Size>64||memchr(Tag.Data,0,Tag.Size)||!xrtUtf8Valid(Tag,NULL)||xrtStrTrim(Tag).Size!=Tag.Size)goto bad;
        for(size_t K=0;K<Tag.Size;K++)if((unsigned char)Tag.Data[K]<32||(unsigned char)Tag.Data[K]==127)goto bad;
        for(size_t J=0;J<I;J++){xstrview Prior={0};xrtValueGetString(xrtValueArrayGet(Tags,J),&Prior);if(xrtStrEqual(Tag,Prior))goto bad;}}
    return true;
bad:if(Error&&Capacity)snprintf(Error,Capacity,"Use an int32 sort_order and up to 16 unique, nonempty tags of at most 64 UTF-8 bytes");return false;
}
static bool MdoExtensionManifest(const xvalue* M,char* Error,size_t Capacity)
{
    cstr Why="extension.json must describe one of the nine extension types";
    if(xrtValueType(M)!=XVALUE_OBJECT)goto bad;
    if(!MdoExtensionPresentation(M,Error,Capacity))return false;
    const xvalue* Settings=xrtValueObjectGet(M,XRT_STR_LITERAL("settings"));bool Off=false;
    if(Settings&&!(xrtValueGetBool(Settings,&Off)&&!Off)){
        cstr Form=MdoEsText(Settings,"form");
        if(!MdoEsKeys(Settings,"|form||templates||environment|")||!Form||!MdoExtensionRelativePath(Form)||!strcmp(Form,"extension.json")){Why="Settings require an in-directory form JSON file";goto bad;}
        const xvalue* Templates=MdoEsGet(Settings,"templates");
        if(Templates&&(xrtValueType(Templates)!=XVALUE_ARRAY||xrtValueCount(Templates)>32)){Why="Declare at most 32 explicit text template paths";goto bad;}
        for(size_t I=0;I<xrtValueCount(Templates);I++){xstrview P={0};if(!xrtValueGetString(xrtValueArrayGet(Templates,I),&P)||memchr(P.Data,0,P.Size)||!MdoExtensionRelativePath(P.Data)||!strcmp(P.Data,"extension.json")||!strcmp(P.Data,Form)){Why="Invalid settings text template path";goto bad;}size_t N=P.Size;bool Text=(N>3&&!strcmp(P.Data+N-3,".md"))||(N>4&&!strcmp(P.Data+N-4,".txt"));if(!Text){Why="Only explicit Markdown or plain text resources can be settings templates";goto bad;}for(size_t J=0;J<I;J++){xstrview Q={0};xrtValueGetString(xrtValueArrayGet(Templates,J),&Q);if(xrtStrEqual(P,Q)){Why="Duplicate settings template path";goto bad;}}}
        const xvalue* Env=MdoEsGet(Settings,"environment");if(Env&&(xrtValueType(Env)!=XVALUE_OBJECT||xrtValueCount(Env)>32)){Why="Settings environment must map at most 32 variable names to fields";goto bad;}
        for(size_t I=0;I<xrtValueCount(Env);I++){xstrview K={0},V={0};const xvalue* X=xrtValueObjectAt(Env,I,&K);if(!MdoEsName(K.Data)||!xrtValueGetString(X,&V)||memchr(V.Data,0,V.Size)||!MdoEsName(V.Data)){Why="Use identifier environment names and settings fields";goto bad;}}
    }
    const char* const Removed[]={"format","resources","files","base64","min_build_id","format_version"};
    for(size_t I=0;I<sizeof(Removed)/sizeof(*Removed);I++)if(xrtValueObjectHas(M,xrtStrView(Removed[I]))){Why="Legacy package fields are not accepted in extension.json";goto bad;}
    cstr Kind=MdoExtensionText(M,"kind",32),Name=MdoExtensionText(M,"name",240),Alias=MdoExtensionText(M,"alias",64),Entry=MdoExtensionText(M,"entry",MDO_EXTENSION_PATH_BYTES);
    if(!MdoExtensionDirectoryKind(Kind)||!Name||!xrtStrTrim(xrtStrView(Name)).Size||!MdoExtensionAlias(Alias))goto bad;
    cstr Id=MdoExtensionText(M,"id",MDO_EXTENSION_REF_BYTES),IdKind=MdoExtensionRefKind(Id);
    if(!IdKind||strcmp(IdKind,Kind)){Why="Declare a stable typed id in extension.json, for example skill:publisher.name; it must match kind";goto bad;}
    if(!MdoExtensionRelations(M,"dependencies",true,Error,Capacity)||!MdoExtensionRelations(M,"conflicts",false,Error,Capacity))return false;
    if(!Entry||!MdoExtensionRelativePath(Entry)||!strcmp(Entry,"extension.json")){Why="Extension entry must reference a file inside its own directory";goto bad;}
    const char* const Optional[]={"description","author","copyright","license","homepage","version"};const size_t Limits[]={2048,240,4096,1024,2048,128};
    for(size_t I=0;I<sizeof(Optional)/sizeof(*Optional);I++)if(xrtValueObjectHas(M,xrtStrView(Optional[I]))&&!MdoExtensionText(M,Optional[I],Limits[I])){Why="Invalid extension display metadata";goto bad;}
    const char* const Paths[]={"native","frontend","readme"};
    for(size_t I=0;I<sizeof(Paths)/sizeof(*Paths);I++)if(xrtValueObjectHas(M,xrtStrView(Paths[I]))){cstr P=MdoExtensionText(M,Paths[I],MDO_EXTENSION_PATH_BYTES);if(!P||!MdoExtensionRelativePath(P)){Why="Invalid relative extension resource path";goto bad;}}
    const xvalue* Ps=xrtValueObjectGet(M,XRT_STR_LITERAL("platforms"));
    if(xrtValueType(Ps)!=XVALUE_ARRAY||!xrtValueCount(Ps)||xrtValueCount(Ps)>8){Why="Declare extension platforms or any";goto bad;}
    for(size_t I=0;I<xrtValueCount(Ps);I++){xstrview P={0};if(!xrtValueGetString(xrtValueArrayGet(Ps,I),&P)||memchr(P.Data,0,P.Size)||!MdoExtensionPlatform(P.Data)){Why="Unknown extension platform";goto bad;}}
    return true;
bad:if(Error&&Capacity)snprintf(Error,Capacity,"%s",Why);return false;
}
typedef struct MdoExtensionManifestBuffer {uint8* Data;size_t Used,Capacity;} MdoExtensionManifestBuffer;
static bool MdoExtensionManifestSink(const void* Data,size_t Size,void* User)
{MdoExtensionManifestBuffer* B=User;if(Size>B->Capacity-B->Used)return false;memcpy(B->Data+B->Used,Data,Size);B->Used+=Size;return true;}
static xvalue* MdoExtensionZipManifest(MdoZip* Z,xcancel* Cancel)
{
    int64 At=MdoZipFind(Z,"extension.json");if(At<0){MdoZipFail(Z,"ZIP root must contain extension.json; no enclosing folder or payload wrapper");return NULL;}
    MdoZipEntry* E=&Z->Entries[At];if(E->Directory||!E->Bytes||E->Bytes>MDO_EXTENSION_MANIFEST_BYTES){MdoZipFail(Z,"Invalid extension.json size");return NULL;}
    MdoExtensionManifestBuffer B={0};B.Capacity=(size_t)E->Bytes;B.Data=xrtMalloc(B.Capacity+1);if(!B.Data)return NULL;
    bool Ok=MdoZipEntryRead(Z,(size_t)At,MdoExtensionManifestSink,&B,Cancel,NULL)&&B.Used==B.Capacity&&!memchr(B.Data,0,B.Used);
    xjsonreadconfig C;xrtJsonReadConfigInit(&C);C.MaxInputBytes=MDO_EXTENSION_MANIFEST_BYTES;C.MaxStringBytes=MDO_EXTENSION_MANIFEST_BYTES;C.MaxValues=4096;C.MaxDepth=16;
    xvalue* M=Ok?xrtJsonRead(xrtStrViewN((cstr)B.Data,B.Used),&C):NULL;xrtFree(B.Data);
    if(!M||!MdoExtensionManifest(M,Z->Error,sizeof(Z->Error))){xrtValueRelease(M);return NULL;}
    const char* const Keys[]={"entry","native","frontend","readme"};
    for(size_t I=0;I<sizeof(Keys)/sizeof(*Keys);I++){cstr P=MdoExtensionText(M,Keys[I],MDO_EXTENSION_PATH_BYTES);if(!P)continue;int64 Index=MdoZipFind(Z,P);
        if(Index<0||Z->Entries[Index].Directory||(!Z->Entries[Index].Bytes&&strcmp(Keys[I],"readme"))){xrtValueRelease(M);MdoZipFail(Z,"Declared extension entry or resource is missing or empty");return NULL;}}
    const xvalue* Settings=MdoEsGet(M,"settings");cstr Form=MdoEsText(Settings,"form");
    if(Form){
        int64 Index=MdoZipFind(Z,Form);xvalue* Definition=NULL;MdoExtensionManifestBuffer F={0};
        bool Good=Index>=0&&!Z->Entries[Index].Directory&&Z->Entries[Index].Bytes&&Z->Entries[Index].Bytes<=MDO_ES_FORM_BYTES;
        if(Good){F.Capacity=(size_t)Z->Entries[Index].Bytes;F.Data=xrtMalloc(F.Capacity+1);Good=F.Data&&MdoZipEntryRead(Z,(size_t)Index,MdoExtensionManifestSink,&F,Cancel,NULL)&&F.Used==F.Capacity&&!memchr(F.Data,0,F.Used);}
        if(Good){Definition=xrtJsonParse(xrtStrViewN((cstr)F.Data,F.Used));}
        xrtFree(F.Data);
        Good=Good&&Definition&&MdoExtensionSettingsFormValid(Definition,Z->Error,sizeof(Z->Error));
        const xvalue* Env=MdoEsGet(Settings,"environment");for(size_t I=0;Good&&I<xrtValueCount(Env);I++){xstrview Field={0};xrtValueGetString(xrtValueObjectAt(Env,I,NULL),&Field);const xvalue* Fld=MdoEsFind(Definition,Field.Data);cstr T=MdoEsText(Fld,"type");Good=Fld&&strcmp(T,"multiselect");}
        const xvalue* Templates=MdoEsGet(Settings,"templates");for(size_t I=0;Good&&I<xrtValueCount(Templates);I++){xstrview P={0};xrtValueGetString(xrtValueArrayGet(Templates,I),&P);int64 At=MdoZipFind(Z,P.Data);Good=At>=0&&!Z->Entries[At].Directory&&Z->Entries[At].Bytes&&Z->Entries[At].Bytes<=128u*1024u;}
        xrtValueRelease(Definition);if(!Good){xrtValueRelease(M);MdoZipFail(Z,"Extension settings form, field binding or text template is invalid or missing");return NULL;}
    }
    return M;
}
#endif
