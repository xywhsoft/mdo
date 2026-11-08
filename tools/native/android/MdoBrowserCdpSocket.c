/* App-private Android/Linux DevTools discovery. No debugging flag is changed.
 * The socket name/peer are derived from this process, never from a URL/caller. */
#include "MdoBrowserCdpSocket.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MDO_CDP_HEADER_MAX 8192u
#define MDO_CDP_BODY_MAX 262144u
#ifndef MDO_CDP_ALLOC
#define MDO_CDP_ALLOC malloc
#endif
#ifndef MDO_CDP_FREE
#define MDO_CDP_FREE free
#endif

static int MdoCdpNow(uint64_t* Now)
{
    struct timespec Tick;
    /* Android elapsedRealtime uses the suspend-inclusive boot clock. The
     * kernel transport must not renew Java's deadline while the device sleeps. */
    if ( clock_gettime(CLOCK_BOOTTIME,&Tick) || Tick.tv_sec<0 ||
         (uint64_t)Tick.tv_sec>UINT64_MAX/1000u-1u ) return 0;
    *Now=(uint64_t)Tick.tv_sec*1000u+(uint64_t)Tick.tv_nsec/1000000u;
    return 1;
}

static enum MdoCdpSocketCode MdoCdpWait(int Fd,short Events,uint64_t End)
{
    for (;;) {
        uint64_t Now;
        if ( !MdoCdpNow(&Now) ) return MDO_CDP_SOCKET_IO;
        if ( Now>=End ) return MDO_CDP_SOCKET_TIMEOUT;
        struct pollfd Wait={Fd,Events,0};
        /* poll's relative wait excludes suspend. Keep a short slice, then
         * recheck the boot deadline even when the descriptor became ready. */
        int Slice=End-Now>20u?20:(int)(End-Now);
        int Ready=poll(&Wait,1,Slice);
        if ( Ready<0 && errno==EINTR ) continue;
        if ( Ready<0 ) return MDO_CDP_SOCKET_IO;
        if ( !Ready ) continue; /* Recheck the absolute clock, not a new budget. */
        if ( !MdoCdpNow(&Now) ) return MDO_CDP_SOCKET_IO;
        if ( Now>=End ) return MDO_CDP_SOCKET_TIMEOUT;
        if ( Wait.revents&Events ) return MDO_CDP_SOCKET_OK;
        if ( Events==POLLIN && Wait.revents&POLLHUP ) return MDO_CDP_SOCKET_OK;
        return MDO_CDP_SOCKET_IO;
    }
}

static enum MdoCdpSocketCode MdoCdpCheckTime(uint64_t End)
{
    uint64_t Now;
    if ( !MdoCdpNow(&Now) ) return MDO_CDP_SOCKET_IO;
    return Now<End?MDO_CDP_SOCKET_OK:MDO_CDP_SOCKET_TIMEOUT;
}

static enum MdoCdpSocketCode MdoCdpConnect(int* Out,uint64_t End)
{
    struct sockaddr_un Address;
    memset(&Address,0,sizeof(Address));Address.sun_family=AF_UNIX;
    int Length=snprintf(Address.sun_path+1,sizeof(Address.sun_path)-1,
                        "webview_devtools_remote_%ld",(long)getpid());
    if ( Length<1 || (size_t)Length>=sizeof(Address.sun_path)-1 ) return MDO_CDP_SOCKET_ARGUMENTS;
    int Fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if ( Fd<0 ) return MDO_CDP_SOCKET_IO;
    *Out=Fd;
    socklen_t Bytes=(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1u+(size_t)Length);
    if ( connect(Fd,(struct sockaddr*)&Address,Bytes) ) {
        int Error=errno;
        if ( Error!=EINPROGRESS && Error!=EALREADY && Error!=EINTR )
            return Error==ENOENT||Error==ECONNREFUSED||Error==EAGAIN?
                MDO_CDP_SOCKET_UNAVAILABLE:MDO_CDP_SOCKET_IO;
        enum MdoCdpSocketCode Code=MdoCdpWait(Fd,POLLOUT,End);
        if ( Code!=MDO_CDP_SOCKET_OK ) return Code;
        socklen_t Size=sizeof(Error);Error=0;
        if ( getsockopt(Fd,SOL_SOCKET,SO_ERROR,&Error,&Size) || Size!=sizeof(Error) || Error )
            return MDO_CDP_SOCKET_UNAVAILABLE;
    }
    /* Linux SO_PEERCRED's fixed pid/uid/gid layout; avoid a hidden Android API
     * or assumptions about Java FileDescriptor's private fields. */
    struct {pid_t Pid;uid_t Uid;gid_t Gid;} Peer;
    socklen_t Size=sizeof(Peer);
    if ( getsockopt(Fd,SOL_SOCKET,SO_PEERCRED,&Peer,&Size) || Size!=sizeof(Peer) ||
         Peer.Pid!=getpid() || Peer.Uid!=getuid() ) return MDO_CDP_SOCKET_PEER;
    return MdoCdpCheckTime(End);
}

