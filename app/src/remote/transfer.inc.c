#include "../../include/mdo/distribution.h"
#include "../../include/mdo/home.h"

/* Same verified TLS transport as device relay, bounded decoded chunks written
 * directly to Home. Never follows a redirect or buffers an entire executable. */
bool MdoTransferDownloadReport(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel,MdoTransferProgress Progress,void* ProgressData,MdoTransferReport* Report)
{
    MdoRemoteNet Net={0}; MdoRemoteSocket* Socket=NULL; xfuture* Future=NULL;
    xfile File=NULL; bool Ok=false; uint64 Total=0; xsha256 Sha; uint8 Digest[32]; char Hex[65]={0};
    cstr Code="metadata",Detail="Invalid download metadata";
    if(Report)memset(Report,0,sizeof(*Report));
    xhttpfield Fields[32]; xhttp1head Head; xhttp1limits Limits;
    xhttp1bodyplan Plan; xhttp1body Body; xhttp1bodylimits BodyLimits;
    char Request[4096]; size_t Length; xdeadline Until=xrtDeadlineAfter(UINT64_C(300000000));
    if (!Size || Size>UINT64_C(536870912) || !Hash || strlen(Hash)!=64 || !Path || Path[0]!='/' ||
        strchr(Path,'\r') || strchr(Path,'\n')) goto done;
    Code="network";Detail="Unable to establish download connection";
    if(!MdoRemoteNetInit(&Net,Engine,NULL))goto done;
    Socket=xrtCalloc(1,sizeof(*Socket)); if(!Socket)goto done;
    Socket->Cancel=xrtCancelChild(Cancel); Socket->Capacity=65536; Socket->Input=xrtMalloc(Socket->Capacity);
    if(!Socket->Cancel || !Socket->Input)goto done;
    if(MDO_DISTRIBUTION_SECURE) {
        xtlsclientconfig Tls; xtlsdialconfig Dial;
        xrtTlsClientConfigInit(&Tls); Tls.Context=Net.Tls; Tls.Verifier=Net.Verifier;
        xrtTlsDialConfigInit(&Dial); Dial.Timeout=MDO_REMOTE_OPEN_US;
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
    Length=(size_t)snprintf(Request,sizeof(Request),"GET %s HTTP/1.1\r\nHost: %s:%u\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",Path,MDO_DISTRIBUTION_HOST,(unsigned)MDO_DISTRIBUTION_PORT);
    if(Length>=sizeof(Request)||!MdoRemoteRawSend(Socket,Request,Length,Until))goto done;
    Code="response";Detail="Invalid or incomplete HTTP download response";
    xrtHttp1LimitsInit(&Limits); Limits.MaxHead=16384; Limits.MaxFields=32;
    for(;;) {
        xrtHttp1HeadInit(&Head,Fields,32);
        xhttp1status State=xrtHttp1ResponseParse((xbytesview){(uint8*)Socket->Input,Socket->InputSize},&Head,&Limits,NULL);
        if(State==XHTTP1_READY)break;
        if(State!=XHTTP1_MORE || Socket->InputSize>=16384 || !MdoRemoteRead(Socket,Until))goto done;
    }
    if(Head.Status!=200 || !xrtHttp1ResponseBodyPlan(&Head,XRT_STR_LITERAL("GET"),&Plan))goto done;
    Socket->InputSize-=Head.Bytes; memmove(Socket->Input,Socket->Input+Head.Bytes,Socket->InputSize);
    xrtHttp1BodyLimitsInit(&BodyLimits); BodyLimits.MaxBody=Size;
    if(!xrtHttp1BodyInit(&Body,&Plan,Fields,32,&BodyLimits))goto done;
    Code="write";Detail="Unable to create the downloaded file";
    File=MdoHomeOpenWrite(Relative,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);
    if(!File)goto done;
    xrtSha256Init(&Sha);
    if(Progress)Progress(0,Size,ProgressData);
    for(;;) {
        size_t Used=0,Written=0; xbytesview Data={0};
        xhttp1bodystatus State=xrtHttp1BodyRead(&Body,(xbytesview){(uint8*)Socket->Input,Socket->InputSize},false,&Used,&Data,NULL);
        Code="response";Detail="Malformed or oversized HTTP download body";
        if(State==XHTTP1_BODY_ERROR || State==XHTTP1_BODY_FIELDS)goto done;
        if(State==XHTTP1_BODY_DATA) {
            Code="write";Detail="Unable to write the downloaded bytes";
            if(Total+Data.Size>Size || !xrtSha256Update(&Sha,Data.Data,Data.Size) ||
                !xrtWrite(File,Data.Data,Data.Size,&Written) || Written!=Data.Size)goto done;
            Total+=Data.Size;
            if(Progress)Progress(Total,Size,ProgressData);
        }
        Socket->InputSize-=Used; memmove(Socket->Input,Socket->Input+Used,Socket->InputSize);
        if(State==XHTTP1_BODY_DONE)break;
        Code="download";Detail="Download interrupted or timed out";
        if(State==XHTTP1_BODY_MORE && !MdoRemoteRead(Socket,xrtDeadlineExpired(Until)?Until:xrtDeadlineAfter(MDO_REMOTE_OPEN_US)))goto done;
        if(xrtCancelRequested(Cancel)||xrtDeadlineExpired(Until))goto done;
    }
    Code="size";Detail="Downloaded size does not match the catalog";
    if(Total!=Size)goto done;
    Code="write";Detail="Unable to finalize the downloaded file";
    if(!xrtSha256Final(&Sha,Digest) || !xrtFlush(File))goto done;
    for(size_t i=0;i<32;i++)snprintf(Hex+i*2,3,"%02x",Digest[i]);
    Code="checksum";Detail="Downloaded SHA-256 does not match the catalog";
    Ok=!strcmp(Hex,Hash);
done:
    if(Report) {
        Report->Bytes=Total;snprintf(Report->ActualHash,sizeof(Report->ActualHash),"%s",Hex);
        if(!Ok){snprintf(Report->Code,sizeof(Report->Code),"%s",Code);snprintf(Report->Detail,sizeof(Report->Detail),"%s",Detail);}
    }
    xrtClose(File); xrtFutureDestroy(Future); MdoRemoteSocketDestroy(Socket); MdoRemoteNetUnit(&Net);
    if(!Ok&&(!Report||xrtCancelRequested(Cancel)))MdoHomeRemove(Relative,false);
    return Ok;
}
bool MdoTransferDownloadProgress(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel,MdoTransferProgress Progress,void* Data)
{return MdoTransferDownloadReport(Engine,Path,Relative,Size,Hash,Cancel,Progress,Data,NULL);}
bool MdoTransferDownload(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel)
{return MdoTransferDownloadProgress(Engine,Path,Relative,Size,Hash,Cancel,NULL,NULL);}
