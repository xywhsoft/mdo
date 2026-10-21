#include "../../include/mdo/time.h"
#include "internal.h"

static xvalue* MdoDtRemote(MdoDtRpc* Rpc,cstr Method,cstr Path,const xvalue* Value) {
    size_t Bytes=0; str Json=Value?xrtJsonStringify(Value,false,&Bytes):NULL;
    xvalue* Response=MdoDtRpcCall(Rpc,Method,Path,Json,Bytes,true); xrtFree(Json);
    bool Success=false; (void)xrtValueGetBool(xrtValueObjectGet(Response,XRT_STR_LITERAL("ok")),&Success);
    const xvalue* Failure=xrtValueObjectGet(Response,XRT_STR_LITERAL("error"));
    cstr Code=MdoAccountText(Failure,"code",64u),Message=MdoAccountText(Failure,"message",512u);
    if (Code) snprintf(Rpc->ErrorCode,sizeof(Rpc->ErrorCode),"%s",Code);
    if (Message) snprintf(Rpc->ErrorMessage,sizeof(Rpc->ErrorMessage),"%s",Message);
    xvalue* Data=Success && Rpc->Status>=200u && Rpc->Status<300u?xrtValueClone(xrtValueObjectGet(Response,XRT_STR_LITERAL("data"))):NULL;
    xrtValueRelease(Response); return Data;
}
static bool MdoDtUpload(MdoDtRpc* Rpc,cstr Source,cstr Prefix,uint64 Offset,uint64 Total,xvalue* Task) {
    xfile File=MdoHomeOpenRead(Source); uint8* Buffer=xrtMalloc(MDO_DT_CHUNK); bool Ok=false;
    if (!File || !Buffer || Offset>Total || !xrtSeek(File,(int64)Offset,XSEEK_START,NULL)) goto done;
    bool Seal=Offset==Total;
    while ((Offset<Total || Seal) && !xrtCancelRequested(Rpc->Cancel)) {
        Seal=false;
        size_t Read=0; size_t Size=Total-Offset>MDO_DT_CHUNK?MDO_DT_CHUNK:(size_t)(Total-Offset);
        if (Size && (!xrtRead(File,Buffer,Size,&Read) || Read!=Size)) goto done;
        char Path[256]; snprintf(Path,sizeof(Path),"%s/%llu",Prefix,(unsigned long long)Offset);
        xvalue* Response=MdoDtRpcCall(Rpc,"PUT",Path,Buffer,Size,false); bool Success=false;
        (void)xrtValueGetBool(xrtValueObjectGet(Response,XRT_STR_LITERAL("ok")),&Success);
        const xvalue* Failure=xrtValueObjectGet(Response,XRT_STR_LITERAL("error"));
        cstr Code=MdoAccountText(Failure,"code",64u),Message=MdoAccountText(Failure,"message",512u);
        if (Code) snprintf(Rpc->ErrorCode,sizeof(Rpc->ErrorCode),"%s",Code);
        if (Message) snprintf(Rpc->ErrorMessage,sizeof(Rpc->ErrorMessage),"%s",Message);
        xrtValueRelease(Response);
        if (!Success || Rpc->Status<200 || Rpc->Status>=300) goto done;
        Offset+=Size; MdoAccountSetUInt(Task,"current_file_bytes",Offset);
        MdoAccountSetUInt(Task,"current_file_total",Total);
        MdoAccountSetString(Task,"route",Rpc->DirectReady?"lan":"relay");
        MdoDtTaskState(Task,"sending",""); if (!MdoDtTaskSave(false,Task)) goto done;
        MdoDtKeepAwake();
    }
    Ok=Offset==Total;
done:
    xrtFree(Buffer); xrtClose(File); return Ok;
}
bool MdoDtSend(xvalue* Task,xcancel* Cancel) {
    cstr Id=MdoAccountText(Task,"id",32u),Device=MdoAccountText(Task,"device_id",32u);
    cstr State=MdoAccountText(Task,"state",32u); bool Planning=State && !strcmp(State,"planning");
    uint64 Member=0; MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("member_id")),&Member);
    MdoDtRpc Rpc={0}; xvalue* Data=NULL; xvalue* Manifest=NULL; bool Ok=false;
    char Path[256],Source[256],Prefix[256],Leaf[64];
    if (!MdoDtRpcConnect(&Rpc,g_MdoDt.Server,Device,Member,Cancel)) goto done;
    Data=MdoDtRemote(&Rpc,"GET","/api/v1/device-transfers/capabilities",NULL);
    if (!Data) goto done;
    bool Receive=false; (void)xrtValueGetBool(xrtValueObjectGet(Data,XRT_STR_LITERAL("allow_receive")),&Receive);
    if (!Receive) {
        MdoDtTaskState(Task,"paused","目标设备尚未允许接收，请在目标设备的远控设置中开启接收，再重试"); goto done;
    }
    MdoApiValueSetInt(Task,"last_connected_at",MdoUtcUs());
    xrtValueRelease(Data); Data=NULL;
    if (Planning) {
        Data=MdoDtRemote(&Rpc,"GET","/api/v1/projects",NULL);
        if (!Data) goto done;
        xvalue* Projects=xrtValueClone(xrtValueObjectGet(Data,XRT_STR_LITERAL("items")));
        Ok=Projects && xrtValueObjectSetTake(Task,XRT_STR_LITERAL("target_projects"),&Projects) &&
            MdoDtTaskState(Task,"awaiting_confirmation","") && MdoDtTaskSave(false,Task);
        xrtValueRelease(Projects); goto done;
    }
    MdoDtTaskPath(Source,false,Id,"manifest.json"); Manifest=MdoDtRead(Source,MDO_DT_MANIFEST);
    if (!MdoDtManifestValid(Manifest)) { MdoDtTaskState(Task,"paused","准备的数据清单损坏，请取消此任务后重新发送"); goto done; }
    xvalue* Offer=xrtValueObject();
    const char* Fields[]={"member_id","manifest_bytes","manifest_sha256","total_bytes","kind","target_project_id","target_directory","source_device"};
    bool Built=Offer!=NULL;
    for (size_t i=0;i<sizeof(Fields)/sizeof(*Fields);i++) {
        xvalue* Value=xrtValueClone(xrtValueObjectGet(Task,xrtStrView(Fields[i])));
        if (!Value || !xrtValueObjectSetTake(Offer,xrtStrView(Fields[i]),&Value)) Built=false;
        xrtValueRelease(Value);
    }
    snprintf(Prefix,sizeof(Prefix),"/api/v1/device-transfers/incoming/%s",Id);
    bool ReconnectExisting=false;
    (void)xrtValueGetBool(xrtValueObjectGet(Task,XRT_STR_LITERAL("reconnect_receiver")),&ReconnectExisting);
    if (ReconnectExisting) {
        xvalue* Existing=MdoDtRemote(&Rpc,"GET",Prefix,NULL);
        cstr Expected=MdoAccountText(Task,"receiver_id",32u),Actual=MdoAccountText(Existing,"receiver_id",32u);
        cstr ExpectedHash=MdoAccountText(Task,"manifest_sha256",64u),ActualHash=MdoAccountText(Existing,"manifest_sha256",64u);
        bool Same=Expected && Actual && ExpectedHash && ActualHash && !strcmp(Expected,Actual) && !strcmp(ExpectedHash,ActualHash);
        xrtValueRelease(Existing);
        if (!Same) {
            xrtValueRelease(Offer); MdoDtTaskState(Task,"paused","所选设备不包含原传输任务，请选择原接收设备重新授权后的设备记录"); goto done;
        }
        MdoAccountSetBool(Task,"reconnect_receiver",false);
    }
    Data=Built?MdoDtRemote(&Rpc,"POST",Prefix,Offer):NULL; xrtValueRelease(Offer);
    if (!Data) goto done;
    cstr ReceiverId=MdoAccountText(Data,"receiver_id",32u);
    if (!MdoDtId(ReceiverId) || !MdoAccountSetString(Task,"receiver_id",ReceiverId) || !MdoDtTaskSave(false,Task)) goto done;
    for (;;) {
        cstr RemoteStateValue=MdoAccountText(Data,"state",32u); char RemoteState[33];
        if (!RemoteStateValue || xrtCancelRequested(Cancel)) goto done;
        strcpy(RemoteState,RemoteStateValue);
        if (!strcmp(RemoteState,"completed")) {
            const char* Results[]={"destination_project_id","destination_session_id","destination_workspace"};
            for (size_t i=0;i<3;i++) {
                cstr Text=MdoAccountText(Data,Results[i],2048u);
                if (Text) MdoAccountSetString(Task,Results[i],Text);
            }
            MdoDtTaskState(Task,"completed",""); Ok=MdoDtTaskSave(false,Task); goto done;
        }
        if (!strcmp(RemoteState,"paused") || !strcmp(RemoteState,"cancelled")) {
            bool Resume=false; (void)xrtValueGetBool(xrtValueObjectGet(Task,XRT_STR_LITERAL("resume_target")),&Resume);
            if (Resume && !strcmp(RemoteState,"paused")) {
                xvalue* Empty=xrtValueObject(); snprintf(Path,sizeof(Path),"%s/resume",Prefix);
                xrtValueRelease(Data); Data=MdoDtRemote(&Rpc,"POST",Path,Empty); xrtValueRelease(Empty);
                MdoAccountSetBool(Task,"resume_target",false); MdoDtTaskSave(false,Task);
                if (!Data) goto done;
                continue;
            }
            cstr Message=MdoAccountText(Data,"error",2048u);
            MdoDtTaskState(Task,"paused",Message && *Message?Message:"目标传输已暂停，请检查目标设备后重试"); goto done;
        }
        xrtValueRelease(Data); Data=NULL;
        if (!strcmp(RemoteState,"receiving_manifest")) {
            /* Query offsets from persistent target files, never transport receipts. */
            Data=MdoDtRemote(&Rpc,"GET",Prefix,NULL); if (!Data) goto done;
            cstr Actual=MdoAccountText(Data,"state",32u);
            if (Actual && strcmp(Actual,"receiving_manifest")) continue;
            uint64 Offset=0,Total=0;
            MdoAccountGetUInt(xrtValueObjectGet(Data,XRT_STR_LITERAL("manifest_offset")),&Offset);
            MdoAccountGetUInt(xrtValueObjectGet(Task,XRT_STR_LITERAL("manifest_bytes")),&Total);
            xrtValueRelease(Data); Data=NULL;
            snprintf(Path,sizeof(Path),"%s/manifest",Prefix); MdoDtTaskPath(Source,false,Id,"manifest.json");
            if (!MdoDtUpload(&Rpc,Source,Path,Offset,Total,Task)) goto done;
        } else if (!strcmp(RemoteState,"receiving_files")) {
            Data=MdoDtRemote(&Rpc,"GET",Prefix,NULL); if (!Data) goto done;
            cstr Actual=MdoAccountText(Data,"state",32u);
            if (Actual && strcmp(Actual,"receiving_files")) continue;
            uint64 Index=0,Offset=0;
            MdoAccountGetUInt(xrtValueObjectGet(Data,XRT_STR_LITERAL("next_file")),&Index);
            MdoAccountGetUInt(xrtValueObjectGet(Data,XRT_STR_LITERAL("offset")),&Offset);
            const xvalue* Files=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files"));
            xrtValueRelease(Data); Data=NULL;
            if (Index==xrtValueCount(Files)) {
                MdoAccountSetBool(Task,"commit_requested",true); MdoDtTaskState(Task,"waiting_target","");
                if (!MdoDtTaskSave(false,Task)) goto done;
                xvalue* Empty=xrtValueObject(); snprintf(Path,sizeof(Path),"%s/commit",Prefix);
                Data=MdoDtRemote(&Rpc,"POST",Path,Empty); xrtValueRelease(Empty);
                if (!Data) goto done;
                MdoDtTaskState(Task,"waiting_target",""); MdoDtTaskSave(false,Task); continue;
            }
            const xvalue* Row=xrtValueArrayGet(Files,(size_t)Index); uint64 Total=0;
            if (!Row || !MdoAccountGetUInt(xrtValueObjectGet(Row,XRT_STR_LITERAL("bytes")),&Total)) goto done;
            MdoAccountSetUInt(Task,"file_index",Index);
            MdoAccountSetString(Task,"current_file",MdoAccountText(Row,"path",2048u));
            snprintf(Leaf,sizeof(Leaf),"files/%08u",(unsigned)Index); MdoDtTaskPath(Source,false,Id,Leaf);
            snprintf(Path,sizeof(Path),"%s/files/%u",Prefix,(unsigned)Index);
            if (!MdoDtUpload(&Rpc,Source,Path,Offset,Total,Task)) goto done;
        } else {
            MdoDtTaskState(Task,"waiting_target",""); MdoDtTaskSave(false,Task);
            for (unsigned i=0;i<50u && !xrtCancelRequested(Cancel);i++) { MdoDtKeepAwake(); xrtSleep(20u); }
        }
        Data=MdoDtRemote(&Rpc,"GET",Prefix,NULL); if (!Data) goto done;
    }
done:
    if (!Ok && !xrtCancelRequested(Cancel)) {
        if (Rpc.ErrorCode[0]) MdoAccountSetString(Task,"error_code",Rpc.ErrorCode);
        cstr Existing=MdoAccountText(Task,"state",32u);
        if (!Existing || strcmp(Existing,"paused")) {
            bool Terminal=(Rpc.Status>=400u && Rpc.Status<500u && Rpc.Status!=408u && Rpc.Status!=409u && Rpc.Status!=429u) || Rpc.Status==507u || !strcmp(Rpc.ErrorCode,"transfer_queue_full");
            MdoDtTaskState(Task,Terminal?"paused":Planning?"planning":"connecting",Rpc.Status==426u?"目标设备版本不支持传输，请更新两端 mdo":
                Rpc.ErrorMessage[0]?Rpc.ErrorMessage:Terminal?"目标设备拒绝接收，请检查接收开关、账号及保存位置":"连接中断，正在自动重连并核对续传位置");
        }
    }
    xrtValueRelease(Data); xrtValueRelease(Manifest); MdoDtRpcClose(&Rpc); return Ok;
}