static enum MdoCdpSocketCode MdoCdpSend(int Fd,const char* Data,size_t Size,uint64_t End)
{
    size_t Done=0;
    while ( Done<Size ) {
        enum MdoCdpSocketCode Code=MdoCdpCheckTime(End);
        if ( Code!=MDO_CDP_SOCKET_OK ) return Code;
        ssize_t Sent=send(Fd,Data+Done,Size-Done,MSG_DONTWAIT|MSG_NOSIGNAL);
        if ( Sent>0 ) {Done+=(size_t)Sent;continue;}
        if ( Sent<0 && errno==EINTR ) continue;
        if ( Sent<0 && (errno==EAGAIN||errno==EWOULDBLOCK) ) {
            Code=MdoCdpWait(Fd,POLLOUT,End);if ( Code!=MDO_CDP_SOCKET_OK ) return Code;
        } else return MDO_CDP_SOCKET_IO;
    }
    return MDO_CDP_SOCKET_OK;
}

static enum MdoCdpSocketCode MdoCdpRead(int Fd,unsigned char* Data,size_t Size,size_t* Read,uint64_t End)
{
    for (;;) {
        enum MdoCdpSocketCode Code=MdoCdpCheckTime(End);
        if ( Code!=MDO_CDP_SOCKET_OK ) return Code;
        ssize_t Count=recv(Fd,Data,Size,MSG_DONTWAIT);
        if ( Count>0 ) {*Read=(size_t)Count;return MDO_CDP_SOCKET_OK;}
        if ( Count==0 ) return MDO_CDP_SOCKET_HTTP;
        if ( errno==EINTR ) continue;
        if ( errno!=EAGAIN && errno!=EWOULDBLOCK ) return MDO_CDP_SOCKET_IO;
        Code=MdoCdpWait(Fd,POLLIN,End);if ( Code!=MDO_CDP_SOCKET_OK ) return Code;
    }
}

static int MdoCdpEqual(const unsigned char* Data,size_t Size,const char* Text)
{
    if ( Size!=strlen(Text) ) return 0;
    for ( size_t I=0;I<Size;I++ ) {
        unsigned char Ch=Data[I];if ( Ch>='A' && Ch<='Z' ) Ch+=(unsigned char)('a'-'A');
        if ( Ch!=(unsigned char)Text[I] ) return 0;
    }
    return 1;
}

static size_t MdoCdpLine(const unsigned char* Data,size_t Start,size_t End)
{
    for ( size_t I=Start;I+1<End;I++ ) if ( Data[I]=='\r' && Data[I+1]=='\n' ) return I;
    return End;
}

