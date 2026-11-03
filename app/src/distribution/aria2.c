#include "../../include/mdo/aria2.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/time.h"
#if defined(_WIN32)
__declspec(dllimport) unsigned long __stdcall GetCurrentProcessId(void);
#else
#include <unistd.h>
#endif

/* Each active producer owns its child, RPC secret and download directory.
 * No global aria2 service, user configuration, PATH lookup or saved tokens. */
typedef struct MdoAria {
    xprocess* Process;MdoCliUse* Use;MdoExtensionLease* Lease;
    char Rpc[96],Secret[72],Gid[17];
} MdoAria;
static bool ArPut(xvalue* V,cstr K,cstr S){return xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S?S:"")));}
static cstr ArText(const xvalue* V,cstr K){cstr S=MdoExtensionText(V,K,4096);return S?S:"";}
static bool ArDecimal(cstr S,uint64* Out){
    uint64 N=0;if(!S||!*S)return false;for(;*S;S++){if(*S<'0'||*S>'9'||N>(UINT64_MAX-(unsigned)(*S-'0'))/10)return false;N=N*10+(unsigned)(*S-'0');}*Out=N;return true;
}
/* Supply the already pinned catalogue size through an in-memory Metalink.
 * This lets aria2 split even when the store streams with chunked framing. */
