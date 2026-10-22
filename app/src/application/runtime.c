#include "internal.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/secrets.h"
#include "../../include/mdo/power.h"
#include "../../include/mdo/version.h"
#include "../../include/mdo/time.h"

static const struct {cstr Id,Description;} SdkRuntimeMethods[]={
    {"runtime.info","Application version, paths, platform and power state"},
    {"runtime.log","Write an extension log message"},{"runtime.clock","Read monotonic and wall clocks"},
    {"runtime.sleep","Cancellable wait in milliseconds"},{"runtime.env.get","Read a process environment variable"},
    {"runtime.env.set","Set a process environment variable"},{"system.open","Open a file or URL with the operating system"},
    {"fs.resolve","Resolve a native, workspace, Home or application resource path"},
    {"fs.stat","Read file identity, size and modification time"},{"fs.read","Read a bounded byte range with text/base64 results"},
    {"fs.write","Write text/base64 bytes, atomically by default"},{"fs.append","Append text/base64 bytes"},
    {"fs.list","List a directory with pagination"},{"fs.mkdir","Create a directory tree"},
    {"fs.remove","Remove a file or an explicitly selected directory tree"},
    {"fs.move","Move a native path"},{"fs.copy","Copy a file"},
    {"vfs.read","Read an application VFS resource"},{"vfs.list","List an application VFS directory"},
    {"storage.read","Read an extension-owned persistent resource"},{"storage.write","Write an extension-owned persistent resource"},
    {"storage.remove","Remove an extension-owned persistent resource"},
    {"secret.store","Seal a credential and return its reference"},{"secret.read","Resolve an authorized credential reference"},
    {"secret.remove","Discard a credential reference"},
    {"process.run","Run a program with typed argv, output capture, cancellation and exact exit status"},
    {"network.request","Perform an HTTP(S) request with headers, body and bounded binary output"}
};
xvalue* MdoApplicationRuntimeCatalog(void){
    xvalue* A=xrtValueArray();
    for(size_t i=0;i<sizeof(SdkRuntimeMethods)/sizeof(*SdkRuntimeMethods);i++){
        xvalue* V=xrtValueObject();SdkPut(V,"id",SdkRuntimeMethods[i].Id);SdkPut(V,"description",SdkRuntimeMethods[i].Description);
        SdkPut(V,"domain","runtime");SdkPut(V,"transport","native");
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("input_schema"),SdkParse("{\"type\":\"object\"}"));xrtValueArrayAppendNew(A,V);
    }return A;
}
static bool SdkNativeFailure(mdo_error_v1* E,cstr Stage){
    const xerror* X=xrtGetError();xerrkind Kind=X?xrtErrorKind(X):XERR_INTERNAL;
    uint32 Status=Kind==XERR_NOT_FOUND?404:Kind==XERR_PERMISSION?403:Kind==XERR_ARGUMENT||Kind==XERR_RANGE||Kind==XERR_TYPE||Kind==XERR_VALUE?400:Kind==XERR_EXISTS||Kind==XERR_STATE?409:Kind==XERR_TIMEOUT?504:Kind==XERR_CANCELLED?499:500;
    MdoApplicationError(E,"sdk_native_failed",X?xrtErrorMessage(X):"Native operation failed",Stage,Status,false);
    if(E){size_t At=0;for(const xerror* Cause=X;Cause&&At+1<sizeof(E->Cause);Cause=xrtErrorCause(Cause)){
        int N=snprintf(E->Cause+At,sizeof(E->Cause)-At,"%s%s/%d: %s",At?" <- ":"",xrtErrorDomain(Cause),(int)xrtErrorCode(Cause),xrtErrorMessage(Cause));
        if(N<0||(size_t)N>=sizeof(E->Cause)-At)break;
        At+=(size_t)N;
    }}return false;
}
static char* SdkBase64(const void* Bytes,size_t Size){
    size_t N=0;if(!xrtBase64Encode(Bytes,Size,NULL,0,&N,NULL))return NULL;
    char* Text=xrtMalloc(N+1);if(!Text)return NULL;
    if(!xrtBase64Encode(Bytes,Size,Text,N+1,&N,NULL)){xrtFree(Text);return NULL;}Text[N]=0;return Text;
}
static bool SdkByteValue(xvalue* R,const void* Bytes,size_t Size){
    char* Encoded=SdkBase64(Bytes,Size);bool Ok=Encoded&&SdkPut(R,"base64",Encoded)&&SdkPutUInt(R,"bytes",Size);xrtFree(Encoded);
    if(Ok&&(!Size||!memchr(Bytes,0,Size))&&xrtUtf8Valid(xrtStrViewN(Bytes,Size),NULL))
        Ok=xrtValueObjectSetNew(R,XRT_STR_LITERAL("text"),xrtValueString(xrtStrViewN(Bytes,Size)));
    return Ok;
}
static bool SdkInputBytes(const xvalue* P,unsigned char** Out,size_t* Size,mdo_error_v1* E){
    cstr Text=SdkField(P,"text"),Encoded=SdkField(P,"base64");*Out=NULL;*Size=0;
    if((Text&&Encoded)||(!Text&&!Encoded))return MdoApplicationError(E,"sdk_arguments_invalid","Supply exactly one text or base64 field","decode",400,false);
    if(Text){*Size=strlen(Text);if(*Size>1024u*1024u)return MdoApplicationError(E,"sdk_arguments_invalid","Write chunks must not exceed 1 MiB","decode",400,false);*Out=(unsigned char*)xrtStrDup(Text);return *Out!=NULL;}
    if(strlen(Encoded)>2u*1024u*1024u||!xrtBase64Decode(Encoded,strlen(Encoded),NULL,0,Size,NULL)||*Size>1024u*1024u)
        return MdoApplicationError(E,"sdk_arguments_invalid","Invalid or oversized base64 payload; write larger data in ranges","decode",400,false);
    *Out=xrtMalloc(*Size+1);if(!*Out)return false;
    if(!xrtBase64Decode(Encoded,strlen(Encoded),*Out,*Size,Size,NULL)){xrtFree(*Out);*Out=NULL;return false;}return true;
}
static bool SdkPortablePath(cstr P){
    if(!P||!P[0]||P[0]=='/'||strchr(P,'\\')||strchr(P,':'))return false;
    for(size_t i=0;P[i];){size_t Start=i;while(P[i]&&P[i]!='/')i++;
        size_t N=i-Start;if(!N||(N==1&&P[Start]=='.')||(N==2&&P[Start]=='.'&&P[Start+1]=='.'))return false;
        if(P[i])i++;
    }return true;
}
static char* SdkNativePath(const mdo_sdk_context_v1* C,const xvalue* P,bool Storage,mdo_error_v1* E){
    cstr Path=SdkField(P,"path"),Space=SdkField(P,"space");if(!Path||!Path[0])goto bad;
    if(Storage){
        if(!SdkPortablePath(Path)||!C->ExtensionId||!SdkIdValid(C->ExtensionId))goto bad;
        char* Relative=xrtFormat("data/extensions/%s/%s",C->ExtensionId,Path);char* Native=Relative?MdoHomeExternalPath(Relative):NULL;xrtFree(Relative);return Native;
    }
    if(Space&&!strcmp(Space,"home"))return MdoHomeExternalPath(Path);
    if(Space&&!strcmp(Space,"application"))return xrtPathJoin(MdoHomeApplicationRoot(),Path);
    if(Space&&strcmp(Space,"native")&&strcmp(Space,"workspace"))goto bad;
    if(xrtPathIsRooted(Path))return xrtPathAbs(Path);
    char* Base=C->Workspace&&C->Workspace[0]?xrtStrDup(C->Workspace):MdoHomeDefaultWorkspacePath(false);
    char* Result=Base?xrtPathJoin(Base,Path):NULL;xrtFree(Base);return Result;
bad:MdoApplicationError(E,"sdk_arguments_invalid","A valid path and resource space are required","resolve",400,false);return NULL;
}
static xvalue* SdkFileInfo(const xfileinfo* I){
    xvalue* R=xrtValueObject();SdkPutUInt(R,"size",I->Size);SdkPutUInt(R,"type",I->Type);
    SdkPutUInt(R,"device",I->Device);SdkPutUInt(R,"identity",I->Identity);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("modified"),xrtValueInt(I->Modified));
    char Revision[160];snprintf(Revision,sizeof(Revision),"f-%llu-%llu-%llu-%lld",(unsigned long long)I->Device,
        (unsigned long long)I->Identity,(unsigned long long)I->Size,(long long)I->Modified);SdkPut(R,"revision",Revision);return R;
}
static bool SdkFileCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    bool Vfs=!strncmp(Method,"vfs.",4),Storage=!strncmp(Method,"storage.",8);char* Path=NULL;
    if(Vfs){cstr Name=SdkField(P,"path");Path=Name?xrtStrDup(Name):NULL;if(!Path||strncmp(Path,"/app/",5))goto invalid;}
    else Path=SdkNativePath(C,P,Storage,E);
    if(!Path)return false;
    cstr Action=strrchr(Method,'.')+1;bool Ok=false;
    if(!strcmp(Action,"resolve")){*R=xrtValueObject();Ok=SdkPut(*R,"path",Path);}
    else if(!strcmp(Action,"stat")){xfileinfo Info={0};Ok=xrtPathStat(Path,false,&Info);if(Ok)*R=SdkFileInfo(&Info);}
    else if(!strcmp(Action,"list")){
        uint64 Offset=0,Limit=128;const xvalue *O=SdkGet(P,"offset"),*L=SdkGet(P,"limit");
        if((O&&!SdkUInt(O,&Offset))||(L&&!SdkUInt(L,&Limit))||!Limit||Limit>1024)goto invalid;
        xdir Dir=Vfs?xrtVfsDirOpen(MdoHomeApplicationVfs(),Path,XDIR_STAT):xrtDirOpen(Path,XDIR_STAT);
        if(Dir){xvalue* Items=xrtValueArray();xdirentry Item;xdirnext Next;uint64 At=0,Added=0;
            while((Next=xrtDirNext(Dir,&Item))==XDIR_NEXT_ITEM){if(At++<Offset)continue;if(Added>=Limit)break;
                xvalue* Entry=SdkFileInfo(&Item.Info);xrtValueObjectSetNew(Entry,XRT_STR_LITERAL("name"),xrtValueString(Item.Name));xrtValueArrayAppendNew(Items,Entry);Added++;}
            *R=xrtValueObject();SdkPutUInt(*R,"next",Offset+Added);xrtValueObjectSetNew(*R,XRT_STR_LITERAL("more"),xrtValueBool(Next==XDIR_NEXT_ITEM));
            xrtValueObjectSetNew(*R,XRT_STR_LITERAL("items"),Items);Ok=xrtDirClose(Dir)&&Next!=XDIR_NEXT_ERROR;
        }
    }else if(!strcmp(Action,"read")){
        uint64 Offset=0,Limit=16384;const xvalue *O=SdkGet(P,"offset"),*L=SdkGet(P,"limit");
        if((O&&!SdkUInt(O,&Offset))||(L&&!SdkUInt(L,&Limit))||!Limit||Limit>256u*1024u)goto invalid;
        xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ;
        xfile File=Vfs?xrtVfsOpen(MdoHomeApplicationVfs(),Path,&Options):xrtFileOpen(Path,&Options);
        if(File){unsigned char* Bytes=xrtMalloc((size_t)Limit);size_t Read=0;xfileinfo Info={0};
            Ok=Bytes&&xrtFileStat(File,&Info)&&xrtReadAt(File,Offset,Bytes,(size_t)Limit,&Read);
            if(Ok){*R=SdkFileInfo(&Info);Ok=SdkByteValue(*R,Bytes,Read);SdkPutUInt(*R,"next",Offset+Read);}
            if(!xrtClose(File))Ok=false;
            xrtFree(Bytes);
        }
    }else if(!strcmp(Action,"write")||!strcmp(Action,"append")){
        cstr Revision=SdkField(P,"revision");
        if(Revision){xfileinfo Current={0};xvalue* Metadata=xrtPathStat(Path,false,&Current)?SdkFileInfo(&Current):NULL;
            bool Match=Metadata&&!strcmp(Revision,SdkField(Metadata,"revision"));xrtValueRelease(Metadata);
            if(!Match){xrtFree(Path);return MdoApplicationError(E,"sdk_file_conflict","File changed since it was read; reload before saving","filesystem",412,false);}}
        unsigned char* Bytes=NULL;size_t Size=0;
        if(!SdkInputBytes(P,&Bytes,&Size,E)){xrtFree(Path);return false;}
        bool Parents=true;xrtValueGetBool(SdkGet(P,"parents"),&Parents);char* Parent=xrtPathParent(Path);
        Ok=!Parents||(Parent&&xrtDirCreateAll(Parent));xrtFree(Parent);
        if(Ok)Ok=!strcmp(Action,"append")?xrtFileAppend(Path,(xbytesview){Bytes,Size}):xrtFileWriteAtomic(Path,(xbytesview){Bytes,Size});
        xrtSecureZero(Bytes,Size);xrtFree(Bytes);if(Ok){xfileinfo Current={0};*R=xrtPathStat(Path,false,&Current)?SdkFileInfo(&Current):xrtValueObject();SdkPutUInt(*R,"bytes",Size);}
    }else if(!strcmp(Action,"mkdir")){Ok=xrtDirCreateAll(Path);if(Ok)*R=xrtValueObject();}
    else if(!strcmp(Action,"remove")){
        bool Recursive=false;xrtValueGetBool(SdkGet(P,"recursive"),&Recursive);xfileinfo Info={0};
        Ok=xrtPathStat(Path,false,&Info)&&((Info.Type==XFILE_TYPE_DIRECTORY)?(Recursive?xrtDirRemoveAll(Path):xrtDirRemove(Path)):xrtFileDelete(Path));
        if(Ok)*R=xrtValueObject();
    }else if(!strcmp(Action,"copy")||!strcmp(Action,"move")){
        cstr Destination=SdkField(P,"destination");bool Replace=false;xrtValueGetBool(SdkGet(P,"replace"),&Replace);
        if(!Destination||!Destination[0])goto invalid;
        xvalue* Target=xrtValueClone(P);SdkPut(Target,"path",Destination);
        char* To=SdkNativePath(C,Target,Storage,E);xrtValueRelease(Target);
        Ok=To&&(!strcmp(Action,"copy")?xrtFileCopy(Path,To,Replace):xrtPathRename(Path,To,Replace));xrtFree(To);if(Ok)*R=xrtValueObject();
    }
    bool Mutation=!strcmp(Action,"write")||!strcmp(Action,"append")||!strcmp(Action,"remove")||!strcmp(Action,"mkdir")||!strcmp(Action,"copy")||!strcmp(Action,"move");
    xrtFree(Path);if(!Ok){if(!E||!E->Code[0])SdkNativeFailure(E,"filesystem");if(E&&Mutation)E->MayHaveCompleted=true;}return Ok;