static enum MdoCdpSocketCode MdoCdpHeaders(const unsigned char* Data,size_t End,size_t* Length)
{
    size_t Line=MdoCdpLine(Data,0,End);
    if ( Line<12 || (memcmp(Data,"HTTP/1.1 200",12)&&memcmp(Data,"HTTP/1.0 200",12)) ||
         (Line>12 && Data[12]!=' ') ) return MDO_CDP_SOCKET_HTTP;
    for ( size_t I=0;I<End;I++ ) {
        unsigned char Ch=Data[I];
        if ( Ch!='\r' && Ch!='\n' && Ch!='\t' && (Ch<32 || Ch>126) ) return MDO_CDP_SOCKET_HTTP;
        if ( (Ch=='\r' && (I+1>=End||Data[I+1]!='\n')) ||
             (Ch=='\n' && (!I||Data[I-1]!='\r')) ) return MDO_CDP_SOCKET_HTTP;
    }
    size_t Position=Line+2;int HaveLength=0,HaveType=0;
    while ( Position+2<=End ) {
        Line=MdoCdpLine(Data,Position,End);
        if ( Line==Position ) return Line+2==End && HaveLength && HaveType && *Length?
            MDO_CDP_SOCKET_OK:MDO_CDP_SOCKET_HTTP;
        if ( Line==End || Data[Position]==' ' || Data[Position]=='\t' ) return MDO_CDP_SOCKET_HTTP;
        size_t Colon=Position;
        while ( Colon<Line && Data[Colon]!=':' ) {
            unsigned char Ch=Data[Colon];
            if ( !((Ch>='a'&&Ch<='z')||(Ch>='A'&&Ch<='Z')||(Ch>='0'&&Ch<='9')||
                   strchr("!#$%&'*+-.^_`|~",Ch)) ) return MDO_CDP_SOCKET_HTTP;
            Colon++;
        }
        if ( Colon==Position || Colon==Line ) return MDO_CDP_SOCKET_HTTP;
        size_t Begin=Colon+1,Finish=Line;
        while ( Begin<Finish && (Data[Begin]==' '||Data[Begin]=='\t') ) Begin++;
        while ( Finish>Begin && (Data[Finish-1]==' '||Data[Finish-1]=='\t') ) Finish--;
        if ( MdoCdpEqual(Data+Position,Colon-Position,"transfer-encoding") ) return MDO_CDP_SOCKET_HTTP;
        if ( MdoCdpEqual(Data+Position,Colon-Position,"content-length") ) {
            if ( HaveLength++ || Begin==Finish ) return MDO_CDP_SOCKET_HTTP;
            size_t Value=0;
            for ( size_t I=Begin;I<Finish;I++ ) {
                unsigned char Ch=Data[I];if ( Ch<'0'||Ch>'9' ) return MDO_CDP_SOCKET_HTTP;
                if ( Value>(MDO_CDP_BODY_MAX-(size_t)(Ch-'0'))/10u ) return MDO_CDP_SOCKET_LIMIT;
                Value=Value*10u+(size_t)(Ch-'0');
            }
            *Length=Value;
        } else if ( MdoCdpEqual(Data+Position,Colon-Position,"content-type") ) {
            if ( HaveType++ || Finish-Begin<16 || !MdoCdpEqual(Data+Begin,16,"application/json") ||
                 (Finish-Begin>16 && Data[Begin+16]!=';' && Data[Begin+16]!=' ') ) return MDO_CDP_SOCKET_HTTP;
        }
        Position=Line+2;
    }
    return MDO_CDP_SOCKET_HTTP;
}

void mdoCdpDiscoveryFree(MdoCdpDiscovery* Reply)
{
    if ( !Reply ) return;
    if ( Reply->Data ) {
        volatile unsigned char* Clear=Reply->Data;
        for ( size_t I=0;I<Reply->Capacity;I++ ) Clear[I]=0;
        MDO_CDP_FREE(Reply->Data);
    }
    memset(Reply,0,sizeof(*Reply));
}

