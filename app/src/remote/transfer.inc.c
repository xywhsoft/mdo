#include "../../include/mdo/download_network.h"
#include "../../include/mdo/update.h"
#include "../../include/mdo/time.h"
#include "../../include/mdo/distribution.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/downloads.h"
#include "../../include/mdo/aria2.h"

static char* MdoTransferResumePath(cstr Relative){return xrtFormat("%s.resume.json",Relative);}
static xvalue* MdoTransferResumeRead(cstr Relative){
    char* Path=MdoTransferResumePath(Relative);xfile F=Path?MdoHomeOpenRead(Path):NULL;xrtFree(Path);uint64 Size=0;
    char Text[8192];bool Ok=F&&xrtFileSize(F,&Size)&&Size&&Size<sizeof(Text)&&xrtReadFull(F,Text,(size_t)Size,NULL);xrtClose(F);
    return Ok?xrtJsonParse(xrtStrViewN(Text,(size_t)Size)):NULL;
}
static cstr MdoTransferText(const xvalue* V,cstr Key){xstrview S={0};return xrtValueGetString(xrtValueObjectGet(V,xrtStrView(Key)),&S)&&!memchr(S.Data,0,S.Size)?S.Data:"";}
static bool MdoTransferResumeSave(cstr Relative,cstr Path,cstr Hash,uint64 Size,cstr Etag){
    xvalue* V=xrtValueObject();bool Ok=V&&xrtValueObjectSetNew(V,XRT_STR_LITERAL("path"),xrtValueString(xrtStrView(Path)))&&
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("sha256"),xrtValueString(xrtStrView(Hash)))&&
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("size"),xrtValueUInt(Size))&&xrtValueObjectSetNew(V,XRT_STR_LITERAL("etag"),xrtValueString(xrtStrView(Etag)));
    char* Text=Ok?xrtJsonStringify(V,false,NULL):NULL,*Meta=MdoTransferResumePath(Relative);Ok=Text&&Meta&&MdoHomeAtomicWrite(Meta,Text,strlen(Text),false);
    xrtValueRelease(V);xrtFree(Text);xrtFree(Meta);return Ok;
}

/* The HTTP SDK owns TLS/proxy framing. Body callbacks only write bounded
 * chunks; pause never blocks a shared network worker. The producer thread
 * resumes/cancels its own call and publishes durable progress. */