invalid:xrtFree(Path);return MdoApplicationError(E,"sdk_arguments_invalid","Invalid path, range, limit or destination","filesystem",400,false);
}
static bool SdkProcessOutput(xprocessstream Stream,xbytesview B,void* User){
    const mdo_sdk_context_v1* C=User;xvalue* Data=xrtValueObject();SdkByteValue(Data,B.Data,B.Size);
    MdoApplicationEvent(Stream==XPROCESS_STDOUT?"process.stdout":"process.stderr",C,Data);xrtValueRelease(Data);return !SdkCancelled(C);
}
static bool SdkProcessCall(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Program=SdkField(P,"program"),Directory=SdkField(P,"cwd"),Input=SdkField(P,"input");const xvalue* Args=SdkGet(P,"args");
    if(!Program||!Program[0]||(Args&&xrtValueType(Args)!=XVALUE_ARRAY)||xrtValueCount(Args)>1024)
        return MdoApplicationError(E,"sdk_arguments_invalid","Supply a program and a string argv array","process",400,false);
    cstr* Argv=xrtCalloc(xrtValueCount(Args)+1,sizeof(*Argv));if(!Argv)return false;
    bool Valid=true;for(size_t i=0;i<xrtValueCount(Args);i++){Argv[i]=SdkText(xrtValueArrayGet(Args,i));if(!Argv[i])Valid=false;}
    if(!Valid){xrtFree(Argv);return MdoApplicationError(E,"sdk_arguments_invalid","Process argv must contain strings; empty strings are supported","process",400,false);}
    xprocessconfig Config;xrtProcessConfigInit(&Config);Config.Program=Program;Config.Args=Argv;Config.ArgCount=xrtValueCount(Args);
    Config.WorkDir=Directory?Directory:(C->Workspace&&C->Workspace[0]?C->Workspace:NULL);Config.HideWindow=true;Config.NewGroup=true;
    Config.Stdin.Mode=Input?XPROCESS_IO_PIPE:XPROCESS_IO_NULL;Config.Stdout.Mode=XPROCESS_IO_PIPE;Config.Stderr.Mode=XPROCESS_IO_PIPE;
    xprocessrunoptions Options;xrtProcessRunOptionsInit(&Options);MdoApplicationInvocation* I=C->Opaque;Options.Cancel=I?I->Cancel:NULL;
    Options.Timeout=C->Deadline?MdoDeadlineWaitMs(C->Deadline):XRT_WAIT_FOREVER;uint64 Timeout=0;
    if(SdkGet(P,"timeout_ms")&&(!SdkUInt(SdkGet(P,"timeout_ms"),&Timeout)||Timeout>INT64_MAX)){xrtFree(Argv);return MdoApplicationError(E,"sdk_arguments_invalid","Invalid process timeout","process",400,false);}
    if(Timeout&&(Options.Timeout<0||Options.Timeout>(int64)Timeout))Options.Timeout=(int64)Timeout;
    Options.Input=(xbytesview){(const unsigned char*)Input,Input?strlen(Input):0};Options.StdoutLimit=16384;Options.StderrLimit=16384;Options.Overflow=XPROCESS_OVERFLOW_KEEP_FIRST;
    bool Stream=false;xrtValueGetBool(SdkGet(P,"stream"),&Stream);if(Stream){Options.Output=SdkProcessOutput;Options.UserData=(void*)C;}
    xprocessresult Result={0};bool Ok=xrtProcessRun(&Config,&Options,&Result);xrtFree(Argv);
    if(Ok){*R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("exit_code"),xrtValueInt(Result.Status.Code));
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("signal"),xrtValueInt(Result.Status.Signal));
        xvalue* Out=xrtValueObject(),*Err=xrtValueObject();SdkByteValue(Out,Result.Stdout,Result.StdoutSize);SdkByteValue(Err,Result.Stderr,Result.StderrSize);
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("stdout"),Out);xrtValueObjectSetNew(*R,XRT_STR_LITERAL("stderr"),Err);
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("truncated"),xrtValueBool(Result.StdoutTruncated||Result.StderrTruncated));
        SdkPutUInt(*R,"wait_status",Result.Wait);
    }
    xwaitresult Wait=Result.Wait;xrtProcessResultUnit(&Result);
    if(Ok&&Wait!=XWAIT_OK)return MdoApplicationError(E,Wait==XWAIT_TIMEOUT?"sdk_process_timeout":"sdk_cancelled",Wait==XWAIT_TIMEOUT?"Process execution timed out; partial output is retained":"Process execution cancelled; partial output is retained","process",Wait==XWAIT_TIMEOUT?504:499,true);
    return Ok||SdkNativeFailure(E,"process");
}
static bool SdkNetworkCall(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Url=SdkField(P,"url"),Method=SdkField(P,"method"),Body=SdkField(P,"body");const xvalue* H=SdkGet(P,"headers");
    if(!Url||(H&&xrtValueType(H)!=XVALUE_OBJECT)||xrtValueCount(H)>64)return MdoApplicationError(E,"sdk_arguments_invalid","Supply a URL and string headers","network",400,false);
    XS_FetchHeader Headers[64];for(size_t i=0;i<xrtValueCount(H);i++){Headers[i].Name=SdkKey(H,i);Headers[i].Value=SdkField(H,Headers[i].Name);
        if(!Headers[i].Value)return MdoApplicationError(E,"sdk_arguments_invalid","Headers must contain strings","network",400,false);}
    XS_FetchRequest Request={0};Request.Size=sizeof(Request);Request.Version=3;Request.Url=Url;Request.Method=Method?Method:"GET";
    Request.Headers=Headers;Request.HeaderCount=xrtValueCount(H);Request.Body=Body;Request.BodySize=Body?strlen(Body):0;
    Request.Timeout=C->Deadline?MdoDeadlineRemainingUs(C->Deadline):30000000;Request.IdleTimeout=30000000;Request.MaxBodyBytes=256u*1024u;
    MdoApplicationInvocation* I=C->Opaque;Request.Cancel=I?I->Cancel:NULL;XS_FetchResponse Reply={0};Reply.Size=sizeof(Reply);Reply.Version=1;
    bool Ok=xsFetch(&Request,&Reply);if(Ok){*R=xrtValueObject();SdkPutUInt(*R,"status",Reply.Status);SdkPut(*R,"url",Reply.FinalUrl);SdkPut(*R,"content_type",Reply.ContentType);Ok=SdkByteValue(*R,Reply.Body,Reply.BodySize);}
    xsFetchResponseUnit(&Reply);if(!Ok){SdkNativeFailure(E,"network");if(E&&strcmp(Request.Method,"GET")&&strcmp(Request.Method,"HEAD"))E->MayHaveCompleted=true;}return Ok;
}
bool MdoApplicationRuntimeCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    *Found=false;for(size_t i=0;i<sizeof(SdkRuntimeMethods)/sizeof(*SdkRuntimeMethods);i++)if(!strcmp(Method,SdkRuntimeMethods[i].Id))*Found=true;
    if(!*Found)return true;
    if(P&&xrtValueType(P)!=XVALUE_OBJECT)return MdoApplicationError(E,"sdk_arguments_invalid","Runtime service parameters must be an object","resolve",400,false);
    if(!strncmp(Method,"fs.",3)||!strncmp(Method,"vfs.",4)||!strncmp(Method,"storage.",8))return SdkFileCall(C,Method,P,R,E);
    if(!strcmp(Method,"process.run"))return SdkProcessCall(C,P,R,E);
    if(!strcmp(Method,"network.request"))return SdkNetworkCall(C,P,R,E);
    if(!strncmp(Method,"secret.",7)){
        if(!strcmp(Method,"secret.store")){cstr Value=SdkField(P,"value");char Ref[MDO_SECRET_VAULT_REFERENCE_CAPACITY];
            if(!Value)return MdoApplicationError(E,"sdk_arguments_invalid","Credential value is required","secret",400,false);
            if(!MdoSecretStore(Value,Ref))return SdkNativeFailure(E,"secret");
            *R=xrtValueObject();SdkPut(*R,"reference",Ref);return true;}
        cstr Ref=SdkField(P,"reference");if(!Ref)return MdoApplicationError(E,"sdk_arguments_invalid","Credential reference is required","secret",400,false);
        if(!strcmp(Method,"secret.remove")){if(!MdoSecretDiscard(Ref))return SdkNativeFailure(E,"secret");*R=xrtValueObject();return true;}
        char* Value=NULL;if(!MdoSecretResolve(xrtStrView(Ref),4096,&Value))return SdkNativeFailure(E,"secret");*R=xrtValueObject();SdkPut(*R,"value",Value);MdoSecretRelease(&Value);return true;
    }
    if(!strcmp(Method,"system.open")){cstr Target=SdkField(P,"target");if(!Target||!xrtProcessOpen(Target))return SdkNativeFailure(E,"platform");*R=xrtValueObject();return true;}
    if(!strcmp(Method,"runtime.clock")){*R=xrtValueObject();SdkPutUInt(*R,"monotonic_us",MdoMonotonicUs());xrtValueObjectSetNew(*R,XRT_STR_LITERAL("unix_us"),xrtValueInt(MdoUtcUs()));return true;}
    if(!strcmp(Method,"runtime.sleep")){uint64 Ms=0;if(!SdkUInt(SdkGet(P,"milliseconds"),&Ms)||Ms>86400000)return MdoApplicationError(E,"sdk_arguments_invalid","Wait must be between 0 and 86400000 milliseconds","wait",400,false);
        uint64 End=MdoDeadlineAfterUs(Ms*1000);while(!MdoDeadlineExpired(End)){if(SdkCancelled(C))return MdoApplicationError(E,"sdk_cancelled","Wait cancelled","wait",499,false);xrtSleep(10);}*R=xrtValueObject();return true;}
    if(!strcmp(Method,"runtime.log")){cstr Message=SdkField(P,"message");if(!Message||strlen(Message)>16384)return MdoApplicationError(E,"sdk_arguments_invalid","A log message up to 16 KiB is required","log",400,false);
        fprintf(stderr,"[extension:%s] %s\n",C->ExtensionId?C->ExtensionId:"",Message);*R=xrtValueObject();return true;}
    if(!strncmp(Method,"runtime.env.",12)){cstr Name=SdkField(P,"name");if(!Name||!Name[0]||strchr(Name,'='))return MdoApplicationError(E,"sdk_arguments_invalid","Invalid environment variable name","environment",400,false);
        if(!strcmp(Method,"runtime.env.set")){cstr Value=SdkField(P,"value");if(!xrtEnvSet(Name,Value))return SdkNativeFailure(E,"environment");*R=xrtValueObject();return true;}
        char* Value=xrtEnvGet(Name);*R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("value"),Value?SdkString(Value):xrtValueNull());xrtFree(Value);return true;}
    *R=xrtValueObject();SdkPut(*R,"version",MDO_VERSION_TEXT);SdkPutUInt(*R,"sdk_version",MDO_SDK_VERSION);
    SdkPut(*R,"application_root",MdoHomeApplicationRoot());MdoHomeSnapshot Home={0};Home.Size=sizeof(Home);
    if(MdoHomeGetSnapshot(&Home))SdkPut(*R,"home",Home.Path);
    MdoPowerManagerStatus Power={0};Power.Size=sizeof(Power);if(MdoPowerManagerGetStatus(&Power))xrtValueObjectSetNew(*R,XRT_STR_LITERAL("power_available"),xrtValueBool(Power.Available));
    return true;
}
