#include "../../include/mdo/recovery.h"
#include "../../include/mdo/diagnostics.h"
/* One host-owned initialization worker. No tool calls are replayed here. */
static bool McpPreparePause(void)
{
    if(!g_MdoMcp.PrepareLock)return true;
    uint64 End=MdoDeadlineAfterUs(5000000);xrtMutexLock(g_MdoMcp.PrepareLock);g_MdoMcp.PrepareSuspended=true;
    if(g_MdoMcp.PrepareCancel)xrtCancelRequest(g_MdoMcp.PrepareCancel);
    while(g_MdoMcp.PrepareCancel&&!MdoDeadlineExpired(End))xrtCondWaitFor(g_MdoMcp.PrepareChanged,g_MdoMcp.PrepareLock,25);
    bool Ok=!g_MdoMcp.PrepareCancel;xrtMutexUnlock(g_MdoMcp.PrepareLock);return Ok;
}
static void McpPrepareSchedule(void)
{
    if(!g_MdoMcp.PrepareLock)return;
    uint64 Generation=MdoMcpManagerGeneration();
    xrtMutexLock(g_MdoMcp.PrepareLock);
    if(!g_MdoMcp.PrepareStopping){g_MdoMcp.PrepareGeneration=Generation;g_MdoMcp.PrepareTicket++;g_MdoMcp.PrepareSuspended=false;
        if(g_MdoMcp.PrepareCancel)xrtCancelRequest(g_MdoMcp.PrepareCancel);
        xrtCondBroadcast(g_MdoMcp.PrepareChanged);}
    xrtMutexUnlock(g_MdoMcp.PrepareLock);
}
static void McpPrepareResult(cstr Id,uint64 Generation,cstr State,cstr Error,unsigned Attempt,uint64 RetryAt)
{
    xrtMutexLock(g_MdoMcp.PrepareLock);
    const xvalue* Existing=xrtValueObjectGet(g_MdoMcp.PrepareResults,xrtStrView(Id));
    cstr OldState=MdoExtensionText(Existing,"state",32);
    bool Unchanged=OldState&&!strcmp(OldState,"ready")&&!strcmp(State,"ready");
    xrtMutexUnlock(g_MdoMcp.PrepareLock);
    if(Unchanged)return;
    xvalue* Row=xrtValueObject();bool Ok=Row&&McpDirectoryPut(Row,"state",State)&&McpDirectoryPut(Row,"error",Error?Error:"")&&
        xrtValueObjectSetNew(Row,XRT_STR_LITERAL("attempt"),xrtValueUInt(Attempt))&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("retry_at"),xrtValueUInt(RetryAt))&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("generation"),xrtValueUInt(Generation));
    xrtMutexLock(g_MdoMcp.PrepareLock);
    if(Ok&&Generation==g_MdoMcp.PrepareGeneration&&!g_MdoMcp.PrepareStopping)
        (void)xrtValueObjectSet(g_MdoMcp.PrepareResults,xrtStrView(Id),Row);
    xrtMutexUnlock(g_MdoMcp.PrepareLock);xrtValueRelease(Row);
}
static bool McpPrepareTransient(const xwork_error* Error)
{
    if(Error->uHttpStatus)return Error->uHttpStatus==408||Error->uHttpStatus==429||Error->uHttpStatus>=500;
    if(Error->eCode==XWORK_ERROR_IO&&strstr(Error->sMessage,"failed to start MCP stdio server"))return false;
    return Error->eCode==XWORK_ERROR_IO||Error->eCode==XWORK_ERROR_TIMEOUT;
}
static int32 McpPrepareWorker(void* Data)
{
    (void)Data;uint64 Ticket=0;
    for(;;){
        xrtMutexLock(g_MdoMcp.PrepareLock);
        if(!g_MdoMcp.PrepareStopping&&(g_MdoMcp.PrepareSuspended||Ticket==g_MdoMcp.PrepareTicket))xrtCondWaitFor(g_MdoMcp.PrepareChanged,g_MdoMcp.PrepareLock,1000);
        if(g_MdoMcp.PrepareStopping){xrtMutexUnlock(g_MdoMcp.PrepareLock);break;}
        if(g_MdoMcp.PrepareSuspended){xrtMutexUnlock(g_MdoMcp.PrepareLock);continue;}
        if(Ticket!=g_MdoMcp.PrepareTicket){xrtValueRelease(g_MdoMcp.PrepareResults);g_MdoMcp.PrepareResults=xrtValueObject();}
        Ticket=g_MdoMcp.PrepareTicket;uint64 Completed=g_MdoMcp.PrepareGeneration;xcancel* Cancel=xrtCancelCreate();g_MdoMcp.PrepareCancel=Cancel;
        xrtMutexUnlock(g_MdoMcp.PrepareLock);
        MdoMcpCatalog* Catalog=Cancel?MdoMcpCatalogSnapshot():NULL;
        for(size_t I=0;Catalog&&I<Catalog->Count&&!xrtCancelRequested(Cancel);I++){
            const MdoMcpServerEntry* Entry=&Catalog->Entries[I];if(!Entry->Enabled)continue;
            MdoMcpServerStatus Status={0};Status.Size=sizeof(Status);
            if(MdoMcpManagerGetStatus(Entry->Id,&Status)&&Status.Connected&&Status.ToolsDiscovered){McpPrepareResult(Entry->Id,Completed,"ready","",0,0);continue;}
            xrtMutexLock(g_MdoMcp.PrepareLock);
            const xvalue* Previous=xrtValueObjectGet(g_MdoMcp.PrepareResults,xrtStrView(Entry->Id));
            uint64 Attempt=0,RetryAt=0;xrtValueGetUInt(xrtValueObjectGet(Previous,XRT_STR_LITERAL("attempt")),&Attempt);
            xrtValueGetUInt(xrtValueObjectGet(Previous,XRT_STR_LITERAL("retry_at")),&RetryAt);
            bool AwaitConfiguration=Attempt&&!RetryAt;
            cstr PreviousState=MdoExtensionText(Previous,"state",32);
            bool ManualOnly=PreviousState&&!strcmp(PreviousState,"ready")&&!Entry->AutoReconnect;
            xrtMutexUnlock(g_MdoMcp.PrepareLock);
            if(ManualOnly||AwaitConfiguration||(RetryAt&&!MdoDeadlineExpired(RetryAt)))continue;
            if(Attempt<UINT32_MAX)Attempt++;
            uint64 Milliseconds=(uint64)Entry->StartupTimeoutMilliseconds+Entry->RequestTimeoutMilliseconds;
            if(Milliseconds>60000)Milliseconds=60000;
            uint64 Deadline=MdoDeadlineAfterUs(Milliseconds*1000);
            {
                McpPrepareResult(Entry->Id,Completed,"starting","",(unsigned)Attempt,0);
                xwork_error Error;xworkErrorInit(&Error);
                bool Ok=xworkRuntimeRefreshMcpServer(g_MdoMcp.Runtime,Entry->Id,Cancel,Deadline,&Error);
                if(Ok){MdoMcpServerStatus Ready={0};Ready.Size=sizeof(Ready);(void)MdoMcpManagerGetStatus(Entry->Id,&Ready);}
                if(xrtCancelRequested(Cancel))break;
                xvalue* Auth=MdoMcpAuthorizationStatus(Entry->Id);bool Login=MdoMcpTemplateFlag(Auth,"needs_authorization",false);xrtValueRelease(Auth);
                bool Retry=!Ok&&!Login&&McpPrepareTransient(&Error);
                uint32 Limit=MdoRecoveryRetryLimit();
                uint64 Delay=Attempt>Limit?MDO_RECOVERY_WAIT_MS:(Attempt>6?30000u:(1000u<<((unsigned)Attempt-1u)));
                McpPrepareResult(Entry->Id,Completed,Ok?"ready":Login||Error.uHttpStatus==401?Entry->OAuthJson?"needs_authorization":"needs_configuration":Retry?Attempt>Limit?"waiting":"starting":"failed",Error.sMessage,Ok?0:(unsigned)Attempt,Retry?MdoDeadlineAfterUs(Delay*1000):0);
            }
        }
        MdoMcpCatalogRelease(Catalog);
        xrtMutexLock(g_MdoMcp.PrepareLock);g_MdoMcp.PrepareCancel=NULL;xrtCondBroadcast(g_MdoMcp.PrepareChanged);xrtMutexUnlock(g_MdoMcp.PrepareLock);
        xrtCancelDestroy(Cancel);
    }
    return 0;
}

