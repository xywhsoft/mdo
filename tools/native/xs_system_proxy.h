#ifndef XS_PLATFORM_SYSTEM_PROXY_H
#define XS_PLATFORM_SYSTEM_PROXY_H
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

typedef struct { char Host[256];unsigned Port;bool Tls; } XS_ProxyTarget;
static bool XS_ProxyFailure(XS_SystemProxyInfo* R,cstr Why)
{
    R->Kind=XS_SYSTEM_PROXY_DIRECT;R->Port=0;R->Host[0]=R->User[0]=0;
    xrtSecureZero(R->Password,sizeof(R->Password));
    snprintf(R->Reason,sizeof(R->Reason),"%s",Why);return false;
}
static unsigned XS_ProxyLower(unsigned C){return C>='A'&&C<='Z'?C+32:C;}
static bool XS_ProxyEqualN(cstr A,cstr B,size_t N)
{for(size_t I=0;I<N;I++)if(XS_ProxyLower((unsigned char)A[I])!=XS_ProxyLower((unsigned char)B[I]))return false;return true;}
static bool XS_ProxyText(cstr S,size_t Limit)
{size_t I=0;for(;S&&I<=Limit&&S[I];I++)if((unsigned char)S[I]<32||(unsigned char)S[I]==127)return false;return S&&I<=Limit;}
static bool XS_ProxyPort(cstr S,size_t N,unsigned* Port)
{unsigned V=0;if(!N||N>5)return false;for(size_t I=0;I<N;I++){if(S[I]<'0'||S[I]>'9')return false;V=V*10+(unsigned)(S[I]-'0');}if(!V||V>65535)return false;*Port=V;return true;}
static bool XS_ProxyAuthority(cstr S,size_t N,char Host[256],unsigned* Port)
{
    size_t Start=0,End=N,Colon=N;
    if(N&&S[0]=='['){Start=1;End=1;while(End<N&&S[End]!=']')End++;if(End==N)return false;if(End+1<N){if(S[End+1]!=':')return false;Colon=End+1;}}
    else{for(size_t I=0;I<N;I++)if(S[I]==':'){if(Colon!=N)return false;Colon=I;}End=Colon;}
    if(!End||End<=Start||End-Start>=256)return false;
    for(size_t I=Start;I<End;I++)if((unsigned char)S[I]<=32||(unsigned char)S[I]>126||strchr("/@?#\\",S[I]))return false;
    if(Colon<N&&!XS_ProxyPort(S+Colon+1,N-Colon-1,Port))return false;
    memcpy(Host,S+Start,End-Start);Host[End-Start]=0;return true;
}
static bool XS_ProxyTargetRead(cstr Url,XS_ProxyTarget* T)
{
    if(!XS_ProxyText(Url,16384))return false;
    bool Tls=strlen(Url)>=8&&XS_ProxyEqualN(Url,"https://",8);
    size_t Start=Tls?8:7;if(!Tls&&(strlen(Url)<7||!XS_ProxyEqualN(Url,"http://",7)))return false;
    size_t End=Start;while(Url[End]&&!strchr("/?#",Url[End]))End++;
    memset(T,0,sizeof(*T));T->Tls=Tls;T->Port=Tls?443:80;
    return XS_ProxyAuthority(Url+Start,End-Start,T->Host,&T->Port);
}
static bool XS_ProxyGlob(cstr S,cstr P)
{
    cstr Star=NULL,Retry=NULL;
    while(*S){if(*P=='*'){Star=P++;Retry=S;}else if(*P&&XS_ProxyLower((unsigned char)*P)==XS_ProxyLower((unsigned char)*S)){P++;S++;}else if(Star){P=Star+1;S=++Retry;}else return false;}
    while(*P=='*')P++;
    return !*P;
}
static bool XS_ProxyBypass(const XS_ProxyTarget* T,cstr Rules,bool Windows)
{
    if(!Rules)return false;
    for(size_t At=0;Rules[At];){
        while(Rules[At]&&(Rules[At]==','||Rules[At]==';'||Rules[At]==' '||Rules[At]=='\t'))At++;
        size_t End=At;while(Rules[End]&&Rules[End]!=','&&Rules[End]!=';'&&Rules[End]!=' '&&Rules[End]!='\t')End++;
        size_t N=End-At;if(!N)break;
        if(N<512){char Pattern[512];memcpy(Pattern,Rules+At,N);Pattern[N]=0;
            if(Windows&&!strcmp(Pattern,"<local>")&&!strchr(T->Host,'.'))return true;
            cstr P=Pattern;unsigned Port=T->Port;char Host[256];
            if(strchr(P,':')&&XS_ProxyAuthority(P,strlen(P),Host,&Port)){if(Port!=T->Port){At=End;continue;}P=Host;}
            if(XS_ProxyGlob(T->Host,P))return true;
            if(!Windows){if(*P=='.')P++;size_t H=strlen(T->Host),D=strlen(P);
                if(D&&H>=D&&XS_ProxyEqualN(T->Host+H-D,P,D)&&(H==D||T->Host[H-D-1]=='.'))return true;}
        }At=End;
    }return false;
}
static int XS_ProxyHex(unsigned C){C=XS_ProxyLower(C);return C>='0'&&C<='9'?(int)(C-'0'):C>='a'&&C<='f'?(int)(C-'a'+10):-1;}
static bool XS_ProxyDecode(cstr S,size_t N,char* Out,size_t Capacity)
{
    size_t J=0;for(size_t I=0;I<N;I++){unsigned C=(unsigned char)S[I];if(C=='%'){if(I+2>=N)return false;int A=XS_ProxyHex((unsigned char)S[I+1]),B=XS_ProxyHex((unsigned char)S[I+2]);if(A<0||B<0)return false;C=(unsigned)(A*16+B);I+=2;}if(!C||C<32||C==127||J+1>=Capacity)return false;Out[J++]=(char)C;}Out[J]=0;return true;
}
static bool XS_ProxyEndpoint(cstr Text,XS_SystemProxyInfo* R)
{
    if(!XS_ProxyText(Text,8192)||!Text[0])return XS_ProxyFailure(R,"The system proxy address is empty or invalid");
    cstr S=Text;unsigned Port=80;uint32 Kind=XS_SYSTEM_PROXY_HTTP_CONNECT;
    if(strlen(S)>=7&&XS_ProxyEqualN(S,"http://",7))S+=7;
    else if(strlen(S)>=9&&XS_ProxyEqualN(S,"socks5://",9)){S+=9;Port=1080;Kind=XS_SYSTEM_PROXY_SOCKS5;}
    else if(strlen(S)>=10&&XS_ProxyEqualN(S,"socks5h://",10)){S+=10;Port=1080;Kind=XS_SYSTEM_PROXY_SOCKS5;}
    else if(strstr(S,"://"))return XS_ProxyFailure(R,"This system proxy protocol is unsupported; use HTTP CONNECT or SOCKS5");
    cstr At=strchr(S,'@');
    if(At){cstr Colon=memchr(S,':',(size_t)(At-S));if(strchr(At+1,'@')||!XS_ProxyDecode(S,(size_t)((Colon?Colon:At)-S),R->User,sizeof(R->User))||!R->User[0]||
            (Colon&&!XS_ProxyDecode(Colon+1,(size_t)(At-Colon-1),R->Password,sizeof(R->Password))))return XS_ProxyFailure(R,"The system proxy credentials are invalid");
        S=At+1;
    }
    size_t N=strlen(S);if(N&&S[N-1]=='/')N--;
    if(!XS_ProxyAuthority(S,N,R->Host,&Port))return XS_ProxyFailure(R,"The system proxy host or port is invalid");
    R->Port=(uint16)Port;R->Kind=Kind;return true;
}
#if defined(_WIN32)
static bool XS_ProxyWindowsList(const XS_ProxyTarget* T,cstr List,cstr Bypass,XS_SystemProxyInfo* R)
{
    if(XS_ProxyBypass(T,Bypass,true))return true;
    if(!XS_ProxyText(List,65536))return XS_ProxyFailure(R,"The system proxy list is invalid");
    int Best=0;bool UnversionedSocks=false;char Selected[8193]={0};
    for(size_t At=0;List[At];){
        while(List[At]&&(List[At]==';'||List[At]==' '||List[At]=='\t'))At++;
        size_t End=At;while(List[End]&&List[End]!=';'&&List[End]!=' '&&List[End]!='\t')End++;
        size_t N=End-At;if(!N)break;cstr S=List+At;int Rank=2;
        cstr Equal=memchr(S,'=',N);bool Socks=false,Unversioned=false;
        if(Equal){size_t K=(size_t)(Equal-S);Rank=0;
            if(K==(T->Tls?5u:4u)&&XS_ProxyEqualN(S,T->Tls?"https":"http",K))Rank=3;
            else if(K==6&&XS_ProxyEqualN(S,"socks5",6)){Rank=1;Socks=true;}
            else if(K==5&&XS_ProxyEqualN(S,"socks",5)){Rank=1;Unversioned=true;}
            N-=(size_t)(Equal-S)+1;S=Equal+1;
        }
        if(Rank>Best){if(!N||N>8192)return XS_ProxyFailure(R,"The system proxy address is too long");memcpy(Selected,S,N);Selected[N]=0;
            if(Socks&&!strstr(Selected,"://")){if(N+9>=sizeof(Selected))return XS_ProxyFailure(R,"The system proxy address is too long");memmove(Selected+9,Selected,N+1);memcpy(Selected,"socks5://",9);}
            Best=Rank;UnversionedSocks=Unversioned;}
        At=End;
    }
    bool Ok=UnversionedSocks?XS_ProxyFailure(R,"The system selected unversioned SOCKS; configure SOCKS5 manually"):!Best||XS_ProxyEndpoint(Selected,R);
    xrtSecureZero(Selected,sizeof(Selected));return Ok;
}
typedef struct XS_ProxyWinApi {
    BOOL (WINAPI *User)(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG*);
    BOOL (WINAPI *Default)(WINHTTP_PROXY_INFO*);
    HINTERNET (WINAPI *Open)(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,DWORD);
    BOOL (WINAPI *Resolve)(HINTERNET,LPCWSTR,WINHTTP_AUTOPROXY_OPTIONS*,WINHTTP_PROXY_INFO*);
    BOOL (WINAPI *Timeouts)(HINTERNET,int,int,int,int);
    BOOL (WINAPI *Close)(HINTERNET);
} XS_ProxyWinApi;
static char* XS_ProxyUtf8(LPCWSTR S)
{
    if(!S)return NULL;
    int N=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,S,-1,NULL,0,NULL,NULL);
    if(N<=0||N>65537)return NULL;
    char* Text=xrtMalloc((size_t)N);
    if(Text&&!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,S,-1,Text,N,NULL,NULL)){xrtFree(Text);Text=NULL;}return Text;
}
static bool XS_ProxyWinResult(const XS_ProxyTarget* T,const WINHTTP_PROXY_INFO* P,XS_SystemProxyInfo* R)
{
    if(P->dwAccessType==WINHTTP_ACCESS_TYPE_NO_PROXY)return true;
    char* List=XS_ProxyUtf8(P->lpszProxy),*Bypass=XS_ProxyUtf8(P->lpszProxyBypass);
    bool Ok=List&&(!P->lpszProxyBypass||Bypass)&&XS_ProxyWindowsList(T,List,Bypass,R);
    if(!Ok&&!R->Reason[0])XS_ProxyFailure(R,"Windows returned an invalid proxy configuration");
    if(List){xrtSecureZero(List,strlen(List));xrtFree(List);}xrtFree(Bypass);return Ok;
}
static bool XS_ProxyWindowsResolve(const XS_ProxyWinApi* A,cstr Url,const XS_ProxyTarget* T,uint32 Timeout,XS_SystemProxyInfo* R)
{
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG C={0};WINHTTP_PROXY_INFO P={0};bool Ok=false;
    if(!A->User(&C)){
        if(A->Default(&P))Ok=XS_ProxyWinResult(T,&P,R);
        else XS_ProxyFailure(R,"Windows system proxy settings could not be read");
        goto done;
    }
    /* A configured static proxy does not need WPAD network discovery. */
    if(C.lpszAutoConfigUrl||(!C.lpszProxy&&C.fAutoDetect)){
        int N=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,Url,-1,NULL,0);wchar_t* Wide=N>0?xrtMalloc((size_t)N*sizeof(wchar_t)):NULL;
        HINTERNET H=Wide?A->Open(L"xs-system-proxy",WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,0):NULL;
        WINHTTP_AUTOPROXY_OPTIONS O={0};O.dwFlags=C.lpszAutoConfigUrl?WINHTTP_AUTOPROXY_CONFIG_URL:WINHTTP_AUTOPROXY_AUTO_DETECT;
        O.lpszAutoConfigUrl=C.lpszAutoConfigUrl;O.dwAutoDetectFlags=C.lpszAutoConfigUrl?0:WINHTTP_AUTO_DETECT_TYPE_DHCP|WINHTTP_AUTO_DETECT_TYPE_DNS_A;
        /* Never send the user's Windows logon credentials to a PAC server. */
        O.fAutoLogonIfChallenged=FALSE;
        bool Resolved=H&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,Url,-1,Wide,N)&&A->Timeouts(H,(int)Timeout,(int)Timeout,(int)Timeout,(int)Timeout)&&A->Resolve(H,Wide,&O,&P);
        DWORD Error=Resolved?0:GetLastError();if(H)A->Close(H);xrtFree(Wide);
        if(Resolved){Ok=XS_ProxyWinResult(T,&P,R);goto done;}
        if(!C.lpszProxy){
            if(!C.lpszAutoConfigUrl&&Error==ERROR_WINHTTP_AUTODETECTION_FAILED)Ok=true;
            else XS_ProxyFailure(R,"Windows PAC or automatic proxy discovery failed; check system settings or use a manual proxy");
            goto done;
        }
    }
    if(!C.lpszProxy){Ok=true;goto done;}
    {WINHTTP_PROXY_INFO Static={WINHTTP_ACCESS_TYPE_NAMED_PROXY,C.lpszProxy,C.lpszProxyBypass};Ok=XS_ProxyWinResult(T,&Static,R);}
