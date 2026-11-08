#ifndef MDO_BROWSER_CDP_JNI_INC_C
#define MDO_BROWSER_CDP_JNI_INC_C
#include <jni.h>
#include "MdoBrowserCdpWebSocket.c"

static void MdoCdpJniThrow(JNIEnv* Env,enum MdoCdpSocketCode Code)
{
    if ( (*Env)->ExceptionCheck(Env) ) return;
    jclass Error=(*Env)->FindClass(Env,"java/io/IOException");
    if ( Error ) {(*Env)->ThrowNew(Env,Error,mdoCdpSocketCode(Code));(*Env)->DeleteLocalRef(Env,Error);}
}
static jbyteArray MdoCdpJniBytes(JNIEnv* Env,MdoCdpDiscovery* Reply,uint64_t Handle)
{
    jbyteArray Bytes=(*Env)->NewByteArray(Env,(jsize)Reply->Size);
    if ( Bytes ) (*Env)->SetByteArrayRegion(Env,Bytes,0,(jsize)Reply->Size,(const jbyte*)Reply->Data);
    mdoCdpDiscoveryFree(Reply);
    if ( !Bytes || (*Env)->ExceptionCheck(Env) ) {
        /* A lost CDP event/reply invalidates the dispatcher stream. */
        mdoCdpWsClose(Handle);return NULL;
    }
    return Bytes;
}

JNIEXPORT jbyteArray JNICALL
Java_org_xserver_android_MdoBrowserCdpNative_discoverNative(JNIEnv* Env,jclass Type,jint Timeout)
{
    (void)Type;
    MdoCdpDiscovery Reply=mdoCdpDiscover(Timeout);
    if ( Reply.Code!=MDO_CDP_SOCKET_OK ) {
        enum MdoCdpSocketCode Code=Reply.Code;
        mdoCdpDiscoveryFree(&Reply);
        MdoCdpJniThrow(Env,Code);
        return NULL;
    }
    return MdoCdpJniBytes(Env,&Reply,0);
}

static jlong MdoCdpJniOpen(JNIEnv* Env,jbyteArray Id,jint Timeout,int Browser)
{
    unsigned char Text[128];uint64_t Handle=0;
    enum MdoCdpSocketCode Code=MDO_CDP_SOCKET_ARGUMENTS;
    jsize Size=Id?(*Env)->GetArrayLength(Env,Id):0;
    if ( (*Env)->ExceptionCheck(Env) ) return 0;
    if ( Size<1 || Size>(jsize)sizeof(Text) || Timeout<10 || Timeout>5000 ) goto Done;
    (*Env)->GetByteArrayRegion(Env,Id,0,Size,(jbyte*)Text);
    if ( !(*Env)->ExceptionCheck(Env) ) Code=Browser?mdoCdpWsOpenBrowser((const char*)Text,(size_t)Size,Timeout,&Handle):mdoCdpWsOpen((const char*)Text,(size_t)Size,Timeout,&Handle);
Done:
    MdoCdpClear(Text,sizeof(Text));
    if ( Code!=MDO_CDP_SOCKET_OK ) MdoCdpJniThrow(Env,Code);
    return (jlong)Handle;
}
JNIEXPORT jlong JNICALL
Java_org_xserver_android_MdoBrowserCdpNative_openNative(JNIEnv* Env,jclass Type,jbyteArray Id,jint Timeout)
{(void)Type;return MdoCdpJniOpen(Env,Id,Timeout,0);}
JNIEXPORT jlong JNICALL
Java_org_xserver_android_MdoBrowserCdpNative_openBrowserNative(JNIEnv* Env,jclass Type,jbyteArray Path,jint Timeout)
{(void)Type;return MdoCdpJniOpen(Env,Path,Timeout,1);}

JNIEXPORT jbyteArray JNICALL
Java_org_xserver_android_MdoBrowserCdpNative_discoverBrowserNative(JNIEnv* Env,jclass Type,jint Timeout)
{
    (void)Type;MdoCdpDiscovery Reply=mdoCdpDiscoverBrowser(Timeout);
    if ( Reply.Code!=MDO_CDP_SOCKET_OK ) {
        enum MdoCdpSocketCode Code=Reply.Code;mdoCdpDiscoveryFree(&Reply);MdoCdpJniThrow(Env,Code);return NULL;
    }
    return MdoCdpJniBytes(Env,&Reply,0);
}

