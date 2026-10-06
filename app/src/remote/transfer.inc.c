#include "../../include/mdo/distribution.h"
#include "../../include/mdo/home.h"

/* Same verified TLS transport as device relay, bounded decoded chunks written
 * directly to Home. Never follows a redirect or buffers an entire executable. */
bool MdoTransferDownloadProgress(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel,MdoTransferProgress Progress,void* ProgressData)
{
    MdoRemoteNet Net={0}; MdoRemoteSocket* Socket=NULL; xfuture* Future=NULL;
    xfile File=NULL; bool Ok=false; uint64 Total=0; xsha256 Sha; uint8 Digest[32]; char Hex[65];
    xhttpfield Fields[32]; xhttp1head Head; xhttp1limits Limits;
    xhttp1bodyplan Plan; xhttp1body Body; xhttp1bodylimits BodyLimits;
    char Request[4096]; size_t Length; xdeadline Until=xrtDeadlineAfter(UINT64_C(300000000));
    if (!Size || Size>UINT64_C(536870912) || !Hash || strlen(Hash)!=64 || !Path || Path[0]!='/' ||
        strchr(Path,'\r') || strchr(Path,'\n') || !MdoRemoteNetInit(&Net,Engine,NULL)) goto done;
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
    File=MdoHomeOpenWrite(Relative,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);
    if(!File)goto done;
    xrtSha256Init(&Sha);
    if(Progress)Progress(0,Size,ProgressData);
    for(;;) {
        size_t Used=0,Written=0; xbytesview Data={0};
        xhttp1bodystatus State=xrtHttp1BodyRead(&Body,(xbytesview){(uint8*)Socket->Input,Socket->InputSize},false,&Used,&Data,NULL);
        if(State==XHTTP1_BODY_ERROR || State==XHTTP1_BODY_FIELDS)goto done;
        if(State==XHTTP1_BODY_DATA) {
            if(Total+Data.Size>Size || !xrtSha256Update(&Sha,Data.Data,Data.Size) ||
                !xrtWrite(File,Data.Data,Data.Size,&Written) || Written!=Data.Size)goto done;
            Total+=Data.Size;
            if(Progress)Progress(Total,Size,ProgressData);
        }
        Socket->InputSize-=Used; memmove(Socket->Input,Socket->Input+Used,Socket->InputSize);
        if(State==XHTTP1_BODY_DONE)break;
        if(State==XHTTP1_BODY_MORE && !MdoRemoteRead(Socket,xrtDeadlineExpired(Until)?Until:xrtDeadlineAfter(MDO_REMOTE_OPEN_US)))goto done;
        if(xrtCancelRequested(Cancel)||xrtDeadlineExpired(Until))goto done;
    }
    if(Total!=Size || !xrtSha256Final(&Sha,Digest) || !xrtFlush(File))goto done;
    for(size_t i=0;i<32;i++)snprintf(Hex+i*2,3,"%02x",Digest[i]);
    Ok=!strcmp(Hex,Hash);
done:
    xrtClose(File); xrtFutureDestroy(Future); MdoRemoteSocketDestroy(Socket); MdoRemoteNetUnit(&Net);
    if(!Ok)MdoHomeRemove(Relative,false);
    return Ok;
}
bool MdoTransferDownload(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel)
{return MdoTransferDownloadProgress(Engine,Path,Relative,Size,Hash,Cancel,NULL,NULL);}