static MdoCdpDiscovery MdoCdpDiscoverFixed(int TimeoutMs,const char* Request,size_t RequestSize,size_t Maximum)
{
    MdoCdpDiscovery Reply={0};uint64_t Now;int Fd=-1;
    if ( TimeoutMs<10 || TimeoutMs>5000 ) {Reply.Code=MDO_CDP_SOCKET_ARGUMENTS;return Reply;}
    if ( !MdoCdpNow(&Now) ) {Reply.Code=MDO_CDP_SOCKET_IO;return Reply;}
    if ( Now>UINT64_MAX-(uint64_t)TimeoutMs ) {Reply.Code=MDO_CDP_SOCKET_IO;return Reply;}
    uint64_t End=Now+(uint64_t)TimeoutMs;
    unsigned char Header[MDO_CDP_HEADER_MAX];size_t Used=0,Head=0,Length=0;
    enum MdoCdpSocketCode Code=MdoCdpConnect(&Fd,End);
    if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
    Code=MdoCdpSend(Fd,Request,RequestSize,End);if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
    while ( !Head ) {
        if ( Used==sizeof(Header) ) {Code=MDO_CDP_SOCKET_LIMIT;goto Done;}
        size_t Count=0;Code=MdoCdpRead(Fd,Header+Used,sizeof(Header)-Used,&Count,End);
        if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
        size_t Before=Used;Used+=Count;
        for ( size_t I=Before>3?Before-3:0;I+3<Used;I++ )
            if ( !memcmp(Header+I,"\r\n\r\n",4) ) {Head=I+4;break;}
    }
    Code=MdoCdpHeaders(Header,Head,&Length);if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
    if ( Length>Maximum ) {Code=MDO_CDP_SOCKET_LIMIT;goto Done;}
    if ( Used-Head>Length ) {Code=MDO_CDP_SOCKET_HTTP;goto Done;}
    Reply.Capacity=Length+1;Reply.Data=MDO_CDP_ALLOC(Reply.Capacity);
    if ( !Reply.Data ) {Code=MDO_CDP_SOCKET_MEMORY;goto Done;}
    Reply.Size=Used-Head;memcpy(Reply.Data,Header+Head,Reply.Size);
    while ( Reply.Size<Length ) {
        size_t Count=0;Code=MdoCdpRead(Fd,Reply.Data+Reply.Size,Length-Reply.Size,&Count,End);
        if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
        Reply.Size+=Count;
    }
    Reply.Data[Length]=0;Code=MdoCdpCheckTime(End);
Done:
    if ( Fd>=0 ) (void)close(Fd); /* Never retry close after EINTR on Linux. */
    volatile unsigned char* ClearHeader=Header;
    for ( size_t I=0;I<sizeof(Header);I++ ) ClearHeader[I]=0;
    if ( Code!=MDO_CDP_SOCKET_OK ) mdoCdpDiscoveryFree(&Reply);
    Reply.Code=Code;return Reply;
}

MdoCdpDiscovery mdoCdpDiscover(int TimeoutMs)
{
    static const char Request[]="GET /json/list HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nAccept: application/json\r\n\r\n";
    return MdoCdpDiscoverFixed(TimeoutMs,Request,sizeof(Request)-1u,MDO_CDP_BODY_MAX);
}
MdoCdpDiscovery mdoCdpDiscoverBrowser(int TimeoutMs)
{
    static const char Request[]="GET /json/version HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nAccept: application/json\r\n\r\n";
    return MdoCdpDiscoverFixed(TimeoutMs,Request,sizeof(Request)-1u,MDO_CDP_BROWSER_DISCOVERY_MAX);
}

const char* mdoCdpSocketCode(enum MdoCdpSocketCode Code)
{
    switch ( Code ) {
    case MDO_CDP_SOCKET_OK:return "cdp_discovery_ready";
    case MDO_CDP_SOCKET_ARGUMENTS:return "cdp_arguments_invalid";
    case MDO_CDP_SOCKET_UNAVAILABLE:return "cdp_local_unavailable";
    case MDO_CDP_SOCKET_PEER:return "cdp_peer_mismatch";
    case MDO_CDP_SOCKET_TIMEOUT:return "cdp_local_timeout";
    case MDO_CDP_SOCKET_HTTP:return "cdp_http_invalid";
    case MDO_CDP_SOCKET_LIMIT:return "cdp_response_limit";
    case MDO_CDP_SOCKET_MEMORY:return "cdp_allocation_failed";
    case MDO_CDP_SOCKET_CLOSED:return "cdp_session_closed";
    case MDO_CDP_SOCKET_PROTOCOL:return "cdp_websocket_invalid";
    case MDO_CDP_SOCKET_BUSY:return "cdp_session_busy";
    case MDO_CDP_SOCKET_RANDOM:return "cdp_random_unavailable";
    case MDO_CDP_SOCKET_EXPIRED:return "cdp_message_timeout";
    case MDO_CDP_SOCKET_JSON:return "cdp_json_invalid";
    default:return "cdp_local_io_failed";
    }
}
