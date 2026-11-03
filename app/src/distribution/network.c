#include "../../include/mdo/download_network.h"
#include "../../include/mdo/models.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/secrets.h"
#include "../../include/mdo/time.h"
#include <ctype.h>
static xnetengine* MdoDownloadEngine;
void MdoDownloadNetworkSetEngine(xnetengine* Engine){MdoDownloadEngine=Engine;}
static bool DnBypass(cstr Patterns,cstr Url){
    cstr Host=strstr(Url,"://");if(!Host)return false;Host+=3;size_t Length=strcspn(Host,":/?#");
    while(*Patterns){while(*Patterns==' '||*Patterns==','||*Patterns==';'||*Patterns=='\n'||*Patterns=='\r')Patterns++;
        cstr Start=Patterns;while(*Patterns&&*Patterns!=','&&*Patterns!=';'&&*Patterns!='\n'&&*Patterns!='\r')Patterns++;
        size_t N=(size_t)(Patterns-Start);while(N&&Start[N-1]==' ')N--;bool Left=N&&Start[0]=='*';if(Left){Start++;N--;}bool Right=N&&Start[N-1]=='*';if(Right)N--;
        if(!N&&(Left||Right))return true;if(N>Length||(!Left&&!Right&&N!=Length))continue;
        for(size_t Offset=Left&&!Right?Length-N:0;Offset+N<=Length;Offset++){
            size_t I=0;while(I<N&&tolower((unsigned char)Start[I])==tolower((unsigned char)Host[Offset+I]))I++;
            if(I==N&&N)return true;if(!Left||!Right)break;
        }
    }return false;
}
typedef struct DnPolicy {MdoConfigTransportSettings Settings;XS_SystemProxyInfo System;char* Password;bool Direct,Socks;} DnPolicy;
static void DnUnit(DnPolicy* P){MdoSecretRelease(&P->Password);xrtSecureZero(P,sizeof(*P));}
static bool DnResolve(DnPolicy* P,cstr Url){
    memset(P,0,sizeof(*P));P->Settings.Size=sizeof(P->Settings);if(!MdoConfigGetTransportSettings(&P->Settings))return false;
    if(DnBypass(P->Settings.ProxyBypass,Url)){P->Direct=true;return true;}
    if(!strcmp(P->Settings.ProxyKind,"system")){
        P->System.Size=sizeof(P->System);P->System.Version=XS_SYSTEM_PROXY_VERSION;if(!xsSystemProxyResolve(Url,5000,&P->System))return false;
        P->Direct=P->System.Kind==XS_SYSTEM_PROXY_DIRECT;P->Socks=P->System.Kind==XS_SYSTEM_PROXY_SOCKS5;
    }else {P->Direct=!strcmp(P->Settings.ProxyKind,"none");P->Socks=!strcmp(P->Settings.ProxyKind,"socks5");
        if(!P->Direct&&P->Settings.ProxySecretRef[0]&&!MdoSecretResolve(xrtStrView(P->Settings.ProxySecretRef),4096,&P->Password))return false;
    }return true;
}
xhttpclient* MdoDownloadClientCreate(xnetengine* Engine,cstr Url,uint64 Timeout,xllm_error* Error){
    DnPolicy P={0};xhttpclient* Client=NULL;xnetproxy* Proxy=NULL;xtlsverifier* Verifier=NULL;xx509store* Store=NULL;
    if(!Engine||!DnResolve(&P,Url))goto done;
    xhttpclientconfig C;xrtHttpClientConfigInit(&C);C.Timeout=Timeout;C.IdleTimeout=UINT64_C(30000000);
    C.Redirect.MaxHops=0;C.Retry.MaxRetries=0;C.Decompress.Enabled=false;C.Decompress.MaxBody=UINT64_MAX;
    if(P.Settings.CaPemPath[0]){Store=MdoModelsLoadCaStore(P.Settings.CaPemPath,Error);if(!Store)goto done;
        xtlsverifierconfig T;xrtTlsVerifierConfigInit(&T);T.Store=Store;Verifier=xrtTlsVerifierCreate(&T);if(!Verifier)goto done;C.TlsVerifier=Verifier;C.SystemTrust=false;}
    if(!P.Direct){bool System=!strcmp(P.Settings.ProxyKind,"system");cstr User=System?P.System.User:P.Settings.ProxyUser,Password=System?P.System.Password:P.Password?P.Password:"";
        xnetproxyconfig N;xrtNetProxyConfigInit(&N);N.Type=P.Socks?XNET_PROXY_SOCKS5:XNET_PROXY_HTTP_CONNECT;
        N.Host=xrtStrView(System?P.System.Host:P.Settings.ProxyHost);N.Port=System?P.System.Port:P.Settings.ProxyPort;
        N.Username=(xbytesview){(const uint8*)User,strlen(User)};N.Password=(xbytesview){(const uint8*)Password,strlen(Password)};
        Proxy=xrtNetProxyCreate(&N);if(!Proxy)goto done;C.Proxy=Proxy;
    }
    Client=xrtHttpClientCreate(Engine,&C);
    if(Client&&Proxy&&xrtHttpClientProxy(Client)!=Proxy){xrtHttpClientDestroy(Client);Client=NULL;}
done:
    xrtNetProxyRelease(Proxy);xrtTlsVerifierRelease(Verifier);xrtX509StoreFree(Store);DnUnit(&P);return Client;
}
static bool DnPut(xvalue* V,cstr K,cstr S){return xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S)));}
bool MdoDownloadAriaOptions(xvalue* Options,cstr Url){
    /* WinTLS/AppleTLS ignore aria2's custom CA file. Keep one portable trust
     * policy by selecting the native verified transport whenever a custom CA
     * or SOCKS proxy is selected. No OS trust-store mutation is necessary. */
    DnPolicy P={0};bool Ok=DnResolve(&P,Url)&&!P.Socks&&!P.Settings.CaPemPath[0];
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
    xllm_error Error={0};xhttpclient* C=MdoDownloadClientCreate(MdoDownloadEngine,R->Url,R->Timeout,&Error);
    xhttprequest* Q=C?xrtHttpRequestCreate(xrtStrView(R->Method),xrtStrView(R->Url)):NULL;bool Ok=Q!=NULL;
    for(size_t I=0;Ok&&I<R->HeaderCount;I++)Ok=xrtHttpRequestSetHeader(Q,xrtStrView(R->Headers[I].Name),xrtStrView(R->Headers[I].Value));
    if(Ok&&R->BodySize)Ok=xrtHttpRequestSetBytes(Q,(xbytesview){R->Body,R->BodySize},(xstrview){0});
    xhttpcalloptions O;xrtHttpCallOptionsInit(&O);O.Cancel=R->Cancel;O.ResponseBodyLimit=R->MaxBodyBytes;O.Timeout=R->Timeout;O.IdleTimeout=R->IdleTimeout;
    xhttpresult* Result=Ok?xrtHttpClientDoSync(C,Q,&O):NULL;const xhttpresponse* Response=xrtHttpResultResponse(Result);Ok=Response!=NULL;
    if(Ok){xbytesview Body=xrtHttpResponseBody(Response);const xhttpfield* Type=xrtHttpResponseHeader(Response,XRT_STR_LITERAL("Content-Type"));
        Out->Status=xrtHttpResponseStatus(Response);Out->FinalUrl=xrtStrDup(R->Url);Out->ContentType=xrtStrDupN(Type?Type->Value.Data:"",Type?Type->Value.Size:0);
        Out->Body=xrtMalloc(Body.Size+1);Out->BodySize=Body.Size;Out->Version=XS_FETCH_RESPONSE_VERSION;
        Ok=Out->FinalUrl&&Out->ContentType&&Out->Body;if(Ok){memcpy(Out->Body,Body.Data,Body.Size);Out->Body[Body.Size]=0;}
    }
    xrtHttpResultDestroy(Result);xrtHttpRequestDestroy(Q);xrtHttpClientDestroy(C);return Ok;
}
void MdoDownloadFetchUnit(XS_FetchResponse* R){if(!R)return;xrtFree(R->FinalUrl);xrtFree(R->ContentType);xrtFree(R->Body);memset(R,0,sizeof(*R));}
