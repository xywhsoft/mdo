#include "../../include/mdo/time.h"
#include "../../include/mdo/distribution.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/version.h"
#include "../remote/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void MdoApiLiveChanged(void* Data);
static struct {
    xmutex* Lock;xthread* Thread;xcancel* Cancel;XS_ServerInfo* Server;
    xvalue* Catalog;bool Busy;unsigned Command;char Message[256],Stage[24],NotificationSession[33];
} g_MdoDistribution;
static cstr MdoDistText(const xvalue* V,cstr K)
{ xstrview S={0};return xrtValueGetString(xrtValueObjectGet(V,xrtStrView(K)),&S)&&S.Size<=4096&&!memchr(S.Data,0,S.Size)?S.Data:""; }
static uint64 MdoDistNumber(const xvalue* V,cstr K)
{uint64 N=0;int64 I=0;const xvalue* X=xrtValueObjectGet(V,xrtStrView(K));if(!xrtValueGetUInt(X,&N)&&xrtValueGetInt(X,&I)&&I>=0)N=(uint64)I;return N;}
static bool MdoDistSet(xvalue* V,cstr K,cstr S)
{return xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S?S:"")));}
uint64 MdoBuildId(void)
{
    str Text=xrtEnvGet("MDO_BUILD_ID"); uint64 Id=0; if(Text && strlen(Text)==8)for(size_t i=0;i<8;i++){if(Text[i]<'0'||Text[i]>'9'){Id=0;break;}Id=Id*10+(unsigned)(Text[i]-'0');} xrtFree(Text);
    return Id>=10000000 && Id<=99999999?Id:MDO_BUILD_ID;
}
cstr MdoEdition(void)
{
#if defined(__ANDROID__)
    str Text=xrtEnvGet("MDO_EDITION"); bool Full=Text&&!strcmp(Text,"full"); xrtFree(Text); return Full?"full":"lite";
#elif defined(MDO_PRODUCT_EDITION)
    return MDO_PRODUCT_EDITION;
#else
    return "desktop";
#endif
}
cstr MdoToolPlatform(void)
{
#if defined(__ANDROID__)
    return "android-arm64-v8a";
#elif defined(_WIN32) || defined(_WIN64)
    return "windows-x86_64";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-arm64";
#else
    return "unsupported";
#endif
}
static xvalue* MdoDistRead(cstr Path,size_t Limit)
{
    bool Missing=false;char* Text=MdoExtensionRead(Path,true,Limit,&Missing);
    xvalue* V=Text?xrtJsonParse(xrtStrView(Text)):NULL;xrtFree(Text);return V;
}
static bool MdoDistSave(cstr Path,xvalue* V)
{
    char* Text=xrtJsonStringify(V,false,NULL);bool Ok=Text&&strlen(Text)<=1048576&&MdoHomeAtomicWrite(Path,Text,strlen(Text),false);xrtFree(Text);return Ok;
}
static bool MdoDistFetch(void)
{
    XS_FetchRequest Q={0};XS_FetchResponse Reply={0};char Url[256];
    snprintf(Url,sizeof(Url),MDO_DISTRIBUTION_ORIGIN "/mdo/catalog?platform=%s&edition=%s&build_id=%llu",MdoToolPlatform(),MdoEdition(),(unsigned long long)MdoBuildId());
    Q.Size=sizeof(Q);Q.Version=XS_FETCH_REQUEST_VERSION;Q.Url=Url;Q.MaxBodyBytes=1048576;Q.Timeout=15000000;Q.IdleTimeout=15000000;Q.Cancel=g_MdoDistribution.Cancel;Reply.Size=sizeof(Reply);
    bool Ok=xsFetch(&Q,&Reply)&&Reply.Status==200;xvalue* Root=Ok?xrtJsonParse(xrtStrViewN((cstr)Reply.Body,Reply.BodySize)):NULL;
    const xvalue* Notices=xrtValueObjectGet(Root,XRT_STR_LITERAL("notices"));Ok=Root&&xrtValueType(Root)==XVALUE_OBJECT&&xrtValueType(Notices)==XVALUE_ARRAY&&xrtValueCount(Notices)<=100;
    if(Ok){xrtValueObjectRemove(Root,XRT_STR_LITERAL("toolpacks"));(void)MdoDistSave("data/notifications/catalog.json",Root);
        xrtMutexLock(g_MdoDistribution.Lock);xrtValueRelease(g_MdoDistribution.Catalog);g_MdoDistribution.Catalog=Root;Root=NULL;xrtMutexUnlock(g_MdoDistribution.Lock);MdoApiLiveChanged(NULL);}
    xrtValueRelease(Root);xsFetchResponseUnit(&Reply);return Ok;
}
static bool MdoDistInvalidate(bool Binary,xbytesview Message,void* Data)
{(void)Message;if(Binary)return false;*(bool*)Data=true;return true;}
static int32 MdoDistWorker(void* Data)
{
    (void)Data;MdoRemoteNet Net={0};MdoRemoteSocket* Socket=NULL;uint64 Next=0,Retry=0;
    while(!xrtThreadStopping()&&!xrtCancelRequested(g_MdoDistribution.Cancel)){
        xrtMutexLock(g_MdoDistribution.Lock);unsigned Command=g_MdoDistribution.Command;g_MdoDistribution.Command=0;xrtMutexUnlock(g_MdoDistribution.Lock);bool Dirty=false;
        if(Socket&&!MdoRemoteSocketPoll(Socket,MdoDistInvalidate,&Dirty)){MdoRemoteSocketDestroy(Socket);Socket=NULL;Retry=MdoDeadlineAfterUs(30000000);}
        if(Command||Dirty||!Next||MdoDeadlineExpired(Next)){
            bool Ok=MdoDistFetch();Next=MdoDeadlineAfterUs(300000000);xrtMutexLock(g_MdoDistribution.Lock);g_MdoDistribution.Busy=false;
            snprintf(g_MdoDistribution.Stage,sizeof(g_MdoDistribution.Stage),"%s",Ok?"complete":"failed");snprintf(g_MdoDistribution.Message,sizeof(g_MdoDistribution.Message),"%s",Ok?"":"Online service unavailable; cached notices retained");xrtMutexUnlock(g_MdoDistribution.Lock);
        }
        if(!Socket&&(!Retry||MdoDeadlineExpired(Retry))){
            if(!Net.Engine)MdoRemoteNetInit(&Net,g_MdoDistribution.Server->Engine,NULL);
            MdoRemoteSocketConfig Config={MDO_DISTRIBUTION_HOST,MDO_DISTRIBUTION_PORT,MDO_DISTRIBUTION_SECURE,"/mdo/push",MDO_DISTRIBUTION_ORIGIN,"mdo.notifications.v1","mdo.notifications.v1",4096};
            Socket=MdoRemoteSocketOpen(&Net,&Config,g_MdoDistribution.Cancel,NULL);Retry=MdoDeadlineAfterUs(60000000);
        }xrtSleep(100);
    }MdoRemoteSocketDestroy(Socket);MdoRemoteNetUnit(&Net);return 0;
}
bool MdoDistributionInit(XS_ServerInfo* Server)
{
    if(g_MdoDistribution.Lock)return true;
    unsigned char Session[16];
    if(!xrtSecureRandom(Session,sizeof(Session)))return false;
    for(size_t I=0;I<16;I++)snprintf(g_MdoDistribution.NotificationSession+I*2,3,"%02x",Session[I]);
    g_MdoDistribution.Lock=xrtMutexCreate();g_MdoDistribution.Cancel=xrtCancelCreate();g_MdoDistribution.Server=Server;
    g_MdoDistribution.Catalog=MdoDistRead("data/notifications/catalog.json",1048576);
    if(xrtValueType(g_MdoDistribution.Catalog)!=XVALUE_OBJECT){xrtValueRelease(g_MdoDistribution.Catalog);g_MdoDistribution.Catalog=xrtValueObject();}
    xrtValueObjectRemove(g_MdoDistribution.Catalog,XRT_STR_LITERAL("toolpacks"));
    if(!g_MdoDistribution.Lock||!g_MdoDistribution.Cancel||!g_MdoDistribution.Catalog||!MdoCliInit(Server)){MdoDistributionUnit();return false;}
    g_MdoDistribution.Thread=xrtThreadCreate(MdoDistWorker,NULL,0);if(!g_MdoDistribution.Thread){MdoDistributionUnit();return false;}return true;
}
void MdoDistributionUnit(void)
{
    MdoCliUnit();if(g_MdoDistribution.Cancel)xrtCancelRequest(g_MdoDistribution.Cancel);
    if(g_MdoDistribution.Thread){xrtThreadStop(g_MdoDistribution.Thread);xrtThreadWait(g_MdoDistribution.Thread);xrtThreadDestroy(g_MdoDistribution.Thread);}
    xrtValueRelease(g_MdoDistribution.Catalog);xrtCancelDestroy(g_MdoDistribution.Cancel);if(g_MdoDistribution.Lock)xrtMutexDestroy(g_MdoDistribution.Lock);memset(&g_MdoDistribution,0,sizeof(g_MdoDistribution));
}
bool MdoDistributionRequest(cstr Action,cstr Id)
{
    (void)Id;if(!g_MdoDistribution.Lock||!Action||(strcmp(Action,"catalog")&&strcmp(Action,"refresh")))return false;
    xrtMutexLock(g_MdoDistribution.Lock);bool Ok=!g_MdoDistribution.Busy&&!xrtCancelRequested(g_MdoDistribution.Cancel);
    if(Ok){g_MdoDistribution.Busy=true;g_MdoDistribution.Command=1;snprintf(g_MdoDistribution.Stage,sizeof(g_MdoDistribution.Stage),"queued");}xrtMutexUnlock(g_MdoDistribution.Lock);return Ok;
}
xvalue* MdoDistributionSnapshot(void)
{
    if(!g_MdoDistribution.Lock)return NULL;
    xrtMutexLock(g_MdoDistribution.Lock);
    xvalue* R=xrtValueDeepClone(g_MdoDistribution.Catalog);
    if(R){xrtValueObjectSetNew(R,XRT_STR_LITERAL("busy"),xrtValueBool(g_MdoDistribution.Busy));MdoDistSet(R,"message",g_MdoDistribution.Message);MdoDistSet(R,"stage",g_MdoDistribution.Stage);MdoDistSet(R,"notification_session",g_MdoDistribution.NotificationSession);}
    xrtMutexUnlock(g_MdoDistribution.Lock);if(R){xrtValueObjectSetNew(R,XRT_STR_LITERAL("build_id"),xrtValueUInt(MdoBuildId()));MdoDistSet(R,"edition",MdoEdition());MdoDistSet(R,"platform",MdoToolPlatform());}return R;
}
#include "cli.inc.c"
bool MdoToolPrompt(char** Prompt){return MdoCliToolPrompt(Prompt);}