JNIEXPORT void JNICALL
Java_org_xserver_android_MdoBrowserCdpNative_sendNative(JNIEnv* Env,jclass Type,jlong Handle,jbyteArray Bytes,jint Timeout)
{
    (void)Type;jsize Size=Bytes?(*Env)->GetArrayLength(Env,Bytes):0;
    if ( (*Env)->ExceptionCheck(Env) ) return;
    if ( Size<1 || Size>(jsize)MDO_CDP_WS_SEND_MAX || Timeout<10 || Timeout>5000 || Handle<=0 ) {
        MdoCdpJniThrow(Env,MDO_CDP_SOCKET_ARGUMENTS);return;
    }
    unsigned char* Data=MDO_CDP_ALLOC((size_t)Size);
    if ( !Data ) {mdoCdpWsClose((uint64_t)Handle);MdoCdpJniThrow(Env,MDO_CDP_SOCKET_MEMORY);return;}
    (*Env)->GetByteArrayRegion(Env,Bytes,0,Size,(jbyte*)Data);
    enum MdoCdpSocketCode Code=MDO_CDP_SOCKET_IO;
    if ( !(*Env)->ExceptionCheck(Env) ) Code=mdoCdpWsSend((uint64_t)Handle,Data,(size_t)Size,Timeout);
    else mdoCdpWsClose((uint64_t)Handle);
    MdoCdpClear(Data,(size_t)Size);MDO_CDP_FREE(Data);
    if ( Code!=MDO_CDP_SOCKET_OK ) MdoCdpJniThrow(Env,Code);
}

JNIEXPORT jbyteArray JNICALL
Java_org_xserver_android_MdoBrowserCdpNative_receiveNative(JNIEnv* Env,jclass Type,jlong Handle,jint Timeout)
{
    (void)Type;MdoCdpDiscovery Reply=mdoCdpWsReceive((uint64_t)Handle,Timeout);
    if ( Reply.Code!=MDO_CDP_SOCKET_OK ) {
        enum MdoCdpSocketCode Code=Reply.Code;mdoCdpDiscoveryFree(&Reply);
        if ( Code!=MDO_CDP_SOCKET_TIMEOUT ) MdoCdpJniThrow(Env,Code);
        return NULL; /* A normal idle timeout never publishes partial text. */
    }
    return MdoCdpJniBytes(Env,&Reply,(uint64_t)Handle);
}

JNIEXPORT void JNICALL
Java_org_xserver_android_MdoBrowserCdpNative_closeNative(JNIEnv* Env,jclass Type,jlong Handle)
{
    (void)Env;(void)Type;mdoCdpWsClose((uint64_t)Handle);
}

/* Android's platform JSON reader is intentionally lenient. Admit only bounded,
 * complete strict JSON, rejecting duplicates at every depth, before Java uses
 * it to route protocol IDs/events. Reuse the pinned host parser, not another
 * handwritten JSON grammar. No bytes are logged or returned by this gate. */
JNIEXPORT void JNICALL
Java_org_xserver_android_MdoBrowserCdpNative_validateNative(JNIEnv* Env,jclass Type,jbyteArray Bytes,jint Maximum)
{
    (void)Type;jsize Size=Bytes?(*Env)->GetArrayLength(Env,Bytes):0;
    if ( (*Env)->ExceptionCheck(Env) ) return;
    if ( (Maximum!=(jint)MDO_CDP_WS_SEND_MAX&&Maximum!=(jint)MDO_CDP_WS_MESSAGE&&Maximum!=(jint)MDO_CDP_BROWSER_DISCOVERY_MAX) || Size<1 || Size>Maximum ) {
        MdoCdpJniThrow(Env,MDO_CDP_SOCKET_JSON);return;
    }
    unsigned char* Data=MDO_CDP_ALLOC((size_t)Size);
    if ( !Data ) {MdoCdpJniThrow(Env,MDO_CDP_SOCKET_MEMORY);return;}
    (*Env)->GetByteArrayRegion(Env,Bytes,0,Size,(jbyte*)Data);
    int Valid=0;
    if ( !(*Env)->ExceptionCheck(Env) ) {
        xjsonreadconfig Config;xrtJsonReadConfigInit(&Config);
        Config.MaxInputBytes=(size_t)Maximum;Config.MaxStringBytes=(size_t)Maximum;
        Config.MaxDepth=64;Config.MaxValues=65536;Config.MaxContainerItems=8192;
        xvalue* Value=xrtJsonRead((xstrview){(const char*)Data,(size_t)Size},&Config);
        Valid=Value!=NULL;xrtValueRelease(Value);
    }
    MdoCdpClear(Data,(size_t)Size);MDO_CDP_FREE(Data);
    if ( !Valid ) MdoCdpJniThrow(Env,MDO_CDP_SOCKET_JSON);
}
#endif
