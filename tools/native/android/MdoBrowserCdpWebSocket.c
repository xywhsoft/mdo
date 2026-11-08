/* Compiled once, through the private JNI entry. Reuse the pinned XRT RFC 6455
 * handshake/frame/message/UTF-8 primitives; no second protocol implementation. */
#include "MdoBrowserCdpWebSocket.h"
#include "MdoBrowserCdpSocket.c"
#include "xrt.h"
#include <pthread.h>

/* Eight physical network owners plus eight document bindings; one extra slot
 * covers the single shared worker's proof/retirement overlap. Still a hard
 * process-wide limit including draining handles, never an unbounded pool. */
#define MDO_CDP_WS_VIEWS 8u
#define MDO_CDP_WS_MAX (2u*MDO_CDP_WS_VIEWS+1u)
#define MDO_CDP_WS_INPUT 8192u
#define MDO_CDP_WS_MESSAGE 4194304u
#define MDO_CDP_WS_SEND_MAX 262144u
#define MDO_CDP_WS_ASSEMBLY_MS 10000u
#ifndef MDO_CDP_RANDOM
#define MDO_CDP_RANDOM xrtSecureRandom
#endif

typedef struct MdoCdpWs {
    int Fd;
    pthread_mutex_t Io;
    unsigned char Input[MDO_CDP_WS_INPUT],Head[XWS_FRAME_HEAD_MAX],Control[125];
    size_t Begin,End,HeadSize,ControlSize;
    uint64_t Remaining,AssemblyEnd,FrameEnd;
    int InFrame,WriteFailed;
    xwsframe Frame;
    xwsmessagestate Message;
    MdoCdpDiscovery Text;
} MdoCdpWs;
typedef struct MdoCdpWsSlot {
    uint64_t Handle;
    MdoCdpWs* Session;
    unsigned Refs;
    int Reserved,Closing;
} MdoCdpWsSlot;
static pthread_mutex_t MdoCdpWsGate=PTHREAD_MUTEX_INITIALIZER;
static MdoCdpWsSlot MdoCdpWsSlots[MDO_CDP_WS_MAX];
static uint64_t MdoCdpWsSerial;

static void MdoCdpClear(void* Data,size_t Size)
{
    volatile unsigned char* Bytes=Data;
    for ( size_t I=0;I<Size;I++ ) Bytes[I]=0;
}
static void MdoCdpWsFree(MdoCdpWs* Session)
{
    if ( !Session ) return;
    if ( Session->Fd>=0 ) (void)close(Session->Fd);
    (void)pthread_mutex_destroy(&Session->Io);
    mdoCdpDiscoveryFree(&Session->Text);
    MdoCdpClear(Session,sizeof(*Session));MDO_CDP_FREE(Session);
}
static MdoCdpWsSlot* MdoCdpWsBorrow(uint64_t Handle)
{
    MdoCdpWsSlot* Found=NULL;
    pthread_mutex_lock(&MdoCdpWsGate);
    for ( size_t I=0;I<MDO_CDP_WS_MAX;I++ ) {
        MdoCdpWsSlot* Slot=&MdoCdpWsSlots[I];
        if ( Handle && Slot->Handle==Handle && Slot->Session && !Slot->Closing ) {
            Slot->Refs++;Found=Slot;break;
        }
    }
    pthread_mutex_unlock(&MdoCdpWsGate);return Found;
}
/* Retire while holding the registry gate. shutdown wakes I/O; close/free wait
 * for every borrower. Therefore neither descriptor nor slot can be reused by
 * a blocked reader, a concurrent cancellation or a stale Java handle. */