bool MdoMcpWarnings(xvalue* Items)
{
    if(!g_MdoMcp.Initialized)return true;
    MdoMcpCatalog* Catalog=MdoMcpCatalogSnapshot();bool Ok=Catalog!=NULL;
    for(size_t I=0;Ok&&I<Catalog->Count;I++){
        const MdoMcpServerEntry* Entry=&Catalog->Entries[I];if(!Entry->Enabled)continue;
        MdoMcpServerStatus Status={0};Status.Size=sizeof(Status);
        if(MdoMcpManagerGetStatus(Entry->Id,&Status)&&Status.Connected&&Status.ToolsDiscovered)continue;
        xrtMutexLock(g_MdoMcp.PrepareLock);
        const xvalue* Row=xrtValueObjectGet(g_MdoMcp.PrepareResults,xrtStrView(Entry->Id));
        cstr State=MdoExtensionText(Row,"state",32);
        bool Report=State&&strcmp(State,"starting")&&strcmp(State,"ready");
        xrtMutexUnlock(g_MdoMcp.PrepareLock);
        if(!Report)continue;
        Ok=MdoDiagnosticWarning(Items,"mcp","connection_unavailable",Entry->Id,"",
            "MCP connection is temporarily unavailable. Other Agent capabilities remain available.",
            "Open MCP settings to reconnect, complete authorization, or disable this connection.","mcp");
    }
    MdoMcpCatalogRelease(Catalog);return Ok;
}
xvalue* MdoMcpExtensionStatus(cstr Ref)
{
    MdoMcpCatalog* Catalog=MdoMcpCatalogSnapshot();MdoMcpDiagnostics* Diagnostics=MdoMcpDiagnosticsSnapshot();
    xvalue* Out=xrtValueObject(),*Missing=xrtValueArray();const char* State="failed";char Error[1024]="MCP configuration is unavailable",Id[97]="";size_t Connections=0,Tools=0;unsigned Best=0;
    for(size_t I=0;Catalog&&I<xrtValueCount(Catalog->Defaults);I++){
        const xvalue* Row=xrtValueArrayGet(Catalog->Defaults,I),*Doc=xrtValueObjectGet(Row,XRT_STR_LITERAL("connection"));cstr Extension=MdoExtensionText(Row,"extension_id",MDO_EXTENSION_REF_BYTES);if(!Extension||!Ref||strcmp(Extension,Ref))continue;
        cstr Key=MdoExtensionText(Doc,"id",96),S=MdoExtensionText(Row,"state",32),Message=MdoExtensionText(Row,"error",1024);if(!S)S="failed";bool Enabled=MdoMcpTemplateFlag(Doc,"enabled",false);MdoMcpServerStatus Status={0};Status.Size=sizeof(Status);
        bool Found=Key&&MdoMcpManagerGetStatus(Key,&Status);xvalue* Prepared=NULL;
        if(Found){if(!Status.Enabled)S="disabled";else if(Status.Connected&&Status.ToolsDiscovered){S="ready";Message="";}else{
            if(g_MdoMcp.PrepareLock){xrtMutexLock(g_MdoMcp.PrepareLock);Prepared=xrtValueClone(xrtValueObjectGet(g_MdoMcp.PrepareResults,xrtStrView(Key)));xrtMutexUnlock(g_MdoMcp.PrepareLock);}
            uint64 G=0;if(xrtValueGetUInt(xrtValueObjectGet(Prepared,XRT_STR_LITERAL("generation")),&G)&&G==Catalog->Generation){S=MdoExtensionText(Prepared,"state",32);Message=MdoExtensionText(Prepared,"error",1024);}
            if(S&&!strcmp(S,"ready")){S=Status.State==XWORK_MCP_SERVER_FAILED?"failed":"disconnected";Message="";}
        }}else if(Key&&Enabled&&strcmp(S,"needs_configuration")&&strcmp(S,"needs_approval")){
            for(size_t J=0;Diagnostics&&J<Diagnostics->Count;J++)if(Diagnostics->Entries[J].ServerId&&!strcmp(Diagnostics->Entries[J].ServerId,Key)){Message=Diagnostics->Entries[J].Message;S=Message&&strstr(Message,"not been approved")?"needs_approval":"failed";break;}
        }
        if(Key)Connections++;
        if(Found&&Status.Enabled)Tools+=Status.DiscoveredToolCount;
        unsigned Rank=!strcmp(S,"ready")?6:!strcmp(S,"starting")?5:!strcmp(S,"disabled")?1:3;
        if(Rank>Best){Best=Rank;State=S;snprintf(Error,sizeof(Error),"%s",Message?Message:"");snprintf(Id,sizeof(Id),"%s",Key?Key:"");
            xrtValueRelease(Missing);Missing=xrtValueClone(xrtValueObjectGet(Row,XRT_STR_LITERAL("missing_inputs")));if(!Missing)Missing=xrtValueArray();
            /* State can be borrowed from the transient prepared result. */
            (void)McpDirectoryPut(Out,"state",State);
        }xrtValueRelease(Prepared);
    }
    if(!Best)McpDirectoryPut(Out,"state",State);
    bool Ok=Out&&Missing&&McpDirectoryPut(Out,"activation_error",Error)&&McpDirectoryPut(Out,"default_connection_id",Id)&&xrtValueObjectSet(Out,XRT_STR_LITERAL("missing_inputs"),Missing)&&xrtValueObjectSetNew(Out,XRT_STR_LITERAL("connection_count"),xrtValueUInt(Connections))&&xrtValueObjectSetNew(Out,XRT_STR_LITERAL("tool_count"),xrtValueUInt(Tools));
    xrtValueRelease(Missing);MdoMcpCatalogRelease(Catalog);MdoMcpDiagnosticsRelease(Diagnostics);if(!Ok){xrtValueRelease(Out);Out=NULL;}return Out;
}
static bool McpPrepareInit(void)
{
    g_MdoMcp.PrepareLock=xrtMutexCreate();g_MdoMcp.PrepareChanged=xrtCondCreate();g_MdoMcp.PrepareResults=xrtValueObject();
    if(!g_MdoMcp.PrepareLock||!g_MdoMcp.PrepareChanged||!g_MdoMcp.PrepareResults)return false;
    g_MdoMcp.PrepareThread=xrtThreadCreate(McpPrepareWorker,NULL,0);return g_MdoMcp.PrepareThread!=NULL;
}
static void McpPrepareUnit(void)
{
    if(g_MdoMcp.PrepareLock){xrtMutexLock(g_MdoMcp.PrepareLock);g_MdoMcp.PrepareStopping=true;
        if(g_MdoMcp.PrepareCancel)xrtCancelRequest(g_MdoMcp.PrepareCancel);
        if(g_MdoMcp.PrepareChanged)xrtCondBroadcast(g_MdoMcp.PrepareChanged);
        xrtMutexUnlock(g_MdoMcp.PrepareLock);}
    if(g_MdoMcp.PrepareThread){xrtThreadWait(g_MdoMcp.PrepareThread);xrtThreadDestroy(g_MdoMcp.PrepareThread);}
    xrtValueRelease(g_MdoMcp.PrepareResults);if(g_MdoMcp.PrepareChanged)xrtCondDestroy(g_MdoMcp.PrepareChanged);if(g_MdoMcp.PrepareLock)xrtMutexDestroy(g_MdoMcp.PrepareLock);
    g_MdoMcp.PrepareLock=NULL;g_MdoMcp.PrepareChanged=NULL;g_MdoMcp.PrepareThread=NULL;g_MdoMcp.PrepareResults=NULL;
}