static char* ArMetalink(cstr Url,uint64 Bytes){
    size_t N=strlen(Url);if(N>4096)return NULL;char* Escaped=xrtMalloc(N*5+1);if(!Escaped)return NULL;size_t At=0;
    for(size_t I=0;I<N;I++){cstr S=Url[I]=='&'?"&amp;":Url[I]=='<'?"&lt;":Url[I]=='>'?"&gt;":NULL;if(S){size_t L=strlen(S);memcpy(Escaped+At,S,L);At+=L;}else Escaped[At++]=Url[I];}Escaped[At]=0;
    char* Xml=xrtFormat("<?xml version=\"1.0\" encoding=\"UTF-8\"?><metalink xmlns=\"urn:ietf:params:xml:ns:metalink\"><file name=\"payload\"><size>%llu</size><url>%s</url></file></metalink>",(unsigned long long)Bytes,Escaped);xrtFree(Escaped);
    char* Encoded=Xml?xrtBase64EncodeNew(Xml,strlen(Xml),NULL):NULL;xrtFree(Xml);return Encoded;
}
static bool ArError(MdoTransferReport* R,cstr Code,cstr Detail){if(R){snprintf(R->Code,sizeof(R->Code),"%s",Code);snprintf(R->Detail,sizeof(R->Detail),"%s",Detail);}return false;}
static char* ArPath(cstr Id,cstr Name){return Id&&strlen(Id)==32&&MdoExtensionDirectoryId(Id)?xrtFormat("data/downloads/aria2/%s/%s",Id,Name):NULL;}
bool MdoAriaCleanup(cstr Id){
    if(!Id||strlen(Id)!=32||!MdoExtensionDirectoryId(Id))return false;
    const cstr Files[]={"payload","payload.aria2","owner.json"};bool Ok=true;
    for(size_t I=0;I<3;I++){char* P=ArPath(Id,Files[I]);Ok=P&&MdoHomeRemove(P,false)&&Ok;xrtFree(P);}
    char* Dir=xrtFormat("data/downloads/aria2/%s",Id);bool Exists=false;xfileinfo Info;
    if(Dir&&MdoHomeExternalStat(Dir,&Exists,&Info)&&Exists)Ok=MdoHomeRemoveEmptyDirectory(Dir)&&Ok;
    xrtFree(Dir);return Ok;
}
static xvalue* ArRpc(MdoAria* A,cstr Method,xvalue* Params,xcancel* Cancel){
    xvalue* Q=xrtValueObject(),*P=xrtValueArray();char* Token=xrtFormat("token:%s",A->Secret);
    bool Ok=Q&&P&&Token&&xrtValueArrayAppendNew(P,xrtValueString(xrtStrView(Token)));
    for(size_t I=0;Ok&&I<xrtValueCount(Params);I++)Ok=xrtValueArrayAppend(P,xrtValueArrayGet(Params,I));
    Ok=Ok&&ArPut(Q,"jsonrpc","2.0")&&ArPut(Q,"id","mdo")&&ArPut(Q,"method",Method)&&xrtValueObjectSet(Q,XRT_STR_LITERAL("params"),P);
    size_t N=0;char* Body=Ok?xrtJsonStringify(Q,false,&N):NULL;XS_FetchHeader Header={"Content-Type","application/json"};
    XS_FetchRequest Req={0};XS_FetchResponse Res={0};Req.Size=sizeof(Req);Req.Version=XS_FETCH_REQUEST_VERSION;Req.Url=A->Rpc;Req.Method="POST";Req.Body=Body;Req.BodySize=N;
    Req.Headers=&Header;Req.HeaderCount=1;Req.MaxBodyBytes=65536;Req.Timeout=1500000;Req.IdleTimeout=1000000;Req.Cancel=Cancel;Res.Size=sizeof(Res);
    Ok=Body&&xsFetch(&Req,&Res)&&Res.Status==200&&!Res.Redirects;
    xvalue* Envelope=Ok?xrtJsonParse(xrtStrViewN((cstr)Res.Body,Res.BodySize)):NULL;
    xvalue* Result=Envelope&&!xrtValueObjectHas(Envelope,XRT_STR_LITERAL("error"))?xrtValueClone(xrtValueObjectGet(Envelope,XRT_STR_LITERAL("result"))):NULL;
    if(Body)xrtSecureZero(Body,N);if(Token)xrtSecureZero(Token,strlen(Token));
    xrtFree(Body);xrtFree(Token);xrtValueRelease(Envelope);xsFetchResponseUnit(&Res);xrtValueRelease(Q);xrtValueRelease(P);return Result;
}
static xvalue* ArGid(MdoAria* A,cstr Method,xcancel* Cancel){xvalue* P=xrtValueArray();if(P)xrtValueArrayAppendNew(P,xrtValueString(xrtStrView(A->Gid)));xvalue* R=P?ArRpc(A,Method,P,Cancel):NULL;xrtValueRelease(P);return R;}
static void ArStop(MdoAria* A){
    if(A->Process){
        xvalue* R=A->Gid[0]?ArGid(A,"aria2.forcePause",NULL):NULL;xrtValueRelease(R);
        R=ArRpc(A,"aria2.shutdown",NULL,NULL);xrtValueRelease(R);
        if(xrtProcessWaitFor(A->Process,2000)!=XWAIT_OK){xrtProcessKillTree(A->Process);xrtProcessWait(A->Process);}
        xrtProcessDestroy(A->Process);
    }
    MdoCliUseEnd(A->Use);MdoExtensionsRelease(A->Lease);xrtSecureZero(A,sizeof(*A));
}
static bool ArStart(MdoAria* A,xcancel* Cancel){
    char Why[512]={0};xvalue* Meta=MdoExtensionsResolve("external-tool:mdo.aria2",Why);cstr Id=MdoExtensionText(Meta,"directory_id",96);
    A->Lease=Id?MdoExtensionsAcquire("external-tools",Id,Why):NULL;xrtValueRelease(Meta);
    char* Program=A->Lease&&MdoCliUseBegin(&A->Use,Why)?MdoCliProgram("external-tool:mdo.aria2","aria2c",Why):NULL;
    if(!Program)return false;
    xnetaddr Addr={0};xnetsocket Socket=xrtNetSocketOpen(XNET_FAMILY_IPV4,XNET_SOCKET_STREAM,0);
    bool Ok=Socket&&xrtNetAddrLoopback(&Addr,XNET_FAMILY_IPV4,0)&&xrtNetSocketBind(Socket,&Addr)&&xrtNetSocketLocal(Socket,&Addr);xrtNetSocketClose(Socket);
    char *Secret=xrtXidMakeString(),Port[48],SecretArg[100],Parent[72];
    Ok=Ok&&Secret;if(Ok){snprintf(A->Secret,sizeof(A->Secret),"%s",Secret);snprintf(A->Rpc,sizeof(A->Rpc),"http://127.0.0.1:%u/jsonrpc",(unsigned)Addr.Port);}xrtFree(Secret);
    snprintf(Port,sizeof(Port),"--rpc-listen-port=%u",(unsigned)Addr.Port);snprintf(SecretArg,sizeof(SecretArg),"--rpc-secret=%s",A->Secret);
#if defined(_WIN32)
    unsigned long Pid=GetCurrentProcessId();
#else
    unsigned long Pid=(unsigned long)getpid();
#endif
    snprintf(Parent,sizeof(Parent),"--stop-with-process=%lu",Pid);
    const cstr Args[]={"--no-conf=true","--no-netrc=true","--enable-rpc=true","--rpc-listen-all=false","--rpc-allow-origin-all=false",Port,SecretArg,Parent,
        "--enable-dht=false","--enable-dht6=false","--enable-peer-exchange=false","--bt-enable-lpd=false","--rpc-save-upload-metadata=false","--quiet=true"};
    xprocessconfig Config;xrtProcessConfigInit(&Config);Config.Program=Program;Config.Args=Args;Config.ArgCount=sizeof(Args)/sizeof(*Args);Config.HideWindow=true;Config.NewGroup=true;
    Config.Stdin.Mode=Config.Stdout.Mode=Config.Stderr.Mode=XPROCESS_IO_NULL;
    A->Process=Ok?xrtProcessSpawn(&Config):NULL;xrtFree(Program);xrtSecureZero(SecretArg,sizeof(SecretArg));
    if(!A->Process)return false;
    for(unsigned I=0;I<30&&!(Cancel&&xrtCancelRequested(Cancel))&&xrtProcessState(A->Process)==XPROCESS_RUNNING;I++){
        xvalue* R=ArRpc(A,"aria2.getSessionInfo",NULL,Cancel);bool Ready=R&&ArText(R,"sessionId")[0];xrtValueRelease(R);if(Ready)return true;xrtSleep(50);
    }return false;
}
static bool ArOwn(cstr Id,cstr Url,cstr Hash,uint64 Bytes){
    char* Path=ArPath(Id,"owner.json");xfile F=Path?MdoHomeOpenRead(Path):NULL;char Text[4096];uint64 N=0;
    bool Read=F&&xrtFileSize(F,&N)&&N<sizeof(Text)&&xrtReadFull(F,Text,(size_t)N,NULL);xrtClose(F);
    xvalue* Old=Read?xrtJsonParse(xrtStrViewN(Text,(size_t)N)):NULL;uint64 Size=0;int64 Signed=0;const xvalue* V=xrtValueObjectGet(Old,XRT_STR_LITERAL("bytes"));
    if(!xrtValueGetUInt(V,&Size)&&xrtValueGetInt(V,&Signed)&&Signed>0)Size=(uint64)Signed;
    bool Same=Size==Bytes&&!strcmp(ArText(Old,"sha256"),Hash)&&!strcmp(ArText(Old,"url"),Url);xrtValueRelease(Old);
    bool Ok=Same||MdoAriaCleanup(Id);xvalue* Record=xrtValueObject();Ok=Ok&&Record&&ArPut(Record,"url",Url)&&ArPut(Record,"sha256",Hash)&&xrtValueObjectSetNew(Record,XRT_STR_LITERAL("bytes"),xrtValueUInt(Bytes));
    char* Json=Ok?xrtJsonStringify(Record,false,NULL):NULL;Ok=Json&&MdoHomeAtomicWrite(Path,Json,strlen(Json),false);xrtFree(Json);xrtFree(Path);xrtValueRelease(Record);return Ok;
}
static bool ArVerify(MdoNativeDownload* T,cstr Path,uint64 Bytes,cstr Hash,xcancel* Cancel,MdoTransferReport* Report){
    xfile F=MdoHomeOpenRead(Path);uint64 Size=0;bool Ok=F&&xrtFileSize(F,&Size)&&Size==Bytes;uint8 Buffer[65536],Digest[32];char Hex[65]={0};xsha256 Sha;xrtSha256Init(&Sha);
    ArError(Report,"checksum","Downloaded bytes do not match the expected size or SHA-256");
    for(uint64 At=0;Ok&&At<Bytes;){size_t N=(size_t)(Bytes-At<sizeof(Buffer)?Bytes-At:sizeof(Buffer));Ok=MdoDownloadsGate(T,Cancel)&&xrtReadFull(F,Buffer,N,NULL)&&xrtSha256Update(&Sha,Buffer,N);At+=N;}
    Ok=Ok&&xrtSha256Final(&Sha,Digest);xrtClose(F);if(Ok){for(size_t I=0;I<32;I++)snprintf(Hex+I*2,3,"%02x",Digest[I]);Ok=!strcmp(Hex,Hash);}
    if(Ok&&Report){memset(Report,0,sizeof(*Report));Report->Bytes=Bytes;snprintf(Report->ActualHash,sizeof(Report->ActualHash),"%s",Hex);}return Ok;
}
MdoAriaResult MdoAriaDownload(MdoNativeDownload* T,cstr Url,uint64 Bytes,cstr Hash,cstr Authorization,xcancel* Cancel,char** Relative,MdoTransferReport* Report){
    if(Relative)*Relative=NULL;if(Report)memset(Report,0,sizeof(*Report));char Id[33];
    if(!Relative||!T||!MdoDownloadsIdentity(T,Id))return MDO_ARIA_UNAVAILABLE;
    MdoAria A={0};MdoAriaResult Result=MDO_ARIA_UNAVAILABLE;char* Path=ArPath(Id,"payload"),*Native=Path?MdoHomeExternalPath(Path):NULL;
    if(!Url||(!strncmp(Url,"https://",8)?false:strncmp(Url,"http://",7)!=0)||strchr(Url,'\r')||strchr(Url,'\n')||!Hash||strlen(Hash)!=64||!Bytes||!Path||!Native)goto done;
    if(!ArStart(&A,Cancel)){Result=A.Lease?MDO_ARIA_FALLBACK:MDO_ARIA_UNAVAILABLE;goto done;}
    Result=MDO_ARIA_FAILED;
    if(!MdoDownloadsEngine(T,"aria2","")||!ArOwn(Id,Url,Hash,Bytes)){ArError(Report,"write","Cannot prepare aria2 task storage");goto done;}
    char* Dir=xrtStrDup(Native);char* Slash=Dir?strrchr(Dir,'/'):NULL;char* Back=Dir?strrchr(Dir,'\\'):NULL;if(Back&&(!Slash||Back>Slash))Slash=Back;if(!Slash){xrtFree(Dir);goto done;}*Slash=0;
    xvalue *P=xrtValueArray(),*Options=xrtValueObject();char* Metalink=ArMetalink(Url,Bytes);
    bool Ok=P&&Options&&Metalink&&xrtValueArrayAppendNew(P,xrtValueString(xrtStrView(Metalink)))&&
        ArPut(Options,"dir",Dir)&&ArPut(Options,"out","payload")&&ArPut(Options,"continue","true")&&ArPut(Options,"auto-file-renaming","false")&&ArPut(Options,"allow-overwrite","true")&&
        ArPut(Options,"file-allocation","none")&&ArPut(Options,"auto-save-interval","1")&&ArPut(Options,"split","4")&&ArPut(Options,"max-connection-per-server","4")&&
        ArPut(Options,"min-split-size","1M")&&ArPut(Options,"max-tries","3")&&ArPut(Options,"retry-wait","1")&&ArPut(Options,"connect-timeout","15")&&ArPut(Options,"timeout","30")&&
        ArPut(Options,"max-redirect","0")&&ArPut(Options,"check-certificate","true")&&ArPut(Options,"follow-torrent","false")&&ArPut(Options,"follow-metalink","false")&&ArPut(Options,"all-proxy","");
    if(Ok&&Authorization&&Authorization[0]){xvalue* H=xrtValueArray();Ok=!strchr(Authorization,'\r')&&!strchr(Authorization,'\n')&&strlen(Authorization)<=4200&&H&&xrtValueArrayAppendNew(H,xrtValueString(xrtStrView(Authorization)))&&xrtValueObjectSet(Options,XRT_STR_LITERAL("header"),H);xrtValueRelease(H);}
    Ok=Ok&&xrtValueArrayAppend(P,Options);xvalue* Reply=Ok?ArRpc(&A,"aria2.addMetalink",P,Cancel):NULL;xstrview Gid={0};Ok=xrtValueCount(Reply)==1&&xrtValueGetString(xrtValueArrayGet(Reply,0),&Gid)&&Gid.Size==16&&!memchr(Gid.Data,0,16);
    if(Ok)snprintf(A.Gid,sizeof(A.Gid),"%s",Gid.Data);xrtValueRelease(Reply);xrtValueRelease(P);xrtValueRelease(Options);xrtFree(Metalink);xrtFree(Dir);
    if(!Ok){Result=MDO_ARIA_FALLBACK;goto done;}
    bool WasPaused=false;
    for(;;){
        bool Paused=false;if(!MdoDownloadsControl(T,&Paused,Cancel))break;
        if(xrtProcessState(A.Process)!=XPROCESS_RUNNING){Result=MDO_ARIA_FALLBACK;break;}
        if(Paused!=WasPaused){Reply=ArGid(&A,Paused?"aria2.pause":"aria2.unpause",Cancel);if(!Reply){Result=MDO_ARIA_FALLBACK;break;}xrtValueRelease(Reply);WasPaused=Paused;}
        Reply=ArGid(&A,"aria2.tellStatus",Cancel);if(!Reply){Result=MDO_ARIA_FALLBACK;break;}
        cstr State=ArText(Reply,"status");uint64 Done=0,Total=0;bool Valid=ArDecimal(ArText(Reply,"completedLength"),&Done)&&ArDecimal(ArText(Reply,"totalLength"),&Total)&&Done<=Bytes&&(!Total||Total==Bytes);
        bool Complete=!strcmp(State,"complete"),Failed=!strcmp(State,"error")||!strcmp(State,"removed");uint64 Code=0;ArDecimal(ArText(Reply,"errorCode"),&Code);
        xrtValueRelease(Reply);
        if(!Valid){ArError(Report,"size","Download size differs from the selected resource");break;}
        if(Failed){
            if(Code==1||Code==2||Code==5||Code==6||Code==7||Code==8||Code==19||Code==22||Code==28||Code==29)Result=MDO_ARIA_FALLBACK;
            else ArError(Report,Code==9||Code==13||Code==14||Code==15||Code==16||Code==17||Code==18?"write":Code==32?"checksum":"http","aria2 rejected the download; check resource access, disk space and certificate trust");
            break;
        }
        if(!MdoDownloadsProgress(T,Done,"downloading")){ArError(Report,"write","Cannot persist download progress");break;}
        if(Complete){
            if(!MdoDownloadsGate(T,Cancel)||!MdoDownloadsProgress(T,Bytes,"verifying"))break;
            if(ArVerify(T,Path,Bytes,Hash,Cancel,Report)){Result=MDO_ARIA_COMPLETE;*Relative=xrtStrDup(Path);if(!*Relative)Result=MDO_ARIA_FAILED;}
            break;
        }
        for(unsigned I=0;I<5&&MdoDownloadsControl(T,NULL,Cancel);I++)xrtSleep(50);
    }
done:
    ArStop(&A);xrtFree(Path);xrtFree(Native);
    bool Running=MdoDownloadsControl(T,NULL,Cancel);
    if(!Running)return MDO_ARIA_FAILED;
    if(Result==MDO_ARIA_FAILED&&Report&&!Report->Code[0])ArError(Report,"download","aria2 download could not complete");
    if(Result==MDO_ARIA_FALLBACK||Result==MDO_ARIA_UNAVAILABLE){
        if(!MdoAriaCleanup(Id)){ArError(Report,"write","Cannot remove the stopped aria2 partial file");return MDO_ARIA_FAILED;}
        if(!MdoDownloadsEngine(T,"native",Result==MDO_ARIA_FALLBACK?"aria2_failed":""))return MDO_ARIA_FAILED;
        if(Report)memset(Report,0,sizeof(*Report));
    }else if(Result==MDO_ARIA_FAILED&&Report&&(!strcmp(Report->Code,"checksum")||!strcmp(Report->Code,"size")))MdoAriaCleanup(Id);
    return Result;
}
