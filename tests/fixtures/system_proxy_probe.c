#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef const char* cstr;
#define XS_API
#define xrtMalloc malloc
#define xrtFree free
static void xrtSecureZero(void* P, size_t N) { volatile unsigned char* B=P; while(N--) *B++=0; }
#include "xs_system_proxy_api.h"
#include "xs_system_proxy.h"

static XS_SystemProxyInfo result(void)
{
    XS_SystemProxyInfo R={0}; R.Size=sizeof(R); R.Version=XS_SYSTEM_PROXY_VERSION; return R;
}
static void common(void)
{
    XS_ProxyTarget T;
    assert(XS_ProxyTargetRead("https://api.example.com/v1",&T) && T.Port==443 && T.Tls);
    assert(XS_ProxyBypass(&T,"example.com",false));
    assert(XS_ProxyBypass(&T,".example.com:443",false));
    assert(!XS_ProxyBypass(&T,".example.com:80",false));
    assert(!XS_ProxyBypass(&T,"ample.com",false));
    assert(XS_ProxyBypass(&T,"*.EXAMPLE.com",true));
    assert(!XS_ProxyBypass(&T,"example.com",true));
    assert(!XS_ProxyTargetRead("https://user:pass@api.example.com/",&T));
    assert(!XS_ProxyTargetRead("https://api.example.com:65536/",&T));
    assert(!XS_ProxyTargetRead("ftp://api.example.com/",&T));
    assert(!XS_ProxyTargetRead("https://api.example.com/\n",&T));
    assert(XS_ProxyTargetRead("https://[::1]:9443/v1",&T) && !strcmp(T.Host,"::1") && T.Port==9443);
    assert(XS_ProxyBypass(&T,"[::1]:9443",false));
    assert(!XS_ProxyBypass(&T,"[::1]:443",false));
    XS_SystemProxyInfo R=result();
    assert(XS_ProxyEndpoint("http://u%40ser:p%3Ass@127.0.0.1:7890/",&R));
    assert(R.Kind==XS_SYSTEM_PROXY_HTTP_CONNECT && R.Port==7890);
    assert(!strcmp(R.User,"u@ser") && !strcmp(R.Password,"p:ss"));
    assert(!XS_ProxyEndpoint("https://proxy.invalid:8443",&R));
    assert(!R.Password[0] && !R.User[0] && !R.Host[0] && R.Kind==XS_SYSTEM_PROXY_DIRECT);
    R=result(); assert(XS_ProxyEndpoint("socks5h://[::1]:1081",&R));
    assert(R.Kind==XS_SYSTEM_PROXY_SOCKS5 && R.Port==1081 && !strcmp(R.Host,"::1"));
    R=result(); assert(!XS_ProxyEndpoint("socks4://host:1080",&R));
    R=result(); assert(!XS_ProxyEndpoint("http://u:p%00@host:80",&R));
    R=result(); assert(!XS_ProxyEndpoint("http://u:p%0A@host:80",&R));
    R=result(); assert(!XS_ProxyEndpoint("http://host:0",&R));
    R=result(); assert(!XS_ProxyEndpoint("http://host:65536",&R));
    R=result(); assert(!XS_ProxyEndpoint("http://host:80/path",&R));
    R=result(); assert(!XS_ProxyEndpoint("http://[::1",&R));
    R=result(); assert(!xsSystemProxyResolve("https://example.com/",0,&R));
    R=result(); assert(!xsSystemProxyResolve("https://example.com/",10001,&R));
    R=result(); R.Version=2; assert(!xsSystemProxyResolve("https://example.com/",10,&R));
}
#ifdef _WIN32
static LPCWSTR ProxyText, BypassText, PacText;
static bool Auto, UserOk=true, DefaultOk=true, ResolveOk=true;
static DWORD ProxyError, DefaultType=WINHTTP_ACCESS_TYPE_NO_PROXY, ResolvedType=WINHTTP_ACCESS_TYPE_NAMED_PROXY;
static unsigned OpenCount, ResolveCount, CloseCount;
static wchar_t* duplicate(LPCWSTR S)
{ if(!S)return NULL; size_t N=(wcslen(S)+1)*sizeof(wchar_t);wchar_t* P=GlobalAlloc(GPTR,N);assert(P);memcpy(P,S,N);return P; }
static BOOL WINAPI user(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG* C)
{ if(!UserOk)return FALSE;C->lpszProxy=duplicate(ProxyText);C->lpszProxyBypass=duplicate(BypassText);C->lpszAutoConfigUrl=duplicate(PacText);C->fAutoDetect=Auto;return TRUE; }
static BOOL WINAPI defaults(WINHTTP_PROXY_INFO* P)
{ if(!DefaultOk)return FALSE;P->dwAccessType=DefaultType;P->lpszProxy=duplicate(ProxyText);return TRUE; }
static HINTERNET WINAPI open_proxy(LPCWSTR Agent,DWORD Type,LPCWSTR Name,LPCWSTR Bypass,DWORD Flags)
{ (void)Agent;(void)Name;(void)Bypass;(void)Flags;assert(Type==WINHTTP_ACCESS_TYPE_NO_PROXY);OpenCount++;return (HINTERNET)(uintptr_t)1; }
static BOOL WINAPI resolve(HINTERNET H,LPCWSTR Url,WINHTTP_AUTOPROXY_OPTIONS* O,WINHTTP_PROXY_INFO* P)
{ (void)H;assert(!wcscmp(Url,L"https://api.example.com/v1"));assert(!O->fAutoLogonIfChallenged);ResolveCount++;if(!ResolveOk){SetLastError(ProxyError);return FALSE;}P->dwAccessType=ResolvedType;P->lpszProxy=duplicate(L"pac-proxy:8123");return TRUE; }
static BOOL WINAPI timeouts(HINTERNET H,int A,int B,int C,int D)
{ (void)H;assert(A==5000&&A==B&&B==C&&C==D);return TRUE; }
static BOOL WINAPI close_proxy(HINTERNET H) { (void)H;CloseCount++;return TRUE; }
static XS_ProxyWinApi Api={user,defaults,open_proxy,resolve,timeouts,close_proxy};
static bool run_windows(XS_SystemProxyInfo* R)
{ XS_ProxyTarget T;assert(XS_ProxyTargetRead("https://api.example.com/v1",&T));*R=result();return XS_ProxyWindowsResolve(&Api,"https://api.example.com/v1",&T,5000,R); }
static void windows(void)
{
    XS_ProxyTarget T; XS_SystemProxyInfo R;
    assert(XS_ProxyTargetRead("https://api.example.com/v1",&T));
    R=result();assert(XS_ProxyWindowsList(&T,"http=a:81;https=b:82;socks=s:1080",NULL,&R));
    assert(!strcmp(R.Host,"b") && R.Port==82 && R.Kind==XS_SYSTEM_PROXY_HTTP_CONNECT);
    R=result();assert(XS_ProxyWindowsList(&T,"socks=s:1080;https=b:82",NULL,&R));assert(!strcmp(R.Host,"b"));
    R=result();assert(!XS_ProxyWindowsList(&T,"socks=s:1080",NULL,&R));
    R=result();assert(XS_ProxyWindowsList(&T,"socks5=s:1080",NULL,&R));assert(R.Kind==XS_SYSTEM_PROXY_SOCKS5);
    R=result();assert(XS_ProxyWindowsList(&T,"http=a:81",NULL,&R));assert(R.Kind==XS_SYSTEM_PROXY_DIRECT);
    R=result();assert(XS_ProxyWindowsList(&T,"a:81;https=b:82","*.example.com",&R));assert(R.Kind==XS_SYSTEM_PROXY_DIRECT);
    assert(XS_ProxyTargetRead("http://intranet/",&T));assert(XS_ProxyBypass(&T,"<local>",true));
    ProxyText=L"https=static-proxy:7890"; Auto=true;
    assert(run_windows(&R) && !strcmp(R.Host,"static-proxy") && OpenCount==0);
    PacText=L"http://pac.example.invalid/proxy.pac";
    assert(run_windows(&R) && !strcmp(R.Host,"pac-proxy") && R.Port==8123);
    ResolvedType=WINHTTP_ACCESS_TYPE_NO_PROXY;
    assert(run_windows(&R) && R.Kind==XS_SYSTEM_PROXY_DIRECT);
    ResolvedType=WINHTTP_ACCESS_TYPE_NAMED_PROXY;ResolveOk=false;ProxyError=ERROR_WINHTTP_TIMEOUT;
    assert(run_windows(&R) && !strcmp(R.Host,"static-proxy"));
    ProxyText=NULL;assert(!run_windows(&R) && R.Reason[0]);
    PacText=NULL;ProxyError=ERROR_WINHTTP_AUTODETECTION_FAILED;
    assert(run_windows(&R) && R.Kind==XS_SYSTEM_PROXY_DIRECT);
    ProxyError=ERROR_WINHTTP_TIMEOUT;assert(!run_windows(&R));
    Auto=false;assert(run_windows(&R) && R.Kind==XS_SYSTEM_PROXY_DIRECT);
    UserOk=false;DefaultType=WINHTTP_ACCESS_TYPE_NAMED_PROXY;ProxyText=L"default-proxy:8181";
    assert(run_windows(&R) && !strcmp(R.Host,"default-proxy"));
    DefaultOk=false;assert(!run_windows(&R));
    assert(OpenCount==ResolveCount && OpenCount==CloseCount);
}
#else
static void environment(void)
{
    const char* Names[]={"https_proxy","HTTPS_PROXY","http_proxy","HTTP_PROXY","all_proxy","ALL_PROXY","no_proxy","NO_PROXY"};
    for(size_t I=0;I<sizeof(Names)/sizeof(Names[0]);I++)unsetenv(Names[I]);
    XS_SystemProxyInfo R=result();assert(xsSystemProxyResolve("https://api.example.com/",5000,&R));assert(R.Kind==XS_SYSTEM_PROXY_DIRECT);
    setenv("HTTPS_PROXY","http://user:secret@upper:7890",1);
    assert(xsSystemProxyResolve("https://api.example.com/",5000,&R));assert(!strcmp(R.Host,"upper") && !strcmp(R.Password,"secret"));
    setenv("https_proxy","socks5://lower:1080",1);
    assert(xsSystemProxyResolve("https://api.example.com/",5000,&R));assert(!strcmp(R.Host,"lower") && !R.Password[0]);
    setenv("NO_PROXY",".example.com:443",1);
    assert(xsSystemProxyResolve("https://api.example.com/",5000,&R));assert(R.Kind==XS_SYSTEM_PROXY_DIRECT);
    setenv("no_proxy","different.invalid",1);
    assert(xsSystemProxyResolve("https://api.example.com/",5000,&R));assert(!strcmp(R.Host,"lower"));
    setenv("https_proxy","",1);
    assert(xsSystemProxyResolve("https://api.example.com/",5000,&R));assert(R.Kind==XS_SYSTEM_PROXY_DIRECT);
    unsetenv("https_proxy");unsetenv("HTTPS_PROXY");setenv("ALL_PROXY","http://fallback:8191",1);
    assert(xsSystemProxyResolve("https://api.example.com/",5000,&R));assert(!strcmp(R.Host,"fallback"));
    setenv("https_proxy","https://unsupported:8443",1);
    assert(!xsSystemProxyResolve("https://api.example.com/",5000,&R) && !R.Host[0] && !R.Password[0]);
    setenv("http_proxy","http://plain:80",1);
    assert(xsSystemProxyResolve("http://api.example.com/",5000,&R));assert(!strcmp(R.Host,"plain"));
}
#endif
int main(void)
{
    common();
#ifdef _WIN32
    windows();
#else
    environment();
#endif
    puts("system proxy native probe: PASS");return 0;
}
