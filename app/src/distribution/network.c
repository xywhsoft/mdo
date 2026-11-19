#include "../../include/mdo/download_network.h"
#include "../../include/mdo/models.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/secrets.h"
#include "../../include/mdo/time.h"
#include <ctype.h>
static xnetengine* MdoDownloadEngine;
static xmutex* MdoDownloadClientLock;
static struct { uint8 Key[32]; xhttpclient* Client; } MdoDownloadClients[4];
static size_t MdoDownloadClientCursor;
void MdoDownloadNetworkSetEngine(xnetengine* Engine){
    MdoDownloadEngine=Engine;
    if(Engine){
        if(!MdoDownloadClientLock)MdoDownloadClientLock=xrtMutexCreate();
        return;
    }
    for(size_t I=0;I<sizeof(MdoDownloadClients)/sizeof(*MdoDownloadClients);++I)
        xrtHttpClientDestroy(MdoDownloadClients[I].Client);
    xrtSecureZero(MdoDownloadClients,sizeof(MdoDownloadClients));
    MdoDownloadClientCursor=0;
    if(MdoDownloadClientLock)xrtMutexDestroy(MdoDownloadClientLock);
    MdoDownloadClientLock=NULL;
}
static bool DnBypass(cstr Patterns,cstr Url){
    cstr Host=strstr(Url,"://");if(!Host)return false;Host+=3;size_t Length=strcspn(Host,":/?#");
    while(*Patterns){while(*Patterns==' '||*Patterns==','||*Patterns==';'||*Patterns=='\n'||*Patterns=='\r')Patterns++;
        cstr Start=Patterns;while(*Patterns&&*Patterns!=','&&*Patterns!=';'&&*Patterns!='\n'&&*Patterns!='\r')Patterns++;
        size_t N=(size_t)(Patterns-Start);while(N&&Start[N-1]==' ')N--;bool Left=N&&Start[0]=='*';if(Left){Start++;N--;}bool Right=N&&Start[N-1]=='*';if(Right)N--;
        if(!N&&(Left||Right))return true;
        if(N>Length||(!Left&&!Right&&N!=Length))continue;
        for(size_t Offset=Left&&!Right?Length-N:0;Offset+N<=Length;Offset++){
            size_t I=0;while(I<N&&tolower((unsigned char)Start[I])==tolower((unsigned char)Host[Offset+I]))I++;
            if(I==N&&N)return true;
            if(!Left||!Right)break;
        }
    }return false;
}
typedef struct DnPolicy {MdoConfigTransportSettings Settings;XS_SystemProxyInfo System;char* Password;bool Direct,Socks;} DnPolicy;
static void DnUnit(DnPolicy* P){MdoSecretRelease(&P->Password);xrtSecureZero(P,sizeof(*P));}
static bool DnResolve(DnPolicy* P,cstr Url,int64 ProxyTimeoutMs){
    memset(P,0,sizeof(*P));P->Settings.Size=sizeof(P->Settings);if(!MdoConfigGetTransportSettings(&P->Settings))return false;
    if(DnBypass(P->Settings.ProxyBypass,Url)){P->Direct=true;return true;}
    if(!strcmp(P->Settings.ProxyKind,"system")){
        P->System.Size=sizeof(P->System);P->System.Version=XS_SYSTEM_PROXY_VERSION;
        if(ProxyTimeoutMs<=0||!xsSystemProxyResolve(Url,ProxyTimeoutMs,&P->System))return false;
        P->Direct=P->System.Kind==XS_SYSTEM_PROXY_DIRECT;P->Socks=P->System.Kind==XS_SYSTEM_PROXY_SOCKS5;
    }else {P->Direct=!strcmp(P->Settings.ProxyKind,"none");P->Socks=!strcmp(P->Settings.ProxyKind,"socks5");
        if(!P->Direct&&P->Settings.ProxySecretRef[0]&&!MdoSecretResolve(xrtStrView(P->Settings.ProxySecretRef),4096,&P->Password))return false;
    }return true;
}
static xhttpclient* DnCreateClient(xnetengine* Engine,const DnPolicy* P,uint64 Timeout,xllm_error* Error){
    xhttpclient* Client=NULL;xnetproxy* Proxy=NULL;xtlsverifier* Verifier=NULL;xx509store* Store=NULL;
    if(!Engine)goto done;
    xhttpclientconfig C;xrtHttpClientConfigInit(&C);C.Timeout=Timeout;C.IdleTimeout=UINT64_C(30000000);
    C.Redirect.MaxHops=0;C.Retry.MaxRetries=0;C.Decompress.Enabled=false;C.Decompress.MaxBody=UINT64_MAX;
    C.Pool.MaxConnections=16;C.Pool.MaxConnectionsPerOrigin=8;C.Pool.MaxIdle=8;
    C.Pool.MaxWaiting=32;C.Pool.MaxWaitingPerOrigin=16;
    C.Pool.MaxIdlePerOrigin=4;C.Pool.IdleTimeout=UINT64_C(15000000);
    if(P->Settings.CaPemPath[0]){Store=MdoModelsLoadCaStore(P->Settings.CaPemPath,Error);if(!Store)goto done;
        xtlsverifierconfig T;xrtTlsVerifierConfigInit(&T);T.Store=Store;Verifier=xrtTlsVerifierCreate(&T);if(!Verifier)goto done;C.TlsVerifier=Verifier;C.SystemTrust=false;}
    if(!P->Direct){bool System=!strcmp(P->Settings.ProxyKind,"system");cstr User=System?P->System.User:P->Settings.ProxyUser,Password=System?P->System.Password:P->Password?P->Password:"";
        xnetproxyconfig N;xrtNetProxyConfigInit(&N);N.Type=P->Socks?XNET_PROXY_SOCKS5:XNET_PROXY_HTTP_CONNECT;
        N.Host=xrtStrView(System?P->System.Host:P->Settings.ProxyHost);N.Port=System?P->System.Port:P->Settings.ProxyPort;
        N.Username=(xbytesview){(const uint8*)User,strlen(User)};N.Password=(xbytesview){(const uint8*)Password,strlen(Password)};
        Proxy=xrtNetProxyCreate(&N);if(!Proxy)goto done;C.Proxy=Proxy;
    }
    Client=xrtHttpClientCreate(Engine,&C);
    if(Client&&Proxy&&xrtHttpClientProxy(Client)!=Proxy){xrtHttpClientDestroy(Client);Client=NULL;}
done:
    xrtNetProxyRelease(Proxy);xrtTlsVerifierRelease(Verifier);xrtX509StoreFree(Store);return Client;
}
xhttpclient* MdoDownloadClientCreate(xnetengine* Engine,cstr Url,uint64 Timeout,xllm_error* Error){
    DnPolicy Policy={0};
    int64 ProxyTimeout=Timeout&&Timeout<5000000?MdoDurationMs(Timeout):5000;
    xhttpclient* Client=Engine&&DnResolve(&Policy,Url,ProxyTimeout)?DnCreateClient(Engine,&Policy,Timeout,Error):NULL;
    DnUnit(&Policy);
    return Client;
}
static bool DnClientKey(const DnPolicy* P,uint8 Key[32]){
    xsha256 Hash;xrtSha256Init(&Hash);
    if(P->Direct)return xrtSha256Update(&Hash,"direct",6)&&xrtSha256Final(&Hash,Key);
    bool System=!strcmp(P->Settings.ProxyKind,"system");
    uint16 Port=System?P->System.Port:P->Settings.ProxyPort;
    const cstr Parts[]={P->Socks?"socks5":"http-connect",System?P->System.Host:P->Settings.ProxyHost,
        System?P->System.User:P->Settings.ProxyUser,System?P->System.Password:P->Password?P->Password:""};
    bool Ok=xrtSha256Update(&Hash,&Port,sizeof(Port));
    for(size_t I=0;Ok&&I<sizeof(Parts)/sizeof(*Parts);++I)
        Ok=xrtSha256Update(&Hash,Parts[I],strlen(Parts[I])+1);
    if(Ok)Ok=xrtSha256Final(&Hash,Key);
    xrtSecureZero(&Hash,sizeof(Hash));
    return Ok;
}
static xhttpclient* DnClientRef(const DnPolicy* Policy,uint64 Timeout,xllm_error* Error){
    uint8 Key[32];
    /* Read a custom CA file afresh so replacing it can revoke old trust. */
    if(!MdoDownloadClientLock||Policy->Settings.CaPemPath[0]||!DnClientKey(Policy,Key))
        return DnCreateClient(MdoDownloadEngine,Policy,Timeout,Error);
    xhttpclient* Client=NULL,*Evicted=NULL;
    xrtMutexLock(MdoDownloadClientLock);
    for(size_t I=0;I<sizeof(MdoDownloadClients)/sizeof(*MdoDownloadClients);++I){
        if(MdoDownloadClients[I].Client&&!memcmp(Key,MdoDownloadClients[I].Key,sizeof(Key))){
            Client=xrtHttpClientRef(MdoDownloadClients[I].Client);break;
        }
    }
    if(!Client){
        Client=DnCreateClient(MdoDownloadEngine,Policy,Timeout,Error);
        if(Client){
            size_t Index=MdoDownloadClientCursor++%(sizeof(MdoDownloadClients)/sizeof(*MdoDownloadClients));
            Evicted=MdoDownloadClients[Index].Client;
            MdoDownloadClients[Index].Client=xrtHttpClientRef(Client);
            memcpy(MdoDownloadClients[Index].Key,Key,sizeof(Key));
        }
    }
    xrtMutexUnlock(MdoDownloadClientLock);
    xrtHttpClientDestroy(Evicted);
    xrtSecureZero(Key,sizeof(Key));
    return Client;
}
static bool DnPut(xvalue* V,cstr K,cstr S){return xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S)));}
bool MdoDownloadAriaOptions(xvalue* Options,cstr Url){
    /* WinTLS/AppleTLS ignore aria2's custom CA file. Keep one portable trust
     * policy by selecting the native verified transport whenever a custom CA
     * or SOCKS proxy is selected. No OS trust-store mutation is necessary. */
    DnPolicy P={0};bool Ok=DnResolve(&P,Url,5000)&&!P.Socks&&!P.Settings.CaPemPath[0];
    if(Ok&&P.Direct)Ok=DnPut(Options,"all-proxy","");
    else if(Ok){bool System=!strcmp(P.Settings.ProxyKind,"system");cstr Host=System?P.System.Host:P.Settings.ProxyHost;
        char* Proxy=xrtFormat(strchr(Host,':')?"http://[%s]:%u":"http://%s:%u",Host,(unsigned)(System?P.System.Port:P.Settings.ProxyPort));
        Ok=Proxy&&DnPut(Options,"all-proxy",Proxy)&&DnPut(Options,"all-proxy-user",System?P.System.User:P.Settings.ProxyUser)&&
            DnPut(Options,"all-proxy-passwd",System?P.System.Password:P.Password?P.Password:"");xrtFree(Proxy);
    }
    DnUnit(&P);return Ok;
}
bool MdoDownloadFetch(const XS_FetchRequest* R,XS_FetchResponse* Out){
    if(!R||!Out||!R->Url||!R->Method||!R->MaxBodyBytes||R->MaxBodyBytes>XS_FETCH_BODY_LIMIT)return false;
    uint64 Deadline=R->Timeout&&R->Timeout!=XHTTP_CLIENT_TIMEOUT_NONE?MdoDeadlineAfterUs(R->Timeout):MDO_DEADLINE_NEVER;
    int64 ProxyTimeout=R->Timeout&&R->Timeout<5000000?MdoDurationMs(R->Timeout):5000;
    DnPolicy Policy={0};
    if(!DnResolve(&Policy,R->Url,ProxyTimeout)){DnUnit(&Policy);return false;}
    xllm_error Error={0};
    xhttpclient* C=DnClientRef(&Policy,R->Timeout,&Error);
    DnUnit(&Policy);
    xhttprequest* Q=C?xrtHttpRequestCreate(xrtStrView(R->Method),xrtStrView(R->Url)):NULL;bool Ok=Q!=NULL;
    for(size_t I=0;Ok&&I<R->HeaderCount;I++)Ok=xrtHttpRequestSetHeader(Q,xrtStrView(R->Headers[I].Name),xrtStrView(R->Headers[I].Value));
    if(Ok&&R->BodySize)Ok=xrtHttpRequestSetBytes(Q,(xbytesview){R->Body,R->BodySize},(xstrview){0});
    xhttpcalloptions O;xrtHttpCallOptionsInit(&O);O.Cancel=R->Cancel;O.ResponseBodyLimit=R->MaxBodyBytes;
    O.Timeout=Deadline==MDO_DEADLINE_NEVER?R->Timeout:MdoDeadlineRemainingUs(Deadline);O.IdleTimeout=R->IdleTimeout;
    O.Retry.Mode=XHTTP_RETRY_DISABLED;
    if(Deadline!=MDO_DEADLINE_NEVER&&!O.Timeout){xrtHttpRequestDestroy(Q);xrtHttpClientDestroy(C);return false;}
    xhttpresult* Result=Ok?xrtHttpClientDoSync(C,Q,&O):NULL;
    /* Preserve the transport's typed cause across accessors and cleanup.
     * Querying a NULL result would replace an EOF/TLS error with ARGUMENT. */
    xerror* Failure=Result?NULL:xrtTakeError();
    const xhttpresponse* Response=Result?xrtHttpResultResponse(Result):NULL;Ok=Response!=NULL;
    if(Ok){xbytesview Body=xrtHttpResponseBody(Response);const xhttpfield* Type=xrtHttpResponseHeader(Response,XRT_STR_LITERAL("Content-Type"));
        Out->Status=xrtHttpResponseStatus(Response);Out->FinalUrl=xrtStrDup(R->Url);Out->ContentType=xrtStrDupN(Type?Type->Value.Data:"",Type?Type->Value.Size:0);
        Out->Body=xrtMalloc(Body.Size+1);Out->BodySize=Body.Size;Out->Version=XS_FETCH_RESPONSE_VERSION;
        Ok=Out->FinalUrl&&Out->ContentType&&Out->Body;if(Ok){memcpy(Out->Body,Body.Data,Body.Size);Out->Body[Body.Size]=0;}
    }
    xrtHttpResultDestroy(Result);xrtHttpRequestDestroy(Q);xrtHttpClientDestroy(C);
    if(Failure){xrtSetError(Failure);xrtErrorFree(Failure);}return Ok;
}
void MdoDownloadFetchUnit(XS_FetchResponse* R){if(!R)return;xrtFree(R->FinalUrl);xrtFree(R->ContentType);xrtFree(R->Body);memset(R,0,sizeof(*R));}
