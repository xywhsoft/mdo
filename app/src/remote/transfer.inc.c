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

/* Same verified TLS transport as device relay, bounded decoded chunks written
 * directly to Home. Never follows a redirect or buffers an entire executable. */
static bool MdoTransferAttempt(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel,MdoTransferProgress Progress,void* ProgressData,MdoTransferReport* Report,MdoNativeDownload* Task)
{
    MdoRemoteNet Net={0}; MdoRemoteSocket* Socket=NULL; xfuture* Future=NULL;
    xfile File=NULL; bool Ok=false; uint64 Total=0; xsha256 Sha; uint8 Digest[32]; char Hex[65]={0};
    cstr Code="metadata",Detail="Invalid download metadata";
    if(Report)memset(Report,0,sizeof(*Report));
    xhttpfield Fields[32]; xhttp1head Head; xhttp1limits Limits;
    xhttp1bodyplan Plan; xhttp1body Body; xhttp1bodylimits BodyLimits;
    /* Large optional programs/APKs can legitimately need more than five
     * minutes. Individual socket reads retain their short idle deadline. */
    char Request[4096],Validator[512]={0}; size_t Length; uint64 Until=MdoDeadlineAfterUs(UINT64_C(3600000000));
    if (!Size || Size>UINT64_C(536870912) || !Hash || strlen(Hash)!=64 || !Path || Path[0]!='/' ||
        strchr(Path,'\r') || strchr(Path,'\n')) goto done;
    xvalue* Saved=MdoTransferResumeRead(Relative);uint64 SavedSize=0;int64 SignedSize=0;const xvalue* SizeValue=xrtValueObjectGet(Saved,XRT_STR_LITERAL("size"));
    if(!xrtValueGetUInt(SizeValue,&SavedSize)&&xrtValueGetInt(SizeValue,&SignedSize)&&SignedSize>0)SavedSize=(uint64)SignedSize;
    bool Same=SavedSize==Size&&!strcmp(MdoTransferText(Saved,"sha256"),Hash)&&!strcmp(MdoTransferText(Saved,"path"),Path);
    cstr Etag=MdoTransferText(Saved,"etag");if(Same&&Etag[0]=='\"'&&strlen(Etag)<sizeof(Validator)&&!strchr(Etag,'\r')&&!strchr(Etag,'\n'))snprintf(Validator,sizeof(Validator),"%s",Etag);xrtValueRelease(Saved);
    File=MdoHomeOpenWrite(Relative,XFILE_READ|XFILE_WRITE|XFILE_CREATE);uint64 Existing=0;
    Code="write";Detail="Unable to open the partial download";if(!File||!xrtFileSize(File,&Existing))goto done;
    if(Existing>Size||(!Same&&Existing!=Size)){if(!xrtFileResize(File,0))goto done;Existing=0;}
    xrtSha256Init(&Sha);uint8 Prefix[65536];
    while(Total<Existing){size_t N=(size_t)(Existing-Total<sizeof(Prefix)?Existing-Total:sizeof(Prefix));if(!MdoDownloadsGate(Task,Cancel)||!xrtReadFull(File,Prefix,N,NULL)||!xrtSha256Update(&Sha,Prefix,N))goto done;Total+=N;}
    if(Existing==Size){
        if(!xrtSha256Final(&Sha,Digest))goto done;for(size_t I=0;I<32;I++)snprintf(Hex+I*2,3,"%02x",Digest[I]);
        if(!strcmp(Hex,Hash)){Ok=true;goto done;}
        if(!xrtFileResize(File,0)||!xrtSeek(File,0,XSEEK_START,NULL))goto done;Total=0;xrtSha256Init(&Sha);Hex[0]=0;
    }
    if(!MdoDownloadsProgress(Task,Total,"downloading")||!MdoDownloadsGate(Task,Cancel))goto done;
    Code="network";Detail="Unable to establish download connection";
    if(!MdoRemoteNetInit(&Net,Engine,NULL))goto done;
    Socket=xrtCalloc(1,sizeof(*Socket)); if(!Socket)goto done;
    Socket->Cancel=xrtCancelChild(Cancel); Socket->Capacity=65536; Socket->Input=xrtMalloc(Socket->Capacity);
    if(!Socket->Cancel || !Socket->Input)goto done;
    if(MDO_DISTRIBUTION_SECURE) {
        xtlsclientconfig Tls; xtlsdialconfig Dial;
        xrtTlsClientConfigInit(&Tls); Tls.Context=Net.Tls; Tls.Verifier=Net.Verifier;
        xrtTlsDialConfigInit(&Dial); Dial.Timeout=MdoDurationMs(MDO_REMOTE_OPEN_US);
        Future=xrtTlsDialAsync(Net.Engine,Net.Resolver,MDO_DISTRIBUTION_HOST,MDO_DISTRIBUTION_PORT,&Tls,&Dial,NULL,NULL);
        if(!MdoRemoteFuture(Future,Until,Socket->Cancel))goto done;
        Socket->Tls=xrtTlsStreamRef((xtlsstream*)xrtFutureValue(Future));
    } else {
        xnetdialconfig Dial; xrtNetDialConfigInit(&Dial);
        Future=xrtNetDialAsync(Net.Engine,Net.Resolver,MDO_DISTRIBUTION_HOST,MDO_DISTRIBUTION_PORT,&Dial,NULL,NULL);
        if(!MdoRemoteFuture(Future,Until,Socket->Cancel))goto done;
        Socket->Tcp=xrtNetStreamRef((xnetstream*)xrtFutureValue(Future));
    }
    xrtFutureDestroy(Future); Future=NULL;
    char Range[768]={0};if(Total)snprintf(Range,sizeof(Range),"Range: bytes=%llu-\r\n%s%s%s",(unsigned long long)Total,Validator[0]?"If-Range: ":"",Validator,Validator[0]?"\r\n":"");
    Length=(size_t)snprintf(Request,sizeof(Request),"GET %s HTTP/1.1\r\nHost: %s:%u\r\nAccept-Encoding: identity\r\n%sConnection: close\r\n\r\n",Path,MDO_DISTRIBUTION_HOST,(unsigned)MDO_DISTRIBUTION_PORT,Range);
    if(Length>=sizeof(Request)||!MdoRemoteRawSend(Socket,Request,Length,Until))goto done;
    Code="response";Detail="Invalid or incomplete HTTP download response";
    xrtHttp1LimitsInit(&Limits); Limits.MaxHead=16384; Limits.MaxFields=32;
    for(;;) {
        xrtHttp1HeadInit(&Head,Fields,32);
        xhttp1status State=xrtHttp1ResponseParse((xbytesview){(uint8*)Socket->Input,Socket->InputSize},&Head,&Limits,NULL);
        if(State==XHTTP1_READY)break;
        if(State!=XHTTP1_MORE || Socket->InputSize>=16384 || !MdoRemoteRead(Socket,Until))goto done;
    }
    char ResponseEtag[512]={0},ContentRange[160]={0};bool Encoded=false;
    for(size_t I=0;I<Head.FieldCount;I++){
        if(xrtStrCaseEqual(Fields[I].Name,XRT_STR_LITERAL("etag"))&&Fields[I].Value.Size<sizeof(ResponseEtag))snprintf(ResponseEtag,sizeof(ResponseEtag),"%.*s",(int)Fields[I].Value.Size,Fields[I].Value.Data);
        if(xrtStrCaseEqual(Fields[I].Name,XRT_STR_LITERAL("content-range"))&&Fields[I].Value.Size<sizeof(ContentRange))snprintf(ContentRange,sizeof(ContentRange),"%.*s",(int)Fields[I].Value.Size,Fields[I].Value.Data);
        if(xrtStrCaseEqual(Fields[I].Name,XRT_STR_LITERAL("content-encoding"))&&!xrtStrCaseEqual(Fields[I].Value,XRT_STR_LITERAL("identity")))Encoded=true;
    }
    if(Head.Status==206){
        unsigned long long Start=0,End=0,Full=0;int Used=0;
        Code="range";Detail="Server returned a mismatched partial response";
        if(sscanf(ContentRange,"bytes %llu-%llu/%llu%n",&Start,&End,&Full,&Used)!=3||ContentRange[Used]||Start!=Total||Full!=Size||End!=Size-1||
           (Validator[0]&&ResponseEtag[0]&&strcmp(Validator,ResponseEtag)))goto done;
    }else if(Head.Status==200){
        if(Total){Code="write";Detail="Unable to restart a server that ignored the range";if(!xrtFileResize(File,0)||!xrtSeek(File,0,XSEEK_START,NULL))goto done;Total=0;xrtSha256Init(&Sha);}
    }else {Code=Head.Status==416?"range":"http";Detail="Download server rejected the request";goto done;}
    if(Encoded||!xrtHttp1ResponseBodyPlan(&Head,XRT_STR_LITERAL("GET"),&Plan))goto done;
    if(!MdoTransferResumeSave(Relative,Path,Hash,Size,ResponseEtag))goto done;
    Socket->InputSize-=Head.Bytes; memmove(Socket->Input,Socket->Input+Head.Bytes,Socket->InputSize);
    xrtHttp1BodyLimitsInit(&BodyLimits); BodyLimits.MaxBody=Size-Total;
    if(!xrtHttp1BodyInit(&Body,&Plan,Fields,32,&BodyLimits))goto done;
    Code="write";Detail="Unable to create the downloaded file";
    if(Progress)Progress(Total,Size,ProgressData);
    for(;;) {
        if(!MdoDownloadsGate(Task,Cancel))goto done;
        size_t Used=0,Written=0; xbytesview Data={0};
        xhttp1bodystatus State=xrtHttp1BodyRead(&Body,(xbytesview){(uint8*)Socket->Input,Socket->InputSize},false,&Used,&Data,NULL);
        Code="response";Detail="Malformed or oversized HTTP download body";
        if(State==XHTTP1_BODY_ERROR || State==XHTTP1_BODY_FIELDS)goto done;
        if(State==XHTTP1_BODY_DATA) {
            Code="write";Detail="Unable to write the downloaded bytes";
            if(Total+Data.Size>Size || !xrtSha256Update(&Sha,Data.Data,Data.Size) ||
                !xrtWrite(File,Data.Data,Data.Size,&Written) || Written!=Data.Size)goto done;
            Total+=Data.Size;
            if(!MdoDownloadsProgress(Task,Total,"downloading"))goto done;
            if(Progress)Progress(Total,Size,ProgressData);
        }
        Socket->InputSize-=Used; memmove(Socket->Input,Socket->Input+Used,Socket->InputSize);
        if(State==XHTTP1_BODY_DONE)break;
        Code="download";Detail="Download interrupted or timed out";
        if(State==XHTTP1_BODY_MORE && !MdoRemoteRead(Socket,MdoDeadlineExpired(Until)?Until:MdoDeadlineAfterUs(MDO_REMOTE_OPEN_US)))goto done;
        if(xrtCancelRequested(Cancel)||MdoDeadlineExpired(Until))goto done;
    }
    Code="size";Detail="Downloaded size does not match the catalog";
    if(Total!=Size)goto done;
    Code="write";Detail="Unable to finalize the downloaded file";
    if(!MdoDownloadsGate(Task,Cancel)||!MdoDownloadsProgress(Task,Total,"verifying")||!xrtSha256Final(&Sha,Digest) || !xrtFlush(File))goto done;
    for(size_t i=0;i<32;i++)snprintf(Hex+i*2,3,"%02x",Digest[i]);
    Code="checksum";Detail="Downloaded SHA-256 does not match the catalog";
    Ok=!strcmp(Hex,Hash);
done:
    if(Report) {
        Report->Bytes=Total;snprintf(Report->ActualHash,sizeof(Report->ActualHash),"%s",Hex);
        if(!Ok){snprintf(Report->Code,sizeof(Report->Code),"%s",Code);snprintf(Report->Detail,sizeof(Report->Detail),"%s",Detail);}
    }
    if(File)(void)xrtFlush(File);xrtClose(File); xrtFutureDestroy(Future); MdoRemoteSocketDestroy(Socket); MdoRemoteNetUnit(&Net);
    if((Ok&&!Task)||!strcmp(Code,"checksum")||!strcmp(Code,"range")){char* Meta=MdoTransferResumePath(Relative);if(Meta)MdoHomeRemove(Meta,false);xrtFree(Meta);if(!Ok)MdoHomeRemove(Relative,false);}
    return Ok;
}
bool MdoTransferDownloadReport(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel,MdoTransferProgress Progress,void* ProgressData,MdoTransferReport* Report){
    xvalue* Context=xrtValueObject();bool Prepared=Context&&Path&&Relative&&xrtValueObjectSetNew(Context,XRT_STR_LITERAL("path"),xrtValueString(xrtStrView(Path)))&&
        xrtValueObjectSetNew(Context,XRT_STR_LITERAL("relative"),xrtValueString(xrtStrView(Relative)));
    cstr Name=Relative?strrchr(Relative,'/'):NULL;Name=Name?Name+1:Relative;
    char Error[512]={0};MdoNativeDownload* Task=Prepared?MdoDownloadsBegin(!strncmp(Relative,"data/update/",12)?"update":"transfer",Path,!strncmp(Relative,"data/update/",12)?"mdo":Name,Hash,Size,Context,Cancel,Error):NULL;
    xrtValueRelease(Context);MdoTransferReport Local={0};bool Ok=false;
    /* Legacy embedders without the task manager retain the synchronous API. */
    if(!Task&&Error[0]){if(Report){memset(Report,0,sizeof(*Report));snprintf(Report->Code,sizeof(Report->Code),"busy");snprintf(Report->Detail,sizeof(Report->Detail),"%.255s",Error);}return false;}
    char* Url=Path?xrtFormat("%s://%s:%u%s",MDO_DISTRIBUTION_SECURE?"https":"http",MDO_DISTRIBUTION_HOST,(unsigned)MDO_DISTRIBUTION_PORT,Path):NULL,*ArFile=NULL;
    MdoAriaResult Ar=Url?MdoAriaDownload(Task,Url,Size,Hash,NULL,MdoDownloadsCancelToken(Task,Cancel),&ArFile,&Local):MDO_ARIA_UNAVAILABLE;xrtFree(Url);
    if(Ar==MDO_ARIA_COMPLETE){
        xfile Parent=MdoDownloadsGate(Task,Cancel)?MdoHomeOpenWrite(Relative,XFILE_CREATE|XFILE_WRITE):NULL;bool PreparedTarget=Parent!=NULL;xrtClose(Parent);
        Ok=PreparedTarget&&MdoTransferResumeSave(Relative,Path,Hash,Size,"")&&MdoHomeRemove(Relative,false)&&MdoHomeRenameNoReplace(ArFile,Relative);
        if(!Ok){snprintf(Local.Code,sizeof(Local.Code),"write");snprintf(Local.Detail,sizeof(Local.Detail),"Cannot publish the verified aria2 download");}
        if(Ok&&Progress)Progress(Size,Size,ProgressData);
    }
    xrtFree(ArFile);
    for(unsigned Attempt=0;Ar!=MDO_ARIA_COMPLETE&&Ar!=MDO_ARIA_FAILED&&Attempt<3;Attempt++){
        if(!MdoDownloadsGate(Task,Cancel))break;
        Ok=MdoTransferAttempt(Engine,Path,Relative,Size,Hash,MdoDownloadsCancelToken(Task,Cancel),Progress,ProgressData,&Local,Task);if(Ok)break;
        if(strcmp(Local.Code,"network")&&strcmp(Local.Code,"download")&&strcmp(Local.Code,"range"))break;
        MdoDownloadsProgress(Task,Local.Bytes,"retrying");
        for(unsigned I=0;I<(Attempt+1)*10&&MdoDownloadsGate(Task,Cancel);I++)xrtSleep(100);
    }
    if(Report)*Report=Local;return MdoDownloadsEnd(Task,Ok,Local.Code,Local.Detail,Cancel);
}
bool MdoTransferDownloadProgress(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel,MdoTransferProgress Progress,void* Data)
{return MdoTransferDownloadReport(Engine,Path,Relative,Size,Hash,Cancel,Progress,Data,NULL);}
bool MdoTransferDownload(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel)
{return MdoTransferDownloadProgress(Engine,Path,Relative,Size,Hash,Cancel,NULL,NULL);}