static void MdoCdpWsRetire(MdoCdpWsSlot* Slot)
{
    if ( !Slot->Closing ) {Slot->Closing=1;(void)shutdown(Slot->Session->Fd,SHUT_RDWR);}
}
static enum MdoCdpSocketCode MdoCdpWsReturn(MdoCdpWsSlot* Slot,enum MdoCdpSocketCode Code,int Retire,int Locked)
{
    MdoCdpWs* Release=NULL;
    pthread_mutex_lock(&MdoCdpWsGate);
    if ( Slot->Closing ) Code=MDO_CDP_SOCKET_CLOSED;
    if ( Retire ) MdoCdpWsRetire(Slot);
    /* Publish retirement before releasing I/O ownership. A new borrower must
     * never write between a failed frame and retirement of that byte stream. */
    if ( Locked ) pthread_mutex_unlock(&Slot->Session->Io);
    Slot->Refs--;
    if ( Slot->Closing && !Slot->Refs ) {Release=Slot->Session;memset(Slot,0,sizeof(*Slot));}
    pthread_mutex_unlock(&MdoCdpWsGate);MdoCdpWsFree(Release);return Code;
}
void mdoCdpWsClose(uint64_t Handle)
{
    MdoCdpWs* Release=NULL;
    pthread_mutex_lock(&MdoCdpWsGate);
    for ( size_t I=0;I<MDO_CDP_WS_MAX;I++ ) {
        MdoCdpWsSlot* Slot=&MdoCdpWsSlots[I];
        if ( Handle && Slot->Handle==Handle && Slot->Session ) {
            MdoCdpWsRetire(Slot);
            if ( !Slot->Refs ) {Release=Slot->Session;memset(Slot,0,sizeof(*Slot));}
            break;
        }
    }
    pthread_mutex_unlock(&MdoCdpWsGate);MdoCdpWsFree(Release);
}
static int MdoCdpWsDeadline(int TimeoutMs,uint64_t* End)
{
    uint64_t Now;
    if ( TimeoutMs<10 || TimeoutMs>5000 || !MdoCdpNow(&Now) || Now>UINT64_MAX-10000u ) return 0;
    *End=Now+(uint64_t)TimeoutMs;return 1;
}
static int MdoCdpWsTarget(const char* Id,size_t Size)
{
    if ( !Id || !Size || Size>128 ) return 0;
    for ( size_t I=0;I<Size;I++ ) {
        unsigned char Ch=(unsigned char)Id[I];
        if ( !((Ch>='a'&&Ch<='z')||(Ch>='A'&&Ch<='Z')||(Ch>='0'&&Ch<='9')||Ch=='-'||Ch=='_') ) return 0;
    }
    return 1;
}
static int MdoCdpWsBrowser(const char* Path,size_t Size)
{
    static const char Base[]="/devtools/browser";size_t Prefix=sizeof(Base)-1;
    if ( !Path || Size<Prefix || memcmp(Path,Base,Prefix) ) return 0;
    if ( Size==Prefix ) return 1;
    if ( Size!=Prefix+37 || Path[Prefix]!='/' ) return 0;
    for ( size_t I=0;I<36;I++ ) {
        unsigned char Ch=(unsigned char)Path[Prefix+1+I];
        if ( I==8 || I==13 || I==18 || I==23 ) {if ( Ch!='-' ) return 0;}
        else if ( !((Ch>='0'&&Ch<='9')||(Ch>='a'&&Ch<='f')||(Ch>='A'&&Ch<='F')) ) return 0;
    }
    return 1;
}
static enum MdoCdpSocketCode MdoCdpWsHeaders(const unsigned char* Data,size_t End,const char* Key)
{
    size_t Line=MdoCdpLine(Data,0,End);
    if ( Line<12 || memcmp(Data,"HTTP/1.1 101",12) || (Line>12&&Data[12]!=' ') ) return MDO_CDP_SOCKET_HTTP;
    for ( size_t I=0;I<End;I++ ) {
        unsigned char Ch=Data[I];
        if ( Ch!='\r'&&Ch!='\n'&&Ch!='\t'&&(Ch<32||Ch>126) ) return MDO_CDP_SOCKET_HTTP;
        if ( (Ch=='\r'&&(I+1>=End||Data[I+1]!='\n')) || (Ch=='\n'&&(!I||Data[I-1]!='\r')) ) return MDO_CDP_SOCKET_HTTP;
    }
    int Upgrade=0,Connection=0,Accept=0;size_t Position=Line+2;
    while ( Position+2<=End ) {
        Line=MdoCdpLine(Data,Position,End);
        if ( Line==Position ) return Line+2==End&&Upgrade&&Connection&&Accept?MDO_CDP_SOCKET_OK:MDO_CDP_SOCKET_HTTP;
        if ( Line==End || Data[Position]==' ' || Data[Position]=='\t' ) return MDO_CDP_SOCKET_HTTP;
        size_t Colon=Position;
        while ( Colon<Line && Data[Colon]!=':' ) {
            unsigned char Ch=Data[Colon];
            if ( !((Ch>='a'&&Ch<='z')||(Ch>='A'&&Ch<='Z')||(Ch>='0'&&Ch<='9')||strchr("!#$%&'*+-.^_`|~",Ch)) ) return MDO_CDP_SOCKET_HTTP;
            Colon++;
        }
        if ( Colon==Position || Colon==Line ) return MDO_CDP_SOCKET_HTTP;
        size_t Begin=Colon+1,Finish=Line;
        while ( Begin<Finish&&(Data[Begin]==' '||Data[Begin]=='\t') ) Begin++;
        while ( Finish>Begin&&(Data[Finish-1]==' '||Data[Finish-1]=='\t') ) Finish--;
        if ( MdoCdpEqual(Data+Position,Colon-Position,"upgrade") ) {
            if ( Upgrade++ || !MdoCdpEqual(Data+Begin,Finish-Begin,"websocket") ) return MDO_CDP_SOCKET_HTTP;
        } else if ( MdoCdpEqual(Data+Position,Colon-Position,"connection") ) {
            if ( Connection++ || !MdoCdpEqual(Data+Begin,Finish-Begin,"upgrade") ) return MDO_CDP_SOCKET_HTTP;
        } else if ( MdoCdpEqual(Data+Position,Colon-Position,"sec-websocket-accept") ) {
            if ( Accept++ || !xrtWsAcceptValid((xstrview){Key,XWS_KEY_SIZE},
                (xstrview){(const char*)Data+Begin,Finish-Begin}) ) return MDO_CDP_SOCKET_HTTP;
        } else if ( MdoCdpEqual(Data+Position,Colon-Position,"content-length") ||
                    MdoCdpEqual(Data+Position,Colon-Position,"transfer-encoding") ||
                    MdoCdpEqual(Data+Position,Colon-Position,"sec-websocket-extensions") ||
                    MdoCdpEqual(Data+Position,Colon-Position,"sec-websocket-protocol") ) return MDO_CDP_SOCKET_HTTP;
        Position=Line+2;
    }
    return MDO_CDP_SOCKET_HTTP;
}
static enum MdoCdpSocketCode MdoCdpWsOpenKind(const char* Id,size_t Size,int TimeoutMs,uint64_t* Handle,int Browser)
{
    if ( Handle ) *Handle=0;
    uint64_t End;
    if ( !Handle || !(Browser?MdoCdpWsBrowser(Id,Size):MdoCdpWsTarget(Id,Size)) || !MdoCdpWsDeadline(TimeoutMs,&End) ) return MDO_CDP_SOCKET_ARGUMENTS;
    MdoCdpWsSlot* Slot=NULL;
    pthread_mutex_lock(&MdoCdpWsGate);
    if ( MdoCdpWsSerial<INT64_MAX ) for ( size_t I=0;I<MDO_CDP_WS_MAX;I++ ) if ( !MdoCdpWsSlots[I].Reserved ) {
        Slot=&MdoCdpWsSlots[I];Slot->Reserved=1;Slot->Handle=++MdoCdpWsSerial;break;
    }
    pthread_mutex_unlock(&MdoCdpWsGate);
    if ( !Slot ) return MDO_CDP_SOCKET_LIMIT;
    MdoCdpWs* Session=MDO_CDP_ALLOC(sizeof(*Session));enum MdoCdpSocketCode Code=MDO_CDP_SOCKET_MEMORY;
    unsigned char Header[MDO_CDP_HEADER_MAX],Random[XWS_KEY_BYTES];
    char Key[XWS_KEY_CAPACITY],Request[512];size_t Used=0,Head=0;
    if ( !Session ) goto Done;
    memset(Session,0,sizeof(*Session));Session->Fd=-1;
    if ( pthread_mutex_init(&Session->Io,NULL) ) {
        MdoCdpClear(Session,sizeof(*Session));MDO_CDP_FREE(Session);Session=NULL;Code=MDO_CDP_SOCKET_IO;goto Done;
    }
    xwsmessageconfig Config;xrtWsMessageConfigInitSafe(&Config);Config.MaxSize=MDO_CDP_WS_MESSAGE;
    if ( !xrtWsMessageInit(&Session->Message,&Config) ) {Code=MDO_CDP_SOCKET_PROTOCOL;goto Done;}
    Code=MdoCdpConnect(&Session->Fd,End);if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
    if ( !MDO_CDP_RANDOM(Random,sizeof(Random)) ) {Code=MDO_CDP_SOCKET_RANDOM;goto Done;}
    size_t KeySize=0;
    if ( !xrtBase64Encode(Random,sizeof(Random),Key,sizeof(Key),&KeySize,NULL) || KeySize!=XWS_KEY_SIZE ) {Code=MDO_CDP_SOCKET_PROTOCOL;goto Done;}
    int Length=snprintf(Request,sizeof(Request),"GET %s%.*s HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",Browser?"":"/devtools/page/",(int)Size,Id,Key);
    if ( Length<1 || (size_t)Length>=sizeof(Request) ) {Code=MDO_CDP_SOCKET_ARGUMENTS;goto Done;}
    Code=MdoCdpSend(Session->Fd,Request,(size_t)Length,End);if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
    while ( !Head ) {
        if ( Used==sizeof(Header) ) {Code=MDO_CDP_SOCKET_LIMIT;goto Done;}
        size_t Count=0;Code=MdoCdpRead(Session->Fd,Header+Used,sizeof(Header)-Used,&Count,End);
        if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
        size_t Before=Used;Used+=Count;
        for ( size_t I=Before>3?Before-3:0;I+3<Used;I++ ) if ( !memcmp(Header+I,"\r\n\r\n",4) ) {Head=I+4;break;}
    }
    Code=MdoCdpWsHeaders(Header,Head,Key);if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
    Session->End=Used-Head;memcpy(Session->Input,Header+Head,Session->End);
    Code=MdoCdpCheckTime(End);if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
    pthread_mutex_lock(&MdoCdpWsGate);Slot->Session=Session;*Handle=Slot->Handle;pthread_mutex_unlock(&MdoCdpWsGate);
    Session=NULL;
Done:
    MdoCdpClear(Header,sizeof(Header));MdoCdpClear(Random,sizeof(Random));
    MdoCdpClear(Key,sizeof(Key));MdoCdpClear(Request,sizeof(Request));
    MdoCdpWsFree(Session);
    if ( Code!=MDO_CDP_SOCKET_OK ) {pthread_mutex_lock(&MdoCdpWsGate);memset(Slot,0,sizeof(*Slot));pthread_mutex_unlock(&MdoCdpWsGate);}
    return Code;
}
enum MdoCdpSocketCode mdoCdpWsOpen(const char* Id,size_t Size,int TimeoutMs,uint64_t* Handle)
{return MdoCdpWsOpenKind(Id,Size,TimeoutMs,Handle,0);}
enum MdoCdpSocketCode mdoCdpWsOpenBrowser(const char* Path,size_t Size,int TimeoutMs,uint64_t* Handle)
{return MdoCdpWsOpenKind(Path,Size,TimeoutMs,Handle,1);}
static enum MdoCdpSocketCode MdoCdpWsWrite(MdoCdpWs* Session,unsigned char Opcode,const unsigned char* Data,size_t Size,uint64_t End)
{
    unsigned char Head[XWS_FRAME_HEAD_MAX],Chunk[4096];size_t HeadSize=0;
    xwsframe Frame={0};Frame.Opcode=Opcode;Frame.Flags=XWS_FRAME_FIN|XWS_FRAME_MASKED;Frame.PayloadSize=Size;
    enum MdoCdpSocketCode Code=MDO_CDP_SOCKET_RANDOM;
    if ( !MDO_CDP_RANDOM(Frame.Mask,sizeof(Frame.Mask)) ) goto Done;
    if ( !xrtWsFrameWrite(&Frame,NULL,Head,sizeof(Head),&HeadSize) ) {Code=MDO_CDP_SOCKET_PROTOCOL;goto Done;}
    Code=MdoCdpSend(Session->Fd,(const char*)Head,HeadSize,End);if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
    for ( size_t Position=0;Position<Size; ) {
        size_t Count=Size-Position;if ( Count>sizeof(Chunk) ) Count=sizeof(Chunk);
        memcpy(Chunk,Data+Position,Count);
        if ( !xrtWsMask(Chunk,Count,Frame.Mask,Position) ) {Code=MDO_CDP_SOCKET_PROTOCOL;goto Done;}
        Code=MdoCdpSend(Session->Fd,(const char*)Chunk,Count,End);if ( Code!=MDO_CDP_SOCKET_OK ) goto Done;
        Position+=Count;
    }
Done:
    MdoCdpClear(Head,sizeof(Head));MdoCdpClear(Chunk,sizeof(Chunk));MdoCdpClear(&Frame,sizeof(Frame));return Code;
}
enum MdoCdpSocketCode mdoCdpWsSend(uint64_t Handle,const unsigned char* Data,size_t Size,int TimeoutMs)
{
    uint64_t End;
    if ( !Data || !Size || Size>MDO_CDP_WS_SEND_MAX || !MdoCdpWsDeadline(TimeoutMs,&End) ) return MDO_CDP_SOCKET_ARGUMENTS;
    /* Validate the complete caller text before any frame byte can be sent. */
    xwsmessagestate State;xwsmessageconfig Config;xwsmessageinfo Info;xwsframe Frame={0};
    xrtWsMessageConfigInitSafe(&Config);Config.MaxSize=MDO_CDP_WS_SEND_MAX;
    Frame.Opcode=XWS_OPCODE_TEXT;Frame.Flags=XWS_FRAME_FIN;Frame.PayloadSize=Size;
    if ( !xrtWsMessageInit(&State,&Config) || !xrtWsMessageFrameBegin(&State,&Frame,&Info,NULL) ||
         !xrtWsMessagePayload(&State,(xbytesview){Data,Size},NULL) || !xrtWsMessageFrameEnd(&State,NULL) ) return MDO_CDP_SOCKET_ARGUMENTS;
    MdoCdpWsSlot* Slot=MdoCdpWsBorrow(Handle);if ( !Slot ) return MDO_CDP_SOCKET_CLOSED;
    MdoCdpWs* Session=Slot->Session;
    if ( pthread_mutex_trylock(&Session->Io) ) return MdoCdpWsReturn(Slot,MDO_CDP_SOCKET_BUSY,0,0);
    enum MdoCdpSocketCode Code=MdoCdpWsWrite(Session,XWS_OPCODE_TEXT,Data,Size,End);
    return MdoCdpWsReturn(Slot,Code,Code!=MDO_CDP_SOCKET_OK,1);
}
static enum MdoCdpSocketCode MdoCdpWsFill(MdoCdpWs* Session,uint64_t End)
{
    if ( Session->Begin<Session->End ) return MDO_CDP_SOCKET_OK;
    MdoCdpClear(Session->Input,sizeof(Session->Input));Session->Begin=Session->End=0;
    enum MdoCdpSocketCode Code=MdoCdpRead(Session->Fd,Session->Input,sizeof(Session->Input),&Session->End,End);
    return Code==MDO_CDP_SOCKET_HTTP?MDO_CDP_SOCKET_CLOSED:Code;
}
static enum MdoCdpSocketCode MdoCdpWsAppend(MdoCdpWs* Session,const unsigned char* Data,size_t Size)
{
    MdoCdpDiscovery* Text=&Session->Text;
    if ( Size>MDO_CDP_WS_MESSAGE-Text->Size ) return MDO_CDP_SOCKET_LIMIT;
    size_t Need=Text->Size+Size+1;
    if ( Need>Text->Capacity ) {
        size_t Capacity=Text->Capacity?Text->Capacity:4096;
        while ( Capacity<Need ) Capacity=Capacity>(MDO_CDP_WS_MESSAGE+1)/2?MDO_CDP_WS_MESSAGE+1:Capacity*2;
        unsigned char* Buffer=MDO_CDP_ALLOC(Capacity);if ( !Buffer ) return MDO_CDP_SOCKET_MEMORY;
        if ( Text->Size ) memcpy(Buffer,Text->Data,Text->Size);
        size_t Before=Text->Size;mdoCdpDiscoveryFree(Text);Text->Data=Buffer;Text->Capacity=Capacity;Text->Size=Before;
    }
    if ( Size ) memcpy(Text->Data+Text->Size,Data,Size);
    Text->Size+=Size;Text->Data[Text->Size]=0;return MDO_CDP_SOCKET_OK;
}
static enum MdoCdpSocketCode MdoCdpWsReadMessage(MdoCdpWs* Session,uint64_t End,MdoCdpDiscovery* Out)
{
    for (;;) {
        uint64_t Now;if ( !MdoCdpNow(&Now) ) return MDO_CDP_SOCKET_IO;
        uint64_t Limit=End;
        if ( Session->AssemblyEnd && Session->AssemblyEnd<Limit ) Limit=Session->AssemblyEnd;
        if ( Session->FrameEnd && Session->FrameEnd<Limit ) Limit=Session->FrameEnd;
        if ( Now>=Limit ) return MDO_CDP_SOCKET_TIMEOUT;
        if ( !Session->InFrame ) {
            enum MdoCdpSocketCode Code=MdoCdpWsFill(Session,Limit);if ( Code!=MDO_CDP_SOCKET_OK ) return Code;
            if ( !Session->FrameEnd ) Session->FrameEnd=Now+MDO_CDP_WS_ASSEMBLY_MS;
            Session->Head[Session->HeadSize++]=Session->Input[Session->Begin++];
            xwsframeconfig Config;xrtWsFrameConfigInit(&Config);Config.MaxPayload=MDO_CDP_WS_MESSAGE;Config.Mask=XWS_MASK_FORBIDDEN;
            xwsframestatus Status=xrtWsFrameParse((xbytesview){Session->Head,Session->HeadSize},&Session->Frame,&Config,NULL);
            if ( Status==XWS_FRAME_ERROR ) return MDO_CDP_SOCKET_PROTOCOL;
            if ( Status==XWS_FRAME_MORE ) {
                if ( Session->HeadSize==sizeof(Session->Head) ) return MDO_CDP_SOCKET_PROTOCOL;
                continue;
            }
            xwsmessageinfo Info;
            if ( Session->Frame.Opcode==XWS_OPCODE_BINARY || !xrtWsMessageFrameBegin(&Session->Message,&Session->Frame,&Info,NULL) ) return MDO_CDP_SOCKET_PROTOCOL;
            Session->InFrame=1;Session->Remaining=Session->Frame.PayloadSize;Session->ControlSize=0;
            if ( Session->Frame.Opcode==XWS_OPCODE_TEXT && !Session->AssemblyEnd ) Session->AssemblyEnd=Session->FrameEnd;
        }
        if ( Session->Remaining ) {
            enum MdoCdpSocketCode Code=MdoCdpWsFill(Session,Limit);if ( Code!=MDO_CDP_SOCKET_OK ) return Code;
            size_t Count=Session->End-Session->Begin;if ( (uint64_t)Count>Session->Remaining ) Count=(size_t)Session->Remaining;
            unsigned char* Data=Session->Input+Session->Begin;
            if ( !xrtWsMessagePayload(&Session->Message,(xbytesview){Data,Count},NULL) ) return MDO_CDP_SOCKET_PROTOCOL;
            if ( Session->Frame.Opcode>=XWS_OPCODE_CLOSE ) {
                if ( Count>sizeof(Session->Control)-Session->ControlSize ) return MDO_CDP_SOCKET_PROTOCOL;
                memcpy(Session->Control+Session->ControlSize,Data,Count);Session->ControlSize+=Count;
            } else {
                Code=MdoCdpWsAppend(Session,Data,Count);if ( Code!=MDO_CDP_SOCKET_OK ) return Code;
            }
            MdoCdpClear(Data,Count);Session->Begin+=Count;Session->Remaining-=Count;
            if ( Session->Remaining ) continue;
        }
        if ( !xrtWsMessageFrameEnd(&Session->Message,NULL) ) return MDO_CDP_SOCKET_PROTOCOL;
        unsigned char Opcode=Session->Frame.Opcode;
        int Complete=Opcode<XWS_OPCODE_CLOSE && (Session->Frame.Flags&XWS_FRAME_FIN);
        Session->InFrame=0;Session->HeadSize=0;Session->FrameEnd=0;MdoCdpClear(Session->Head,sizeof(Session->Head));
        if ( Opcode==XWS_OPCODE_CLOSE || Opcode==XWS_OPCODE_PING ) {
            enum MdoCdpSocketCode Code=MdoCdpWsWrite(Session,Opcode==XWS_OPCODE_PING?XWS_OPCODE_PONG:XWS_OPCODE_CLOSE,Session->Control,Session->ControlSize,Limit);
            MdoCdpClear(Session->Control,sizeof(Session->Control));Session->ControlSize=0;
            if ( Opcode==XWS_OPCODE_CLOSE ) return MDO_CDP_SOCKET_CLOSED;
            if ( Code!=MDO_CDP_SOCKET_OK ) {Session->WriteFailed=1;return Code;}
        } else {MdoCdpClear(Session->Control,sizeof(Session->Control));Session->ControlSize=0;}
        if ( Complete ) {
            enum MdoCdpSocketCode Code=MdoCdpWsAppend(Session,NULL,0);if ( Code!=MDO_CDP_SOCKET_OK ) return Code;
            *Out=Session->Text;memset(&Session->Text,0,sizeof(Session->Text));Session->AssemblyEnd=0;return MDO_CDP_SOCKET_OK;
        }
    }
}
MdoCdpDiscovery mdoCdpWsReceive(uint64_t Handle,int TimeoutMs)
{
    MdoCdpDiscovery Out={0};uint64_t End;
    if ( !MdoCdpWsDeadline(TimeoutMs,&End) ) {Out.Code=MDO_CDP_SOCKET_ARGUMENTS;return Out;}
    MdoCdpWsSlot* Slot=MdoCdpWsBorrow(Handle);if ( !Slot ) {Out.Code=MDO_CDP_SOCKET_CLOSED;return Out;}
    MdoCdpWs* Session=Slot->Session;
    if ( pthread_mutex_trylock(&Session->Io) ) {Out.Code=MdoCdpWsReturn(Slot,MDO_CDP_SOCKET_BUSY,0,0);return Out;}
    enum MdoCdpSocketCode Code=MdoCdpWsReadMessage(Session,End,&Out);uint64_t Now=0;
    int Retire=Code!=MDO_CDP_SOCKET_OK && Code!=MDO_CDP_SOCKET_TIMEOUT;
    if ( Code==MDO_CDP_SOCKET_TIMEOUT ) Retire=!MdoCdpNow(&Now) || (Session->AssemblyEnd&&Now>=Session->AssemblyEnd) || (Session->FrameEnd&&Now>=Session->FrameEnd);
    if ( Code==MDO_CDP_SOCKET_TIMEOUT && (Retire||Session->WriteFailed) ) Code=MDO_CDP_SOCKET_EXPIRED;
    Code=MdoCdpWsReturn(Slot,Code,Retire||Session->WriteFailed,1);
    if ( Code!=MDO_CDP_SOCKET_OK ) mdoCdpDiscoveryFree(&Out);
    Out.Code=Code;return Out;
}
