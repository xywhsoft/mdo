#include "../../include/mdo/time.h"
#include "internal.h"

#if defined(_WIN32) || defined(_WIN64)
__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int,unsigned long,const char*,int,unsigned short*,int);
__declspec(dllimport) int __stdcall GetDiskFreeSpaceExW(const unsigned short*,uint64*,uint64*,uint64*);
#endif
bool MdoDtDiskSpace(cstr Path,uint64 Required) {
    uint64 Available=0;
#if defined(_WIN32) || defined(_WIN64)
    unsigned short Wide[4096];
    if (!Path || !MultiByteToWideChar(65001u,8u,Path,-1,Wide,4096) || !GetDiskFreeSpaceExW(Wide,&Available,NULL,NULL)) return false;
#elif defined(__linux__)
    /* Supported Linux/Android products use 64-bit unsigned-long statvfs ABI.
     * Resolve libc dynamically because the small TCC SDK omits statvfs.h. */
    void* Lib=dlopen(NULL,RTLD_NOW); uint64 Buffer[32]={0};
    int (*Stat)(const char*,void*)=Lib?dlsym(Lib,"statvfs"):NULL;
    bool Ok=Stat && Stat(Path,Buffer)==0; if (Lib) dlclose(Lib);
    if (!Ok) return false;
    uint64 Unit=Buffer[1]?Buffer[1]:Buffer[0];
    Available=Unit && Buffer[4]>UINT64_MAX/Unit?UINT64_MAX:Unit*Buffer[4];
#else
    (void)Path; return true;
#endif
    return Required<=Available && Available-Required>=UINT64_C(33554432);
}
void MdoDtKeepAwake(void) {
    if (!g_MdoDt.Lock) return;
    xrtMutexLock(g_MdoDt.Lock);
    if (MdoMonotonicUs()>=g_MdoDt.BackgroundUntil) {
        if (g_MdoDt.Background && !xsBackgroundLeaseRenew(g_MdoDt.Background,60000u)) g_MdoDt.Background=0;
        if (!g_MdoDt.Background) g_MdoDt.Background=xsBackgroundLeaseAcquire(60000u);
        g_MdoDt.BackgroundUntil=MdoMonotonicUs()+UINT64_C(10000000);
    }
    xrtMutexUnlock(g_MdoDt.Lock);
}
static xvalue* MdoDtTaskList(bool Incoming) {
    char Path[80]; snprintf(Path,sizeof(Path),MDO_DT_BASE "/%s",Incoming?"incoming":"outgoing");
    xvalue* Items=xrtValueArray(); xvalue* Recent[16]={0}; size_t RecentCount=0,ActiveCount=0;
    xdir Directory=MdoHomeOpenDirectory(Path,XDIR_STAT); xdirentry Entry; size_t Scanned=0;
    if (Directory) {
        while (xrtDirNext(Directory,&Entry)==XDIR_NEXT_ITEM && Scanned++<4096u) {
            char Id[33]; if (Entry.Info.Type!=XFILE_TYPE_DIRECTORY || Entry.Name.Size!=32u) continue;
            memcpy(Id,Entry.Name.Data,32); Id[32]=0;
            xvalue* Task=MdoDtTaskRead(Incoming,Id); uint64 Member=0;
            if (Task) MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("member_id")),&Member);
            if (Task && MdoDtMember(Member)) {
                const char* Fields[]={"id","kind","title","project_id","device_id","device_name","member_id","state","error",
                    "updated_at","destination_project_id","destination_session_id","destination_workspace","commit_requested","commit_started"};
                xvalue* Summary=xrtValueObject();
                for (size_t i=0;i<sizeof(Fields)/sizeof(*Fields);i++) {
                    xvalue* Value=xrtValueClone(xrtValueObjectGet(Task,xrtStrView(Fields[i])));
                    if (Value) (void)MdoApiValueSetTake(Summary,Fields[i],&Value);
                }
                MdoAccountSetBool(Summary,"incoming",Incoming);
                cstr State=MdoAccountText(Task,"state",32u);
                bool Terminal=State && (!strcmp(State,"completed") || !strcmp(State,"cancelled"));
                if (!Terminal && ActiveCount<32u) { xrtValueArrayAppendNew(Items,Summary); ActiveCount++; }
                else if (RecentCount<16u) Recent[RecentCount++]=Summary;
                else {
                    size_t Oldest=0; int64 Minimum=INT64_MAX,At=0;
                    (void)xrtValueGetInt(xrtValueObjectGet(Summary,XRT_STR_LITERAL("updated_at")),&At);
                    for (size_t i=0;i<16u;i++) { int64 Stamp=0; (void)xrtValueGetInt(xrtValueObjectGet(Recent[i],XRT_STR_LITERAL("updated_at")),&Stamp); if (Stamp<Minimum) { Minimum=Stamp; Oldest=i; } }
                    if (At>Minimum) { xrtValueRelease(Recent[Oldest]); Recent[Oldest]=Summary; } else xrtValueRelease(Summary);
                }
            }
            xrtValueRelease(Task);
        }
        xrtDirClose(Directory);
    }
    for (size_t i=0;i<RecentCount;i++) xrtValueArrayAppendNew(Items,Recent[i]);
    return Items;
}
static bool MdoDtIs(cstr State,cstr Text) { return State && !strcmp(State,Text); }
static bool MdoDtTerminal(cstr State) { return MdoDtIs(State,"completed") || MdoDtIs(State,"cancelled"); }
static xvalue* MdoDtQueuedTasks(bool Incoming) {
    xrtMutexLock(g_MdoDt.Lock); xvalue* Queue=MdoDtQueue(Incoming); xrtMutexUnlock(g_MdoDt.Lock);
    xvalue* Items=xrtValueArray();
    for (size_t i=0;i<xrtValueCount(Queue);i++) {
        xstrview Id; char Text[33];
        if (!xrtValueGetString(xrtValueArrayGet(Queue,i),&Id) || Id.Size!=32u) continue;
        memcpy(Text,Id.Data,32u); Text[32]=0;
        xvalue* Task=MdoDtTaskRead(Incoming,Text); uint64 Member=0;
        if (Task) MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("member_id")),&Member);
        if (Task && MdoDtMember(Member) && !MdoDtTerminal(MdoAccountText(Task,"state",32u))) xrtValueArrayAppendNew(Items,Task);
        else {
            if (Task && MdoDtMember(Member) && MdoDtTerminal(MdoAccountText(Task,"state",32u))) (void)MdoDtPrunePayload(Incoming,Task);
            xrtValueRelease(Task); xrtMutexLock(g_MdoDt.Lock); (void)MdoDtQueueChange(Incoming,Text,false); xrtMutexUnlock(g_MdoDt.Lock);
        }
    }
    xrtValueRelease(Queue); return Items;
}
static int32 MdoDtSourceWorker(void* Data) {
    (void)Data; unsigned Retry=0;
    while (!xrtCancelRequested(g_MdoDt.Stop)) {
        xvalue* List=MdoDtQueuedTasks(false); xvalue* Task=NULL;
        for (size_t i=0;i<xrtValueCount(List);i++) {
            const xvalue* Item=xrtValueArrayGet(List,i); cstr State=MdoAccountText(Item,"state",32u);
            if (State && !MdoDtTerminal(State) && strcmp(State,"paused") && strcmp(State,"awaiting_confirmation")) { Task=xrtValueClone(Item); break; }
        }
        xrtValueRelease(List);
        if (!Task) { xrtSleep(100u); continue; }
        xcancel* Cancel=xrtCancelChild(g_MdoDt.Stop); MdoPowerInhibitor Power={0};
        xrtMutexLock(g_MdoDt.Lock);
        xvalue* Current=MdoDtTaskRead(false,MdoAccountText(Task,"id",32u));
        cstr CurrentState=MdoAccountText(Current,"state",32u);
        if (!Cancel || !CurrentState || MdoDtTerminal(CurrentState) || MdoDtIs(CurrentState,"paused") || MdoDtIs(CurrentState,"awaiting_confirmation")) {
            xrtMutexUnlock(g_MdoDt.Lock); xrtCancelDestroy(Cancel); xrtValueRelease(Current); xrtValueRelease(Task); continue;
        }
        xrtValueRelease(Task); Task=Current;
        g_MdoDt.WorkCancel=Cancel; g_MdoDt.Working=true;
        strcpy(g_MdoDt.Active,MdoAccountText(Task,"id",32u)); xrtMutexUnlock(g_MdoDt.Lock);
        (void)MdoPowerInhibitorBegin(&Power); MdoDtKeepAwake();
        cstr State=MdoAccountText(Task,"state",32u); bool Ok=false; int64 ConnectedBefore=0,ConnectedAfter=0;
        (void)xrtValueGetInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("last_connected_at")),&ConnectedBefore);
        if (MdoDtIs(State,"planning")) Ok=MdoDtSend(Task,Cancel);
        else {
            uint64 ManifestBytes=0;
            MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("manifest_bytes")),&ManifestBytes);
            if (!ManifestBytes) {
                xwork_error Error; MdoDtTaskState(Task,"capturing",""); MdoDtTaskSave(false,Task);
                if (!MdoDtCapture(Task,Cancel,&Error))
                    MdoDtTaskState(Task,Error.eCode==XWORK_ERROR_CONTEXT?"waiting_idle":"paused",Error.sMessage);
                else Ok=MdoDtSend(Task,Cancel);
            } else Ok=MdoDtSend(Task,Cancel);
        }
        if (xrtCancelRequested(Cancel) && !xrtCancelRequested(g_MdoDt.Stop)) MdoDtTaskState(Task,"cancelled","");
        if (xrtCancelRequested(g_MdoDt.Stop) && !Ok && !MdoDtTerminal(MdoAccountText(Task,"state",32u))) {
            uint64 Bytes=0; MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("manifest_bytes")),&Bytes);
            bool Confirmed=MdoAccountText(Task,"target_project_id",64u)!=NULL;
            MdoDtTaskState(Task,Bytes?"connecting":Confirmed?"waiting_idle":"planning","");
        }
        if (!Ok || xrtCancelRequested(Cancel)) MdoDtTaskSave(false,Task);
        MdoPowerInhibitorEnd(&Power);
        if (MdoDtTerminal(MdoAccountText(Task,"state",32u))) (void)MdoDtPrunePayload(false,Task);
        xrtMutexLock(g_MdoDt.Lock);
        if (MdoDtTerminal(MdoAccountText(Task,"state",32u))) (void)MdoDtQueueChange(false,g_MdoDt.Active,false);
        g_MdoDt.Active[0]=0; g_MdoDt.WorkCancel=NULL; g_MdoDt.Working=false; xrtMutexUnlock(g_MdoDt.Lock);
        (void)xrtValueGetInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("last_connected_at")),&ConnectedAfter);
        xrtCancelDestroy(Cancel); xrtValueRelease(Task);
        if (Ok || ConnectedAfter!=ConnectedBefore) Retry=0; else if (Retry<6u) Retry++;
        unsigned Delay=Ok?100u:(1u<<Retry)*250u;
        for (unsigned n=0;n<Delay/50u && !xrtCancelRequested(g_MdoDt.Stop);n++) xrtSleep(50u);
    }
    return 0;
}
static int32 MdoDtReceiverWorker(void* Data) {
    (void)Data; MdoPowerInhibitor Power={0};
    while (!xrtCancelRequested(g_MdoDt.Stop)) {
        xvalue* List=MdoDtQueuedTasks(true); bool Active=false;
        for (size_t i=0;i<xrtValueCount(List) && !xrtCancelRequested(g_MdoDt.Stop);i++) {
            xvalue* Task=xrtValueArrayGet(List,i); cstr State=MdoAccountText(Task,"state",32u); int64 Updated=0;
            (void)xrtValueGetInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("updated_at")),&Updated);
            bool Commit=MdoDtIs(State,"queued_commit") || MdoDtIs(State,"committing") || MdoDtIs(State,"committed");
            Active|=Commit || (State && !strncmp(State,"receiving_",10u) && MdoUtcUs()-Updated<INT64_C(90000000));
            if (Commit && MdoDtReceiveAllowed()) {
                xrtMutexLock(g_MdoDt.Lock);
                xvalue* Current=MdoDtTaskRead(true,MdoAccountText(Task,"id",32u));
                cstr CurrentState=MdoAccountText(Current,"state",32u);
                if (!MdoDtIs(CurrentState,"queued_commit") && !MdoDtIs(CurrentState,"committing") && !MdoDtIs(CurrentState,"committed")) {
                    xrtValueRelease(Current); xrtMutexUnlock(g_MdoDt.Lock); continue;
                }
                Task=Current; State=CurrentState;
                strcpy(g_MdoDt.ActiveIncoming,MdoAccountText(Task,"id",32u));
                MdoAccountSetBool(Task,"commit_started",true);
                if (!MdoDtIs(State,"committed")) MdoDtTaskState(Task,"committing","");
                MdoDtTaskSave(true,Task); xrtMutexUnlock(g_MdoDt.Lock);
                (void)MdoPowerInhibitorBegin(&Power); MdoDtKeepAwake();
                xwork_error Error; xworkErrorInit(&Error);
                MdoAccountLease Lease={0}; uint64 Member=0;
                MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("member_id")),&Member);
                bool Authorized=MdoAccountAcquireService(g_MdoDt.Stop,&Lease) && Lease.MemberId==Member;
                bool Committed=Authorized && MdoDtCommit(Task,Lease.Cancel,&Error);
                if (!Committed && !xrtCancelRequested(g_MdoDt.Stop)) {
                    if (!Authorized || xrtCancelRequested(Lease.Cancel))
                        snprintf(Error.sMessage,sizeof(Error.sMessage),"account authorization changed; sign in to the receiving account and resume this transfer");
                    cstr Settled=MdoAccountText(Task,"state",32u);
                    if (MdoDtIs(Settled,"committed")) MdoAccountSetString(Task,"error",Error.sMessage);
                    else MdoDtTaskState(Task,"paused",Error.sMessage);
                    MdoDtTaskSave(true,Task);
                }
                MdoAccountRelease(&Lease);
                if (MdoDtTerminal(MdoAccountText(Task,"state",32u))) (void)MdoDtPrunePayload(true,Task);
                xrtMutexLock(g_MdoDt.Lock);
                if (MdoDtTerminal(MdoAccountText(Task,"state",32u))) (void)MdoDtQueueChange(true,g_MdoDt.ActiveIncoming,false);
                g_MdoDt.ActiveIncoming[0]=0; xrtMutexUnlock(g_MdoDt.Lock); xrtValueRelease(Current);
            }
        }
        xrtValueRelease(List);
        if (Active && MdoDtReceiveAllowed()) { (void)MdoPowerInhibitorBegin(&Power); MdoDtKeepAwake(); }
        else MdoPowerInhibitorEnd(&Power);
        xrtSleep(100u);
    }
    MdoPowerInhibitorEnd(&Power); return 0;
}
bool MdoDeviceTransfersInit(XS_ServerInfo* Server) {
    if (g_MdoDt.Lock) return true;
    g_MdoDt.Lock=xrtMutexCreate(); g_MdoDt.Stop=xrtCancelCreate(); g_MdoDt.Server=xsServerRetain(Server);
    if (!g_MdoDt.Lock || !g_MdoDt.Stop || !g_MdoDt.Server) goto failed;
    bool QueueExists=false;
    if (!MdoHomeExternalStat(MDO_DT_BASE "/queue.json",&QueueExists,NULL)) goto failed;
    if (QueueExists) {
        xvalue* Queue=MdoDtRead(MDO_DT_BASE "/queue.json",8192u); bool Valid=Queue!=NULL;
        const char* Names[]={"incoming","outgoing"};
        for (size_t i=0;i<2u;i++) {
            const xvalue* Items=xrtValueObjectGet(Queue,xrtStrView(Names[i]));
            if (xrtValueType(Items)!=XVALUE_ARRAY || xrtValueCount(Items)>MDO_DT_TASKS) Valid=false;
            for (size_t j=0;j<xrtValueCount(Items);j++) { xstrview Id; if (!xrtValueGetString(xrtValueArrayGet(Items,j),&Id) || Id.Size!=32u || !MdoDtId(Id.Data)) Valid=false; }
        }
        xrtValueRelease(Queue); if (!Valid) goto failed;
    }
    g_MdoDt.Thread=xrtThreadCreate(MdoDtSourceWorker,NULL,0u);
    g_MdoDt.Receiver=xrtThreadCreate(MdoDtReceiverWorker,NULL,0u);
    if (g_MdoDt.Thread && g_MdoDt.Receiver) return true;
failed:
    MdoDeviceTransfersUnit(); return false;
}
void MdoDeviceTransfersUnit(void) {
    if (g_MdoDt.Stop) xrtCancelRequest(g_MdoDt.Stop);
    if (g_MdoDt.Thread) { xrtThreadWait(g_MdoDt.Thread); xrtThreadDestroy(g_MdoDt.Thread); }
    if (g_MdoDt.Receiver) { xrtThreadWait(g_MdoDt.Receiver); xrtThreadDestroy(g_MdoDt.Receiver); }
    xsBackgroundLeaseRelease(g_MdoDt.Background); xrtValueRelease(g_MdoDt.IncomingManifest);
    xsServerRelease(g_MdoDt.Server); xrtCancelDestroy(g_MdoDt.Stop); xrtMutexDestroy(g_MdoDt.Lock);
    memset(&g_MdoDt,0,sizeof(g_MdoDt));
}
bool MdoDeviceTransfersActive(void) {
    if (!g_MdoDt.Lock) return false;
    xrtMutexLock(g_MdoDt.Lock); bool Active=g_MdoDt.Working || g_MdoDt.ActiveIncoming[0]; xrtMutexUnlock(g_MdoDt.Lock); return Active;
}
static bool MdoDtStart(MdoApiContext* Context) {
    MdoApiJsonBody Body; MdoApiBodyStatus Status=MdoApiJsonBodyRead(Context,&Body);
    if (Status!=MDO_API_BODY_OK) return MdoApiReplyBodyError(Context,Status);
    cstr Id=MdoAccountText(Body.Value,"id",32u),Kind=MdoAccountText(Body.Value,"kind",16u);
    cstr Project=MdoAccountText(Body.Value,"project_id",64u),Session=MdoAccountText(Body.Value,"session_id",32u);
    cstr Device=MdoAccountText(Body.Value,"device_id",32u); char Hash[65]; size_t Bytes=0;
    bool TextValid=(!xrtValueObjectHas(Body.Value,XRT_STR_LITERAL("title")) || MdoAccountText(Body.Value,"title",256u)) &&
        (!xrtValueObjectHas(Body.Value,XRT_STR_LITERAL("device_name")) || MdoAccountText(Body.Value,"device_name",96u));
    str Json=xrtJsonStringify(Body.Value,false,&Bytes); MdoAccountLease Lease={0};
    static const char* const Keys[]={"id","kind","project_id","session_id","title","device_id","device_name"};
    bool Valid=TextValid && MdoDtKeys(Body.Value,Keys,sizeof(Keys)/sizeof(*Keys)) && MdoDtId(Id) && MdoDtId(Device) && Project && MdoDtPath(Project) && !strchr(Project,'/') && Kind &&
        (!strcmp(Kind,"project") || (!strcmp(Kind,"session") && MdoDtSessionId(Session))) &&
        Json && MdoDtHex(Json,Bytes,Hash) && MdoAccountAcquireService(NULL,&Lease);
    xrtFree(Json);
    xvalue* Existing=Valid?MdoDtTaskRead(false,Id):NULL;
    if (Existing) {
        cstr Fingerprint=MdoAccountText(Existing,"request_sha256",64u); uint64 Member=0;
        MdoAccountGetUInt(xrtValueObjectGet(Existing,XRT_STR_LITERAL("member_id")),&Member);
        bool Same=Fingerprint && !strcmp(Fingerprint,Hash) && Member==Lease.MemberId;
        MdoApiJsonBodyUnit(&Body); MdoAccountRelease(&Lease);
        if (Same && !MdoDtTerminal(MdoAccountText(Existing,"state",32u))) Same=MdoDtQueueChange(false,MdoAccountText(Existing,"id",32u),true);
        return Same?MdoApiReplySuccessTake(Context,200,Existing,NULL):
            (xrtValueRelease(Existing),MdoApiReplyError(Context,409,"transfer_identity_conflict","Transfer ID already used",NULL));
    }
    xvalue* Remote=MdoRemoteSnapshot(); cstr Local=MdoAccountText(Remote,"device_id",32u);
    xvalue* Queue=MdoDtQueue(false); bool Full=xrtValueCount(Queue)>=MDO_DT_TASKS; xrtValueRelease(Queue);
    if (Valid && Full) {
        xrtValueRelease(Remote); MdoAccountRelease(&Lease); MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context,409,"transfer_queue_full","Finish or cancel an existing outgoing transfer before starting another",NULL);
    }
    if (Valid && Local && !strcmp(Local,Device)) Valid=false;
    xvalue* Task=Valid?xrtValueClone(Body.Value):NULL;
    Valid=Task && MdoAccountSetUInt(Task,"member_id",Lease.MemberId) && MdoAccountSetString(Task,"request_sha256",Hash) &&
        MdoAccountSetString(Task,"source_device",Local?Local:"") && MdoApiValueSetInt(Task,"created_at",MdoUtcUs()) &&
        MdoDtTaskState(Task,"planning","") && MdoDtTaskSave(false,Task) && MdoDtQueueChange(false,Id,true);
    xrtValueRelease(Remote); MdoAccountRelease(&Lease); MdoApiJsonBodyUnit(&Body);
    if (!Valid) { xrtValueRelease(Task); return MdoApiReplyError(Context,400,"transfer_unavailable","Sign in and choose a different device with a valid project or session",NULL); }
    return MdoApiReplySuccessTake(Context,202,Task,NULL);
}
bool MdoApiDeviceTransfersRoute(MdoApiContext* Context) {
    if (!g_MdoDt.Lock) return MdoApiReplyError(Context,503,"transfers_unavailable","Device transfer service is unavailable",NULL);
    bool Read=Context->Request->head->MethodCode==XHTTP_METHOD_GET;
    xstrview Path=Context->Target.Path;
    if (xrtStrEqual(Path,XRT_STR_LITERAL("/api/v1/device-transfers/capabilities"))) {
        xvalue* Value=xrtValueObject(); MdoAccountSetUInt(Value,"version",1u); MdoAccountSetBool(Value,"allow_receive",MdoDtReceiveAllowed());
        MdoAccountSetUInt(Value,"chunk_bytes",MDO_DT_CHUNK); return MdoApiReplySuccessTake(Context,200,Value,NULL);
    }
    if (xrtStrEqual(Path,XRT_STR_LITERAL("/api/v1/device-transfers/policy"))) {
        if (Read) {
            xvalue* Value=xrtValueObject(); xvalue* Policy=MdoDtRead("config/device-transfer.json",4096u);
            bool Allowed=false; uint64 Member=0;
            (void)xrtValueGetBool(xrtValueObjectGet(Policy,XRT_STR_LITERAL("allow_receive")),&Allowed);
            (void)MdoAccountGetUInt(xrtValueObjectGet(Policy,XRT_STR_LITERAL("member_id")),&Member);
            MdoAccountSetBool(Value,"allow_receive",Allowed && MdoDtMember(Member)); xrtValueRelease(Policy);
            return MdoApiReplySuccessTake(Context,200,Value,NULL);
        }
        MdoApiJsonBody Body; MdoApiBodyStatus Status=MdoApiJsonBodyRead(Context,&Body);
        if (Status!=MDO_API_BODY_OK) return MdoApiReplyBodyError(Context,Status);
        bool Allow=false; MdoAccountLease Lease={0};
        bool Ok=xrtValueCount(Body.Value)==1u && xrtValueGetBool(xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("allow_receive")),&Allow) &&
            (!Allow || MdoAccountAcquireService(NULL,&Lease)) && MdoAccountSetUInt(Body.Value,"member_id",Lease.MemberId) &&
            MdoDtSave("config/device-transfer.json",Body.Value);
        MdoAccountRelease(&Lease); MdoApiJsonBodyUnit(&Body);
        return Ok?MdoApiReplySuccessTake(Context,200,xrtValueObject(),NULL):MdoApiReplyError(Context,409,"transfer_policy_unavailable","Sign in before changing receiving settings",NULL);
    }
    if (xrtStrEqual(Path,XRT_STR_LITERAL("/api/v1/device-transfers/devices"))) {
        if (!Read) (void)MdoRemoteRequest(MDO_REMOTE_LIST,NULL);
        return MdoApiReplySuccessTake(Context,Read?200:202,MdoRemoteSnapshot(),NULL);
    }
    static const char IncomingPrefix[]="/api/v1/device-transfers/incoming/";
    if (Path.Size>=sizeof(IncomingPrefix)-1u && !memcmp(Path.Data,IncomingPrefix,sizeof(IncomingPrefix)-1u)) {
        xrtMutexLock(g_MdoDt.Lock); bool Ok=MdoDtIncomingRoute(Context); xrtMutexUnlock(g_MdoDt.Lock); return Ok;
    }
    if (Context->ParamCount==0u) {
        if (!Read) { xrtMutexLock(g_MdoDt.Lock); bool Accepted=MdoDtStart(Context); xrtMutexUnlock(g_MdoDt.Lock); return Accepted; }
        xvalue* Value=xrtValueObject(),*Outgoing=MdoDtTaskList(false),*Incoming=MdoDtTaskList(true);
        MdoApiValueSetTake(Value,"outgoing",&Outgoing); MdoApiValueSetTake(Value,"incoming",&Incoming);
        return MdoApiReplySuccessTake(Context,200,Value,NULL);
    }
    char Id[33]; if (Context->Params[0].Size!=32u) return MdoApiReplyError(Context,400,"invalid_transfer_id","Invalid transfer ID",NULL);
    memcpy(Id,Context->Params[0].Data,32); Id[32]=0;
    xrtMutexLock(g_MdoDt.Lock); xvalue* Task=MdoDtTaskRead(false,Id); uint64 Member=0;
    if (Task) MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("member_id")),&Member);
    if (!Task || !MdoDtMember(Member)) { xrtValueRelease(Task); xrtMutexUnlock(g_MdoDt.Lock); return MdoApiReplyError(Context,404,"transfer_not_found","Transfer not found",NULL); }
    if (Read) {
        xvalue* Queue=MdoDtQueue(false); bool Queued=false;
        for (size_t i=0;i<xrtValueCount(Queue);i++) { xstrview Value; if (xrtValueGetString(xrtValueArrayGet(Queue,i),&Value) && Value.Size==32u && !memcmp(Value.Data,Id,32u)) Queued=true; }
        xrtValueRelease(Queue); MdoAccountSetBool(Task,"queued",Queued);
        xrtMutexUnlock(g_MdoDt.Lock); return MdoApiReplySuccessTake(Context,200,Task,NULL);
    }
    MdoApiJsonBody Body; MdoApiBodyStatus BodyStatus=MdoApiJsonBodyRead(Context,&Body);
    cstr Action=BodyStatus==MDO_API_BODY_OK?MdoAccountText(Body.Value,"action",16u):NULL;
    cstr State=MdoAccountText(Task,"state",32u); bool Ok=false;
    if (Action && !strcmp(Action,"cancel") && !MdoDtTerminal(State)) {
        bool Committing=false; (void)xrtValueGetBool(xrtValueObjectGet(Task,XRT_STR_LITERAL("commit_requested")),&Committing);
        if (Committing) {
            MdoApiJsonBodyUnit(&Body); xrtValueRelease(Task); xrtMutexUnlock(g_MdoDt.Lock);
            return MdoApiReplyError(Context,409,"transfer_commit_in_progress","Target commit was requested; wait for its confirmed result",NULL);
        }
        if (!strcmp(g_MdoDt.Active,Id) && g_MdoDt.WorkCancel) xrtCancelRequest(g_MdoDt.WorkCancel);
        Ok=MdoDtTaskState(Task,"cancelled","");
    } else if (Action && !strcmp(Action,"confirm")) {
        cstr Project=MdoAccountText(Body.Value,"target_project_id",64u),Directory=MdoAccountText(Body.Value,"target_directory",2048u);
        cstr PreviousProject=MdoAccountText(Task,"target_project_id",64u),PreviousDirectory=MdoAccountText(Task,"target_directory",2048u);
        if (Project && Directory && PreviousProject && PreviousDirectory && !strcmp(Project,PreviousProject) && !strcmp(Directory,PreviousDirectory)) {
            MdoApiJsonBodyUnit(&Body); xrtMutexUnlock(g_MdoDt.Lock); return MdoApiReplySuccessTake(Context,200,Task,NULL);
        }
        if (MdoDtIs(State,"awaiting_confirmation")) Ok=Project && Directory && MdoDtPath(Project) && !strchr(Project,'/') &&
                MdoAccountSetString(Task,"target_project_id",Project) && MdoAccountSetString(Task,"target_directory",Directory) && MdoDtTaskState(Task,"waiting_idle","");
    } else if (Action && !strcmp(Action,"resume") && !MdoDtTerminal(State) && strcmp(g_MdoDt.Active,Id)) {
        uint64 Bytes=0; MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("manifest_bytes")),&Bytes);
        bool Confirmed=MdoAccountText(Task,"target_project_id",64u)!=NULL;
        cstr Device=MdoAccountText(Body.Value,"device_id",32u),OldDevice=MdoAccountText(Task,"device_id",32u);
        if (Device && MdoDtId(Device) && OldDevice && strcmp(Device,OldDevice)) {
            if (MdoAccountText(Task,"receiver_id",32u)) (void)MdoAccountSetBool(Task,"reconnect_receiver",true);
            if (!MdoAccountSetString(Task,"device_id",Device)) goto action_done;
        }
        Ok=MdoAccountSetBool(Task,"resume_target",true) && MdoDtTaskState(Task,Bytes?"connecting":Confirmed?"waiting_idle":"planning","");
    }
action_done:
    if (Ok) Ok=MdoDtTaskSave(false,Task) && MdoDtQueueChange(false,Id,true);
    MdoApiJsonBodyUnit(&Body); xrtMutexUnlock(g_MdoDt.Lock);
    if (!Ok) { xrtValueRelease(Task); return MdoApiReplyError(Context,409,"transfer_state_conflict","Reload transfer state before continuing",NULL); }
    return MdoApiReplySuccessTake(Context,202,Task,NULL);
}
