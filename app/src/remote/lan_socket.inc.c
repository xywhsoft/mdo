static MdoRemoteSocket* MdoRemoteServerSocket(xnetstream* Tcp, xtlsstream* Tls, xcancel* Cancel)
{
    MdoRemoteSocket* Socket=xrtCalloc(1u,sizeof(*Socket)); if (!Socket) return NULL;
    Socket->Limit=262123u; Socket->Capacity=Socket->Limit+XWS_FRAME_HEAD_MAX; Socket->Server=true;
    Socket->Cancel=xrtCancelChild(Cancel); Socket->Input=xrtMalloc(Socket->Capacity); Socket->Message=xrtMalloc(Socket->Limit);
    Socket->Tls=Tls ? xrtTlsStreamRef(Tls) : NULL; Socket->Tcp=Tcp ? xrtNetStreamRef(Tcp) : NULL;
    Socket->UpgradeUntil=xrtDeadlineAfter(2000000u);
    if (!Socket->Cancel || !Socket->Input || !Socket->Message || (!Socket->Tcp && !Socket->Tls)) {
        MdoRemoteSocketDestroy(Socket); return NULL;
    }
    return Socket;
}
static bool MdoRemoteServerHead(MdoRemoteSocket* Socket, const xhttp1head* Head, cstr Protocol)
{
    xwsupgradeserverconfig Config; xwsupgrade Upgrade; xhttpfield Fields[XWS_UPGRADE_RESPONSE_FIELDS_MAX];
    size_t Count=0u, Size=0u; xwsmessageconfig Message;
    xrtWsUpgradeServerConfigInit(&Config); Config.Protocols=xrtStrView(Protocol);
    if (!xrtWsUpgradeRequestCheck(Head,&Config,&Upgrade) || !Upgrade.Protocol.Size ||
        !xrtWsUpgradeResponseFields(xrtStrView(Upgrade.Accept),Upgrade.Protocol,xrtStrView(""),Fields,
            XWS_UPGRADE_RESPONSE_FIELDS_MAX,&Count) ||
        !xrtHttp1ResponseWrite(XHTTP_VERSION_1_1,101u,XRT_STR_LITERAL("Switching Protocols"),Fields,Count,
            Socket->UpgradeHead,sizeof(Socket->UpgradeHead),&Size)) return false;
    xrtWsMessageConfigInitSafe(&Message); Message.MaxSize=Socket->Limit;
    if (!xrtWsMessageInit(&Socket->State,&Message)) return false;
    /* Only the executor may block for drain. The route itself runs on an
     * xs network worker and merely transfers the owned stream to that task. */
    Socket->UpgradeHeadSize=Size;
    Socket->Ready=true; Socket->LastRead=Socket->LastPing=xrtClock(); return true;
}
MdoRemoteSocket* MdoRemoteSocketAcceptHttp(XS_HttpReq* Request, cstr Protocol, xcancel* Cancel)
{
    MdoRemoteSocket* Socket=MdoRemoteServerSocket(Request->tcp,Request->tls,Cancel);
    if (Socket && MdoRemoteServerHead(Socket,Request->head,Protocol)) return Socket;
    MdoRemoteSocketDestroy(Socket); return NULL;
}
MdoRemoteSocket* MdoRemoteSocketAcceptTls(xtlsstream* Stream, xcancel* Cancel)
{ return MdoRemoteServerSocket(NULL,Stream,Cancel); }
int MdoRemoteSocketUpgrade(MdoRemoteSocket* Socket, cstr Protocol, MdoRemoteAcceptProc Authorize, void* Data)
{
    if (!Socket || !MdoRemoteSocketAlive(Socket) || xrtDeadlineExpired(Socket->UpgradeUntil)) return -1;
    if (Socket->Ready) return 1;
    if (MdoRemoteAvailable(Socket) && !MdoRemoteRead(Socket,Socket->UpgradeUntil)) return -1;
    xhttp1head Head; xhttpfield Fields[32]; xhttp1limits Limits; xrtHttp1LimitsInit(&Limits);
    Limits.MaxHead=MDO_REMOTE_HEAD; Limits.MaxFields=32u; xrtHttp1HeadInit(&Head,Fields,32u);
    xhttp1status Status=xrtHttp1RequestParse((xbytesview){(const uint8*)Socket->Input,Socket->InputSize},&Head,&Limits,NULL);
    if (Status == XHTTP1_MORE && Socket->InputSize < MDO_REMOTE_HEAD) return 0;
    if (Status != XHTTP1_READY || !Authorize || !Authorize(&Head,Data) || !MdoRemoteServerHead(Socket,&Head,Protocol)) return -1;
    Socket->InputSize -= Head.Bytes; memmove(Socket->Input,Socket->Input+Head.Bytes,Socket->InputSize);
    return 1;
}