typedef struct MdoTransferStream {
    xmutex* Lock;xfile File;xsha256 Sha;uint64 Total,Size;bool Done,Success,Headers;
    cstr Path,Relative,Hash,Code,Detail;char Validator[512];char* Redirect;MdoNativeDownload* Task;xcancel* Cancel;
} MdoTransferStream;
static bool MdoTransferHeaders(xhttpcall* Call,const xhttpresponse* Response,void* Data){
    (void)Call;MdoTransferStream* S=Data;bool Ok=true;uint16 Status=xrtHttpResponseStatus(Response);
    const xhttpfield* Etag=xrtHttpResponseHeader(Response,XRT_STR_LITERAL("etag"));
    const xhttpfield* Range=xrtHttpResponseHeader(Response,XRT_STR_LITERAL("content-range"));
    const xhttpfield* Encoding=xrtHttpResponseHeader(Response,XRT_STR_LITERAL("content-encoding"));char Validator[512]={0},Text[160]={0};
    if(Etag&&Etag->Value.Size<sizeof(Validator))snprintf(Validator,sizeof(Validator),"%.*s",(int)Etag->Value.Size,Etag->Value.Data);
    if(Range&&Range->Value.Size<sizeof(Text))snprintf(Text,sizeof(Text),"%.*s",(int)Range->Value.Size,Range->Value.Data);
    xrtMutexLock(S->Lock);S->Code="response";S->Detail="Invalid download response";
    if(Encoding&&!xrtStrCaseEqual(Encoding->Value,XRT_STR_LITERAL("identity")))Ok=false;
    if(Status==206){unsigned long long Start=0,End=0,Full=0;int Used=0;S->Code="range";S->Detail="Server returned a mismatched partial response";
        Ok=Ok&&sscanf(Text,"bytes %llu-%llu/%llu%n",&Start,&End,&Full,&Used)==3&&!Text[Used]&&Start==S->Total&&Full==S->Size&&End==S->Size-1&&
            (!S->Validator[0]||!Validator[0]||!strcmp(S->Validator,Validator));
    }else if(Status==200){if(S->Total){S->Code="write";S->Detail="Unable to restart a server that ignored the range";
        Ok=Ok&&xrtFileResize(S->File,0)&&xrtSeek(S->File,0,XSEEK_START,NULL);if(Ok){S->Total=0;xrtSha256Init(&S->Sha);}}
    }else {if(Status>=300&&Status<400&&S->Redirect){const xhttpfield* Location=xrtHttpResponseHeader(Response,XRT_STR_LITERAL("location"));if(Location&&Location->Value.Size<=2048)snprintf(S->Redirect,2049,"%.*s",(int)Location->Value.Size,Location->Value.Data);}
        Ok=false;S->Code=S->Redirect&&S->Redirect[0]?"redirect":Status==416?"range":"http";S->Detail="Download server rejected the request or returned a redirect";}
    if(Ok){S->Code="write";S->Detail="Unable to persist download ownership";Ok=MdoTransferResumeSave(S->Relative,S->Path,S->Hash,S->Size,Validator);}
    S->Headers=Ok;if(Ok){S->Code="download";S->Detail="Download connection interrupted; received bytes were retained";}
    xrtMutexUnlock(S->Lock);return Ok;
}
static bool MdoTransferBody(xhttpcall* Call,const xhttpresponse* Response,xbytesview Bytes,void* Data){
    (void)Response;MdoTransferStream* S=Data;bool Paused=false;if(!MdoDownloadsControl(S->Task,&Paused,S->Cancel))return false;
    xrtMutexLock(S->Lock);bool Ok=S->Headers&&Bytes.Size<=S->Size-S->Total&&xrtSha256Update(&S->Sha,Bytes.Data,Bytes.Size)&&xrtWriteFull(S->File,Bytes.Data,Bytes.Size,NULL);
    if(Ok)S->Total+=Bytes.Size;else {S->Code="write";S->Detail="Unable to write the downloaded bytes, or the response exceeded the expected size";}
    xrtMutexUnlock(S->Lock);if(Ok&&Paused)(void)xrtHttpCallPause(Call);return Ok;
}
static void MdoTransferDone(xhttpcall* Call,const xhttpcallresult* Result,void* Data){
    (void)Call;MdoTransferStream* S=Data;
    xrtHttpResponseDestroy(Result->Response);xrtNetStreamDestroy(Result->Tcp);xrtTlsStreamDestroy(Result->Tls);
    xrtMutexLock(S->Lock);
    for(const xerror* E=Result->Error;E;E=xrtErrorCause(E))if(xrtErrorDomain(E)&&!strcmp(xrtErrorDomain(E),"xrt.tls")&&
        (xrtErrorCode(E)==XTLS_ERROR_VERIFY||xrtErrorCode(E)==XTLS_ERROR_CERTIFICATE||xrtErrorCode(E)==XTLS_ERROR_IDENTITY)){
        S->Code="certificate";S->Detail="Download TLS certificate verification failed; check custom CA settings";break;}
    S->Success=Result->Result==XNET_RESULT_OK;S->Done=true;xrtMutexUnlock(S->Lock);
}
static bool MdoTransferAttempt(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel,MdoTransferProgress Progress,void* ProgressData,MdoTransferReport* Report,MdoNativeDownload* Task,cstr EffectiveUrl,char Redirect[2049]){
    MdoTransferStream S={0};S.Path=Path;S.Relative=Relative;S.Hash=Hash;S.Size=Size;S.Task=Task;S.Cancel=Cancel;S.Code="metadata";S.Redirect=Redirect;if(Redirect)Redirect[0]=0;S.Detail="Invalid download metadata";
    xhttpclient* Client=NULL;xhttprequest* Request=NULL;xhttpcall* Call=NULL;char* Url=NULL;bool Ok=false;uint8 Digest[32],Buffer[65536];char Hex[65]={0};
    if(Report)memset(Report,0,sizeof(*Report));
    if(!Size||Size>UINT64_C(1099511627776)||!Hash||strlen(Hash)!=64||!Path||(Path[0]!='/'&&!MdoHttpUrlValid(xrtStrView(Path),false))||strchr(Path,'\r')||strchr(Path,'\n'))goto done;
    S.Lock=xrtMutexCreate();S.File=MdoHomeOpenWrite(Relative,XFILE_READ|XFILE_WRITE|XFILE_CREATE);uint64 Existing=0;S.Code="write";S.Detail="Unable to open the partial download";
    if(!S.Lock||!S.File||!xrtFileSize(S.File,&Existing))goto done;
    xvalue* Saved=MdoTransferResumeRead(Relative);uint64 SavedSize=0;int64 SignedSize=0;const xvalue* Value=xrtValueObjectGet(Saved,XRT_STR_LITERAL("size"));
    if(!xrtValueGetUInt(Value,&SavedSize)&&xrtValueGetInt(Value,&SignedSize)&&SignedSize>0)SavedSize=(uint64)SignedSize;
    bool Same=SavedSize==Size&&!strcmp(MdoTransferText(Saved,"sha256"),Hash)&&!strcmp(MdoTransferText(Saved,"path"),Path);cstr Etag=MdoTransferText(Saved,"etag");
    if(Same&&Etag[0]=='"'&&strlen(Etag)<sizeof(S.Validator)&&!strchr(Etag,'\r')&&!strchr(Etag,'\n'))snprintf(S.Validator,sizeof(S.Validator),"%s",Etag);xrtValueRelease(Saved);
    if(Existing>Size||(!Same&&Existing!=Size)){if(!xrtFileResize(S.File,0))goto done;Existing=0;}
    xrtSha256Init(&S.Sha);
    while(S.Total<Existing){size_t N=(size_t)(Existing-S.Total<sizeof(Buffer)?Existing-S.Total:sizeof(Buffer));if(!MdoDownloadsGate(Task,Cancel)||!xrtReadFull(S.File,Buffer,N,NULL)||!xrtSha256Update(&S.Sha,Buffer,N))goto done;S.Total+=N;}
    if(Existing==Size){if(!xrtSha256Final(&S.Sha,Digest))goto done;for(size_t I=0;I<32;I++)snprintf(Hex+I*2,3,"%02x",Digest[I]);if(!strcmp(Hex,Hash)){Ok=true;goto done;}
        if(!xrtFileResize(S.File,0)||!xrtSeek(S.File,0,XSEEK_START,NULL))goto done;S.Total=0;xrtSha256Init(&S.Sha);Hex[0]=0;}
    char* Parent=MdoHomeExternalPath(Relative);if(Parent){char* Slash=strrchr(Parent,'/'),*Back=strrchr(Parent,'\\');if(Back&&(!Slash||Back>Slash))Slash=Back;if(Slash)*Slash=0;}
    bool Space=Parent&&MdoHomeDiskSpace(Parent,Size-S.Total);xrtFree(Parent);if(!Space){S.Code="space";S.Detail="Insufficient free disk space or unable to inspect the download volume";goto done;}
    if(!MdoDownloadsGate(Task,Cancel))goto done;
    Url=EffectiveUrl?xrtStrDup(EffectiveUrl):xrtFormat("%s://%s:%u%s",MDO_DISTRIBUTION_SECURE?"https":"http",MDO_DISTRIBUTION_HOST,(unsigned)MDO_DISTRIBUTION_PORT,Path);
    xllm_error Error={0};S.Code="network";S.Detail="Unable to establish download connection; check proxy and certificate settings";
    Client=Url?MdoDownloadClientCreate(Engine,Url,UINT64_C(86400000000),&Error):NULL;Request=Client?xrtHttpRequestCreate(XRT_STR_LITERAL("GET"),xrtStrView(Url)):NULL;
    if(!Client){S.Code="network_config";S.Detail="Unable to initialize download proxy or certificate settings";goto done;}
    bool Prepared=Request&&xrtHttpRequestSetHeader(Request,XRT_STR_LITERAL("Accept-Encoding"),XRT_STR_LITERAL("identity"));
    if(Prepared&&S.Total){char Range[80];snprintf(Range,sizeof(Range),"bytes=%llu-",(unsigned long long)S.Total);Prepared=xrtHttpRequestSetHeader(Request,XRT_STR_LITERAL("Range"),xrtStrView(Range));
        if(Prepared&&S.Validator[0])Prepared=xrtHttpRequestSetHeader(Request,XRT_STR_LITERAL("If-Range"),xrtStrView(S.Validator));}
    xhttpcalloptions Options;xrtHttpCallOptionsInit(&Options);Options.Cancel=Cancel;Options.ResponseBodyLimit=Size;Options.Events.Headers=MdoTransferHeaders;Options.Events.Body=MdoTransferBody;Options.Events.Data=&S;
    Call=Prepared?xrtHttpClientDo(Client,Request,&Options,MdoTransferDone,&S):NULL;if(!Call)goto done;
    for(;;){xrtMutexLock(S.Lock);bool Done=S.Done,Success=S.Success;uint64 Total=S.Total;xrtMutexUnlock(S.Lock);if(Done){Ok=Success;break;}
        bool Paused=false;if(!MdoDownloadsControl(Task,&Paused,Cancel)||!MdoDownloadsProgress(Task,Total,"downloading"))(void)xrtHttpCallCancel(Call);
        else if(!Paused&&xrtHttpCallPaused(Call))(void)xrtHttpCallResume(Call);
        if(Progress)Progress(Total,Size,ProgressData);xrtSleep(25);
    }
    if(!Ok)goto done;S.Code="checksum";S.Detail="Downloaded bytes do not match the expected size or SHA-256";
    Ok=S.Total==Size&&MdoDownloadsGate(Task,Cancel)&&MdoDownloadsProgress(Task,S.Total,"verifying")&&xrtFlush(S.File)&&xrtSha256Final(&S.Sha,Digest);
    if(Ok){for(size_t I=0;I<32;I++)snprintf(Hex+I*2,3,"%02x",Digest[I]);Ok=!strcmp(Hex,Hash);}if(Ok&&Progress)Progress(Size,Size,ProgressData);
done:
    if(Report){Report->Bytes=S.Total;snprintf(Report->ActualHash,sizeof(Report->ActualHash),"%s",Hex);if(!Ok){snprintf(Report->Code,sizeof(Report->Code),"%s",S.Code);snprintf(Report->Detail,sizeof(Report->Detail),"%s",S.Detail);}}
    xrtHttpCallDestroy(Call);xrtHttpRequestDestroy(Request);xrtHttpClientDestroy(Client);if(S.File)(void)xrtFlush(S.File);xrtClose(S.File);xrtMutexDestroy(S.Lock);xrtFree(Url);
    if((Ok&&!Task)||!strcmp(S.Code,"checksum")||!strcmp(S.Code,"range")){char* Meta=MdoTransferResumePath(Relative);if(Meta)MdoHomeRemove(Meta,false);xrtFree(Meta);if(!Ok)MdoHomeRemove(Relative,false);}
    return Ok;
}
static bool MdoTransferRedirectAllowed(cstr Initial,cstr Next,const xvalue* Hosts){
    cstr A=strstr(Initial,"://"),B=strstr(Next,"://");if(!A||!B)return false;A+=3;B+=3;
    size_t AN=A[0]=='['?(size_t)(strchr(A,']')-A)+1:strcspn(A,":/?"),BN=B[0]=='['?(size_t)(strchr(B,']')-B)+1:strcspn(B,":/?");
    if(AN==BN&&MdoHttpUrlAsciiEqual(A,B,AN))return true;
    for(size_t I=0;I<xrtValueCount(Hosts);I++){xstrview H={0};if(xrtValueGetString(xrtValueArrayGet(Hosts,I),&H)&&H.Size==BN&&MdoHttpUrlAsciiEqual(H.Data,B,BN))return true;}
    return false;
}
static bool MdoTransferDownloadInternal(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,const xvalue* Hosts,xcancel* Cancel,MdoTransferProgress Progress,void* ProgressData,MdoTransferReport* Report){
    if(!Path||!Relative||!Size||Size>UINT64_C(1099511627776)){if(Report){memset(Report,0,sizeof(*Report));snprintf(Report->Code,sizeof(Report->Code),"metadata");}return false;}
    xvalue* Context=xrtValueObject();bool Prepared=Context&&Path&&Relative&&xrtValueObjectSetNew(Context,XRT_STR_LITERAL("path"),xrtValueString(xrtStrView(Path)))&&
        xrtValueObjectSetNew(Context,XRT_STR_LITERAL("relative"),xrtValueString(xrtStrView(Relative)));
    if(Prepared&&Hosts)Prepared=xrtValueObjectSet(Context,XRT_STR_LITERAL("redirect_hosts"),Hosts);
    if(Prepared&&!strncmp(Relative,"data/update/",12)){MdoUpdateStatus Update={0};
        if(MdoUpdateGetStatus(&Update)&&Hash&&!strcmp(Update.Hash,Hash))Prepared=xrtValueObjectSetNew(Context,XRT_STR_LITERAL("build_id"),xrtValueUInt(Update.BuildId));}
    cstr Name=Relative?strrchr(Relative,'/'):NULL;Name=Name?Name+1:Relative;
    char Error[512]={0};MdoNativeDownload* Task=Prepared?MdoDownloadsBegin(!strncmp(Relative,"data/update/",12)?"update":"transfer",Path,!strncmp(Relative,"data/update/",12)?"mdo":Name,Hash,Size,Context,Cancel,Error):NULL;
    xrtValueRelease(Context);MdoTransferReport Local={0};bool Ok=false;
    /* Legacy embedders without the task manager retain the synchronous API. */
    if(!Task&&Error[0]){if(Report){memset(Report,0,sizeof(*Report));snprintf(Report->Code,sizeof(Report->Code),"busy");snprintf(Report->Detail,sizeof(Report->Detail),"%.255s",Error);}return false;}
    char* Url=Path[0]!='/'?xrtStrDup(Path):xrtFormat("%s://%s:%u%s",MDO_DISTRIBUTION_SECURE?"https":"http",MDO_DISTRIBUTION_HOST,(unsigned)MDO_DISTRIBUTION_PORT,Path),*ArFile=NULL;
    MdoAriaResult Ar=Url?MdoAriaDownload(Task,Url,Size,Hash,NULL,MdoDownloadsCancelToken(Task,Cancel),&ArFile,&Local):MDO_ARIA_UNAVAILABLE;
    if(Ar==MDO_ARIA_COMPLETE){
        xfile Parent=MdoDownloadsGate(Task,Cancel)?MdoHomeOpenWrite(Relative,XFILE_CREATE|XFILE_WRITE):NULL;bool PreparedTarget=Parent!=NULL;xrtClose(Parent);
        Ok=PreparedTarget&&MdoTransferResumeSave(Relative,Path,Hash,Size,"")&&MdoHomeRemove(Relative,false)&&MdoHomeRenameNoReplace(ArFile,Relative);
        if(!Ok){snprintf(Local.Code,sizeof(Local.Code),"write");snprintf(Local.Detail,sizeof(Local.Detail),"Cannot publish the verified aria2 download");}
        if(Ok&&Progress)Progress(Size,Size,ProgressData);
    }
    xrtFree(ArFile);
    for(unsigned Attempt=0;Ar!=MDO_ARIA_COMPLETE&&Ar!=MDO_ARIA_FAILED&&Attempt<3;Attempt++){
        if(!MdoDownloadsGate(Task,Cancel))break;
        char* Effective=xrtStrDup(Url);char Redirect[2049]={0};
        for(unsigned Hop=0;Effective&&Hop<=5;Hop++){
            Ok=MdoTransferAttempt(Engine,Path,Relative,Size,Hash,MdoDownloadsCancelToken(Task,Cancel),Progress,ProgressData,&Local,Task,Effective,Redirect);
            if(Ok||strcmp(Local.Code,"redirect")||!Hosts||Hop==5)break;
            char* Next=NULL;
            if(Redirect[0]=='/'&&Redirect[1]!='/'){cstr Authority=strstr(Effective,"://");size_t End=Authority?(size_t)(Authority+3-Effective)+strcspn(Authority+3,"/?"):0;Next=End?xrtFormat("%.*s%s",(int)End,Effective,Redirect):NULL;}
            else Next=xrtStrDup(Redirect);
            bool Allowed=Next&&MdoHttpUrlValid(xrtStrView(Next),false)&&MdoTransferRedirectAllowed(Url,Next,Hosts);
            xrtFree(Effective);Effective=Allowed?Next:NULL;if(!Allowed)xrtFree(Next);
        }
        xrtFree(Effective);if(Ok)break;
        if(strcmp(Local.Code,"network")&&strcmp(Local.Code,"download")&&strcmp(Local.Code,"range"))break;
        MdoDownloadsProgress(Task,Local.Bytes,"retrying");
        for(unsigned I=0;I<(Attempt+1)*10&&MdoDownloadsGate(Task,Cancel);I++)xrtSleep(100);
    }
    xrtFree(Url);if(Report)*Report=Local;return MdoDownloadsEnd(Task,Ok,Local.Code,Local.Detail,Cancel);
}
bool MdoTransferDownloadReport(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel,MdoTransferProgress Progress,void* Data,MdoTransferReport* Report){
    if(!Path||Path[0]!='/')return false;
    return MdoTransferDownloadInternal(Engine,Path,Relative,Size,Hash,NULL,Cancel,Progress,Data,Report);
}
bool MdoTransferDownloadSource(xnetengine* Engine,const MdoDownloadSource* Source,xcancel* Cancel,MdoTransferProgress Progress,void* Data,MdoTransferReport* Report){
    if(Report)memset(Report,0,sizeof(*Report));
    if(!Source||!Source->Url||strlen(Source->Url)>2048||!MdoHttpUrlValid(xrtStrView(Source->Url),false)||!Source->Relative||strncmp(Source->Relative,"data/downloads/assets/",22)||Source->RedirectHostCount>8||
        (Source->RedirectHostCount&&!Source->RedirectHosts)){if(Report)snprintf(Report->Code,sizeof(Report->Code),"metadata");return false;}
    xvalue* Hosts=xrtValueArray();bool Ok=Hosts!=NULL;
    for(size_t I=0;Ok&&I<Source->RedirectHostCount;I++)Ok=Source->RedirectHosts[I]&&xrtValueArrayAppendNew(Hosts,xrtValueString(xrtStrView(Source->RedirectHosts[I])));
    Ok=Ok&&MdoTransferDownloadInternal(Engine,Source->Url,Source->Relative,Source->Bytes,Source->Sha256,Hosts,Cancel,Progress,Data,Report);xrtValueRelease(Hosts);return Ok;
}
bool MdoTransferDownloadProgress(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel,MdoTransferProgress Progress,void* Data)
{return MdoTransferDownloadReport(Engine,Path,Relative,Size,Hash,Cancel,Progress,Data,NULL);}
bool MdoTransferDownload(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel)
{return MdoTransferDownloadProgress(Engine,Path,Relative,Size,Hash,Cancel,NULL,NULL);}
