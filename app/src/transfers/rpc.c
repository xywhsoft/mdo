#include "../../include/mdo/time.h"
#include "internal.h"

static bool MdoDtPacketId(cstr Text,uint8 Bytes[16]) {
    if (!MdoDtId(Text)) return false;
    for (size_t i=0;i<16u;i++) {
        unsigned A=(unsigned char)Text[2u*i],B=(unsigned char)Text[2u*i+1u];
        Bytes[i]=(uint8)(((A<='9'?A-'0':A-'a'+10u)<<4u)|(B<='9'?B-'0':B-'a'+10u));
    }
    return true;
}
static uint64 MdoDtPacketRead64(const uint8* Bytes) {
    uint64 Value=0; for (unsigned i=0;i<8u;i++) Value=(Value<<8u)|Bytes[i]; return Value;
}
static void MdoDtPacketWrite64(uint8* Bytes,uint64 Value) {
    for (unsigned i=0;i<8u;i++) { Bytes[7u-i]=(uint8)Value; Value>>=8u; }
}

static bool MdoDtRpcText(MdoDtRpc* Rpc,xvalue* Value) {
    size_t Bytes=0; str Text=xrtJsonStringify(Value,false,&Bytes);
    bool Ok=Text && MdoRemoteSocketSend(Rpc->DirectReady?Rpc->Direct:Rpc->Socket,false,(xbytesview){(uint8*)Text,Bytes});
    xrtFree(Text); xrtValueRelease(Value); return Ok;
}
static bool MdoDtRpcAck(MdoDtRpc* Rpc) {
    xvalue* Value=xrtValueObject();
    if (!Value || !MdoAccountSetString(Value,"type","download_ack") || !MdoAccountSetString(Value,"id",Rpc->Request) ||
        !MdoAccountSetUInt(Value,"offset",Rpc->Received)) { xrtValueRelease(Value); return false; }
    return MdoDtRpcText(Rpc,Value);
}
static bool MdoDtRpcMessage(bool Binary,xbytesview Message,void* Data) {
    MdoDtRpc* Rpc=Data;
    if (Binary) {
        const uint8* Bytes=Message.Data; uint8 Id[16];
        if (!Rpc->Hello || Message.Size<=29u || Message.Size>65536u+29u || memcmp(Bytes,"MDP1",4u) || Bytes[4]!=2u) return false;
        if (!MdoDtPacketId(Rpc->Request,Id) || memcmp(Bytes+5,Id,16u) || MdoDtPacketRead64(Bytes+21)!=Rpc->Received ||
            Message.Size-29u>262144u-Rpc->ResponseBytes) return false;
        memcpy(Rpc->Response+Rpc->ResponseBytes,Bytes+29,Message.Size-29u);
        Rpc->ResponseBytes+=Message.Size-29u; Rpc->Response[Rpc->ResponseBytes]=0;
        Rpc->Received+=Message.Size-29u; return MdoDtRpcAck(Rpc);
    }
    xjsonreadconfig Config; xrtJsonReadConfigInit(&Config); Config.MaxInputBytes=16384u;
    Config.MaxValues=256u; Config.MaxDepth=5u; Config.MaxStringBytes=2048u;
    xvalue* Value=xrtJsonRead(xrtStrViewN((cstr)Message.Data,Message.Size),&Config);
    cstr Type=MdoAccountText(Value,"type",32u),Id=MdoAccountText(Value,"id",32u); bool Ok=false;
    if (!Type) goto done;
    if (!Rpc->Ready && !strcmp(Type,"ready")) {
        cstr Device=MdoAccountText(Value,"device_id",32u),Mode=MdoAccountText(Value,"mode",16u);
        Rpc->Ready=Device && Mode && !strcmp(Device,Rpc->Device) && !strcmp(Mode,"control");
        Ok=Rpc->Ready; goto done;
    }
    if (Rpc->Ready && !Rpc->Hello && !strcmp(Type,"hello")) {
        cstr Runtime=MdoAccountText(Value,"runtime_id",32u),Mode=MdoAccountText(Value,"mode",16u); uint64 Version=0;
        if (!MdoDtId(Runtime) || !Mode || strcmp(Mode,"control") ||
            !MdoAccountGetUInt(xrtValueObjectGet(Value,XRT_STR_LITERAL("device_transfer")),&Version) || Version!=1u) {
            Rpc->Status=426u; goto done;
        }
        strcpy(Rpc->Runtime,Runtime); Rpc->Hello=true;
        Rpc->Offer=xrtValueClone(xrtValueObjectGet(Value,XRT_STR_LITERAL("direct")));
        Ok=true; goto done;
    }
    if (Rpc->Direct && !Rpc->DirectReady && !strcmp(Type,"direct_ready")) {
        cstr Runtime=MdoAccountText(Value,"runtime_id",32u),Peer=MdoAccountText(Value,"peer_id",32u);
        cstr Expected=MdoAccountText(Rpc->Offer,"peer_id",32u),Mode=MdoAccountText(Value,"mode",16u);
        Rpc->DirectReady=Runtime && Peer && Expected && Mode && !strcmp(Runtime,Rpc->Runtime) &&
            !strcmp(Peer,Expected) && !strcmp(Mode,"control"); Ok=Rpc->DirectReady; goto done;
    }
    if (!Id || strcmp(Id,Rpc->Request)) goto done;
    if (!strcmp(Type,"request_ready") || !strcmp(Type,"upload_ack")) {
        uint64 Offset=0;
        if (!MdoAccountGetUInt(xrtValueObjectGet(Value,XRT_STR_LITERAL("offset")),&Offset) || Offset!=Rpc->Sent) goto done;
        size_t Part=Rpc->UploadBytes-(size_t)Offset; if (Part>65536u) Part=65536u;
        if (!Part) { Ok=true; goto done; }
        uint8* Packet=xrtMalloc(Part+29u); if (!Packet) goto done;
        memcpy(Packet,"MDP1",4); Packet[4]=1u; MdoDtPacketId(Rpc->Request,Packet+5); MdoDtPacketWrite64(Packet+21,Offset);
        memcpy(Packet+29,Rpc->Upload+Offset,Part);
        Ok=MdoRemoteSocketSend(Rpc->DirectReady?Rpc->Direct:Rpc->Socket,true,(xbytesview){Packet,Part+29u});
        xrtFree(Packet); Rpc->Sent+=Part;
    } else if (!strcmp(Type,"response")) {
        uint64 Status=0;
        if (!MdoAccountGetUInt(xrtValueObjectGet(Value,XRT_STR_LITERAL("status")),&Status) || Status<200u || Status>599u) goto done;
        Rpc->Status=(uint16)Status;
        const xvalue* Headers=xrtValueObjectGet(Value,XRT_STR_LITERAL("headers"));
        for (size_t i=0;i<xrtValueCount(Headers);i++) {
            const xvalue* Header=xrtValueArrayGet(Headers,i); xstrview Key,Text;
            if (xrtValueGetString(xrtValueArrayGet(Header,0),&Key) &&
                xrtValueGetString(xrtValueArrayGet(Header,1),&Text) &&
                Key.Size==17u && !memcmp(Key.Data,"X-Mdo-Write-Token",17u) && Text.Size<sizeof(Rpc->Token)) {
                memcpy(Rpc->Token,Text.Data,Text.Size); Rpc->Token[Text.Size]=0;
            }
        }
        Ok=true;
    } else if (!strcmp(Type,"end")) {
        uint64 Bytes=0; Ok=Rpc->Status && MdoAccountGetUInt(xrtValueObjectGet(Value,XRT_STR_LITERAL("bytes")),&Bytes) && Bytes==Rpc->Received;
        Rpc->Done=Ok;
    }
done:
    xrtValueRelease(Value); return Ok;
}
static bool MdoDtRpcPoll(MdoDtRpc* Rpc) {
    if (xrtCancelRequested(Rpc->Cancel) || xrtCancelRequested(Rpc->Lease.Cancel) ||
        !MdoRemoteSocketPoll(Rpc->Socket,MdoDtRpcMessage,Rpc)) return false;
    return !Rpc->Direct || MdoRemoteSocketPoll(Rpc->Direct,MdoDtRpcMessage,Rpc);
}
void MdoDtRpcClose(MdoDtRpc* Rpc) {
    MdoRemoteSocketDestroy(Rpc->Direct); MdoRemoteSocketDestroy(Rpc->Socket);
    MdoRemoteNetUnit(&Rpc->DirectNet); MdoRemoteNetUnit(&Rpc->Net);
    MdoAccountSecretValueRelease(Rpc->Offer); MdoAccountRelease(&Rpc->Lease);
    xrtFree(Rpc->Response); memset(Rpc,0,sizeof(*Rpc));
}
bool MdoDtRpcConnect(MdoDtRpc* Rpc,XS_ServerInfo* Server,cstr Device,uint64 Member,xcancel* Cancel) {
    memset(Rpc,0,sizeof(*Rpc)); Rpc->Cancel=Cancel; strcpy(Rpc->Device,Device); MdoDtRandom(Rpc->Client);
    if (!MdoAccountAcquireService(Cancel,&Rpc->Lease) || Rpc->Lease.MemberId!=Member ||
        !MdoRemoteNetInit(&Rpc->Net,Server->Engine,NULL)) return false;
    xvalue* Body=xrtValueObject(); uint16 Status=0;
    bool Ok=Body && MdoAccountSetString(Body,"device_id",Device) && MdoAccountSetString(Body,"role","controller") && MdoAccountSetString(Body,"mode","control");
    xvalue* Ticket=Ok?MdoRemoteServiceCall("/api/v1/devices/ticket","POST",Body,&Rpc->Lease,&Status):NULL;
    xrtValueRelease(Body); Rpc->Status=Status;
    if (Ticket) Rpc->Socket=MdoRemoteServiceConnect(&Rpc->Net,Ticket,&Rpc->Lease,&Status);
    MdoAccountSecretValueRelease(Ticket);
    if (!Rpc->Socket) return false;
    uint64 Until=MdoDeadlineAfterUs(10000000u);
    while (!Rpc->Hello && !MdoDeadlineExpired(Until)) { if (!MdoDtRpcPoll(Rpc)) return false; xrtSleep(1u); }
    if (!Rpc->Hello) return false;
    if (Rpc->Offer) {
        Rpc->Direct=MdoLanConnectOffer(&Rpc->DirectNet,Server,Rpc->Offer,Rpc->Lease.Cancel);
        Until=MdoDeadlineAfterUs(2000000u);
        while (Rpc->Direct && !Rpc->DirectReady && !MdoDeadlineExpired(Until)) {
            if (!MdoDtRpcPoll(Rpc)) break;
            xrtSleep(1u);
        }
        if (!Rpc->DirectReady) { MdoRemoteSocketDestroy(Rpc->Direct); Rpc->Direct=NULL; MdoRemoteNetUnit(&Rpc->DirectNet); }
    }
    return true;
}
xvalue* MdoDtRpcCall(MdoDtRpc* Rpc,cstr Method,cstr Path,const void* Data,size_t Bytes,bool Json) {
    bool Write=strcmp(Method,"GET")!=0; char Hash[65]; xvalue* Value=xrtValueObject();
    xvalue* Headers=xrtValueArray(); xvalue* Header=NULL; xvalue* Result=NULL;
    Rpc->Done=false; Rpc->Status=0; Rpc->Sent=Rpc->Received=Rpc->ResponseBytes=0;
    Rpc->ErrorCode[0]=Rpc->ErrorMessage[0]=0;
    Rpc->Upload=Data; Rpc->UploadBytes=Bytes;
    if (!Rpc->Response) Rpc->Response=xrtMalloc(262145u);
    if (!Rpc->Response || !MdoDtRandom(Rpc->Request) || !MdoDtHex(Data,Bytes,Hash)) goto done;
    if (Write) {
        Header=xrtValueArray();
        if (!Header || !MdoApiValueAppendString(Header,"X-Mdo-Write-Token") || !MdoApiValueAppendString(Header,Rpc->Token) ||
            !xrtValueArrayAppendTake(Headers,&Header)) goto done;
        Header=xrtValueArray();
        if (!Header || !MdoApiValueAppendString(Header,"Content-Type") || !MdoApiValueAppendString(Header,Json?"application/json":"application/octet-stream") ||
            !xrtValueArrayAppendTake(Headers,&Header)) goto done;
    }
    if (!Value || !MdoAccountSetString(Value,"type","request") || !MdoAccountSetString(Value,"id",Rpc->Request) ||
        !MdoAccountSetString(Value,"runtime_id",Rpc->Runtime) || !MdoAccountSetString(Value,"client_id",Rpc->Client) ||
        !MdoAccountSetUInt(Value,"sequence",Write?++Rpc->Sequence:0u) || !MdoAccountSetString(Value,"method",Method) ||
        !MdoAccountSetString(Value,"path",Path) || !MdoAccountSetUInt(Value,"bytes",Bytes) || !MdoAccountSetString(Value,"sha256",Hash) ||
        !xrtValueObjectSetTake(Value,XRT_STR_LITERAL("headers"),&Headers)) goto done;
    if (!MdoDtRpcText(Rpc,Value)) { Value=NULL; goto done; }
    Value=NULL;
    uint64 Until=MdoDeadlineAfterUs(60000000u);
    while (!Rpc->Done && !MdoDeadlineExpired(Until)) { if (!MdoDtRpcPoll(Rpc)) goto done; xrtSleep(1u); }
    if (Rpc->Done) {
        xjsonreadconfig Config; xrtJsonReadConfigInit(&Config); Config.MaxInputBytes=262144u;
        Config.MaxValues=8192u; Config.MaxDepth=12u; Config.MaxStringBytes=8192u;
        Result=xrtJsonRead(xrtStrViewN(Rpc->Response,Rpc->ResponseBytes),&Config);
    }
done:
    Rpc->Upload=NULL; Rpc->UploadBytes=0;
    xrtValueRelease(Value); xrtValueRelease(Headers); xrtValueRelease(Header); return Result;
}