done:
    if(C.lpszAutoConfigUrl)GlobalFree(C.lpszAutoConfigUrl);
    if(C.lpszProxy)GlobalFree(C.lpszProxy);
    if(C.lpszProxyBypass)GlobalFree(C.lpszProxyBypass);
    if(P.lpszProxy)GlobalFree(P.lpszProxy);
    if(P.lpszProxyBypass)GlobalFree(P.lpszProxyBypass);
    return Ok;
}
#endif

bool xsSystemProxyResolve(cstr Url,uint32 TimeoutMs,XS_SystemProxyInfo* R)
{
    if(!R||R->Size<sizeof(*R)||R->Version!=XS_SYSTEM_PROXY_VERSION)return false;
    uint32 Size=R->Size;memset(R,0,sizeof(*R));R->Size=Size;R->Version=XS_SYSTEM_PROXY_VERSION;
    XS_ProxyTarget T;if(!TimeoutMs||TimeoutMs>10000||!XS_ProxyTargetRead(Url,&T))return XS_ProxyFailure(R,"System proxy resolution requires a valid HTTP(S) target and timeout");
#if defined(_WIN32)
    HMODULE L=LoadLibraryExW(L"winhttp.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);XS_ProxyWinApi A={0};bool Ok=false;
    if(L){
        FARPROC P=GetProcAddress(L,"WinHttpGetIEProxyConfigForCurrentUser");memcpy(&A.User,&P,sizeof(P));
        P=GetProcAddress(L,"WinHttpGetDefaultProxyConfiguration");memcpy(&A.Default,&P,sizeof(P));
        P=GetProcAddress(L,"WinHttpOpen");memcpy(&A.Open,&P,sizeof(P));
        P=GetProcAddress(L,"WinHttpGetProxyForUrl");memcpy(&A.Resolve,&P,sizeof(P));
        P=GetProcAddress(L,"WinHttpSetTimeouts");memcpy(&A.Timeouts,&P,sizeof(P));
        P=GetProcAddress(L,"WinHttpCloseHandle");memcpy(&A.Close,&P,sizeof(P));
    }
    if(A.User&&A.Default&&A.Open&&A.Resolve&&A.Timeouts&&A.Close)Ok=XS_ProxyWindowsResolve(&A,Url,&T,TimeoutMs,R);
    else XS_ProxyFailure(R,"The Windows proxy service is unavailable");
    if(L)FreeLibrary(L);
    return Ok;
#elif defined(__ANDROID__)
    extern bool XS_AndroidSystemProxy(cstr Url,uint32 TimeoutMs,XS_SystemProxyInfo* Result);
    return XS_AndroidSystemProxy(Url,TimeoutMs,R);
#else
    cstr Bypass=getenv("no_proxy");if(!Bypass)Bypass=getenv("NO_PROXY");
    if(Bypass&&!XS_ProxyText(Bypass,65536))return XS_ProxyFailure(R,"The process proxy bypass list is invalid");
    if(XS_ProxyBypass(&T,Bypass,false))return true;
    cstr Proxy=getenv(T.Tls?"https_proxy":"http_proxy");if(!Proxy)Proxy=getenv(T.Tls?"HTTPS_PROXY":"HTTP_PROXY");
    if(!Proxy)Proxy=getenv("all_proxy");
    if(!Proxy)Proxy=getenv("ALL_PROXY");
    return !Proxy||!Proxy[0]||XS_ProxyEndpoint(Proxy,R);
#endif
}
#endif
