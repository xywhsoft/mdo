"""Raw ZIP staging, replay/partial-write recovery, integrity and quota tests."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
import zipfile
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static void QaTaskFlag(xvalue* R,cstr K,bool B){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(B));}
static bool QaBeginTask(cstr Purpose,cstr Kind,const void* Data,size_t Bytes,char Id[33],char Error[512]){
 char Hash[65];xvalue* P=xrtValueObject(),*Out=NULL;bool Ok=MdoExtensionHashBytes(Data,Bytes,Hash)&&SdkPut(P,"purpose",Purpose)&&SdkPut(P,"kind",Kind)&&SdkPut(P,"sha256",Hash)&&SdkPutUInt(P,"bytes",Bytes)&&MdoExtensionTaskBegin(P,&Out,Error);
 if(Ok)snprintf(Id,33,"%s",SdkField(Out,"id"));xrtValueRelease(P);xrtValueRelease(Out);return Ok;
}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();char Error[512]={0},Id[33]={0};bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);bool Ok=Home&&MdoExtensionsInit()&&MdoExtensionTasksInit();
 xfile Source=MdoHomeOpenRead("payload.zip");uint64 Bytes=0;Ok=Ok&&Source&&xrtFileSize(Source,&Bytes)&&Bytes>500&&Bytes<65536;uint8* Data=Ok?xrtMalloc((size_t)Bytes):NULL;Ok=Ok&&Data&&xrtReadFull(Source,Data,(size_t)Bytes,NULL);xrtClose(Source);
 Ok=Ok&&QaBeginTask("install","skills",Data,(size_t)Bytes,Id,Error);QaTaskFlag(R,"begin",Ok);xvalue* Out=NULL;
 Ok=Ok&&MdoExtensionTaskWrite(Id,0,Data,100,&Out,Error);xrtValueRelease(Out);Out=NULL;
 Ok=Ok&&MdoExtensionTaskWrite(Id,0,Data,100,&Out,Error);xrtValueRelease(Out);Out=NULL;QaTaskFlag(R,"identical_chunk_replay",Ok);
 uint8 Wrong[100];memcpy(Wrong,Data,100);Wrong[0]^=1;QaTaskFlag(R,"conflicting_replay_rejected",!MdoExtensionTaskWrite(Id,0,Wrong,100,&Out,Error));xrtValueRelease(Out);Out=NULL;
 QaTaskFlag(R,"gap_rejected",!MdoExtensionTaskWrite(Id,101,Data,1,&Out,Error));xrtValueRelease(Out);Out=NULL;
 char* Part=MdoExtArchivePath(Id,"archive.part");xfile F=MdoHomeOpenWrite(Part,XFILE_WRITE);bool Partial=F&&xrtWriteAtFull(F,100,Data+100,50,NULL)&&xrtFlush(F);xrtClose(F);xrtFree(Part);
 MdoExtensionTasksUnit();Partial=Partial&&MdoExtensionTasksInit();Out=MdoExtensionTaskRead(Id,Error);uint64 Received=0;Partial=Partial&&MdoExtArchiveNumber(Out,"received",&Received)&&Received==150;QaTaskFlag(R,"restart_retains_real_offset",Partial);xrtValueRelease(Out);Out=NULL;
 bool Incomplete=!MdoExtensionTaskSeal(Id,NULL,&Out,Error);xrtValueRelease(Out);Out=MdoExtensionTaskRead(Id,Error);Incomplete=Incomplete&&Out&&!strcmp(SdkField(Out,"state"),"receiving");QaTaskFlag(R,"early_seal_can_resume",Incomplete);xrtValueRelease(Out);Out=NULL;
 Ok=Partial&&MdoExtensionTaskWrite(Id,100,Data+100,200,&Out,Error);xrtValueRelease(Out);Out=NULL;
 Ok=Ok&&MdoExtensionTaskWrite(Id,300,Data+300,(size_t)Bytes-300,&Out,Error);xrtValueRelease(Out);Out=NULL;QaTaskFlag(R,"partial_chunk_completed",Ok);
 bool Sealed=Ok&&MdoExtensionTaskSeal(Id,NULL,&Out,Error)&&!strcmp(SdkField(Out,"state"),"ready");QaTaskFlag(R,"hash_and_zip_sealed",Sealed);xrtValueRelease(Out);Out=NULL;
 char* Archive=MdoExtensionTaskArchive(Id,Error);char* Record=MdoExtArchivePath(Id,"task.json"),*Text=Record?MdoExtensionRead(Record,true,16384,NULL):NULL;
 QaTaskFlag(R,"receipt_contains_no_file_payload",Text&&!strstr(Text,"ARCHIVE-BINARY-CONTENT-MARKER")&&!strstr(Text,"base64")&&!strstr(Text,"resources"));xrtFree(Record);xrtFree(Text);
 bool Installed=Archive&&MdoExtensionsInstall("skills","0123456789ABCDEFGHJKMNPQRSTVWXYZ",Archive,NULL,false,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;
 QaTaskFlag(R,"sealed_zip_installs_directory",Installed);SdkPut(R,"archive",Archive);xrtFree(Archive);
 bool Removed=MdoExtensionsRemove("skills","0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Out,Error);xrtValueRelease(Out);Out=NULL;QaTaskFlag(R,"installed_directory_removed",Removed);
 char BadId[33]={0};cstr Json="{\"format\":\"mdo.extension.v2\",\"resources\":[]}";bool Legacy=QaBeginTask("install","skills",Json,strlen(Json),BadId,Error)&&MdoExtensionTaskWrite(BadId,0,Json,strlen(Json),&Out,Error);xrtValueRelease(Out);Out=NULL;
 Legacy=Legacy&&!MdoExtensionTaskSeal(BadId,NULL,&Out,Error);xrtValueRelease(Out);Out=MdoExtensionTaskRead(BadId,Error);Legacy=Legacy&&Out&&!strcmp(SdkField(Out,"state"),"failed");QaTaskFlag(R,"legacy_json_is_not_an_archive",Legacy);xrtValueRelease(Out);Out=NULL;
 char Waiting[3][33]={{0}},Fourth[33]={0};bool Quota=true;
 for(size_t I=0;I<3;I++)Quota=Quota&&QaBeginTask("review","",Json,strlen(Json),Waiting[I],Error);
 Quota=Quota&&!QaBeginTask("review","",Json,strlen(Json),Fourth,Error);QaTaskFlag(R,"active_task_quota",Quota);
 bool Cancel=MdoExtensionTaskCancel(Id,&Out,Error);xrtValueRelease(Out);Out=NULL;Archive=MdoExtensionTaskArchive(Id,Error);Cancel=Cancel&&!Archive;xrtFree(Archive);QaTaskFlag(R,"cancel_removes_archive_access",Cancel);
 bool Clear=MdoExtensionTaskForget(Id,Error)&&MdoExtensionTaskCancel(BadId,&Out,Error);xrtValueRelease(Out);Out=NULL;Clear=Clear&&MdoExtensionTaskForget(BadId,Error);
 for(size_t I=0;I<3;I++){Clear=Clear&&MdoExtensionTaskCancel(Waiting[I],&Out,Error);xrtValueRelease(Out);Out=NULL;Clear=Clear&&MdoExtensionTaskForget(Waiting[I],Error);}
 xvalue* List=MdoExtensionTasksList(Error);Clear=Clear&&List&&!xrtValueCount(List);xrtValueRelease(List);QaTaskFlag(R,"explicit_cleanup",Clear);
 QaTaskFlag(R,"task_path_escape_rejected",!MdoExtensionTaskRead("../outside",Error));
 char Staged[32][33]={{0}};bool Batch=true;const char* Small="qa";
 for(size_t I=0;I<32;I++){Batch=Batch&&QaBeginTask("file","mcp",Small,2,Staged[I],Error)&&MdoExtensionTaskWrite(Staged[I],0,Small,2,&Out,Error);xrtValueRelease(Out);Out=NULL;
  Batch=Batch&&MdoExtensionTaskSeal(Staged[I],NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;}
 QaTaskFlag(R,"thirty_two_sealed_files_leave_transfer_slots",Batch);
 for(size_t I=0;I<32;I++)if(Staged[I][0]){MdoExtensionTaskCancel(Staged[I],&Out,Error);xrtValueRelease(Out);Out=NULL;MdoExtensionTaskForget(Staged[I],Error);}
 char Owned[33]={0};bool OwnedOk=QaBeginTask("install","skills",Data,(size_t)Bytes,Owned,Error);MdoExtensionTaskUse* Owner=OwnedOk?MdoExtensionTaskUseBegin(Owned,true,NULL,Error):NULL;
 bool Blocked=Owner&&!MdoExtensionTaskWrite(Owned,0,Data,(size_t)Bytes,&Out,Error);xrtValueRelease(Out);Out=NULL;
 OwnedOk=Blocked&&MdoExtensionTaskUseWrite(Owner,0,Data,(size_t)Bytes,&Out,Error);xrtValueRelease(Out);Out=NULL;
 QaTaskFlag(R,"only_active_receiver_can_write_owned_task",OwnedOk);
 xvalue* Origin=SdkParse("{\"kind\":\"skills\",\"xid\":\"0123456789ABCDEFGHJKMNPQRSTVWXYZ\",\"version_id\":\"1123456789ABCDEFGHJKMNPQRSTVWXYZ\",\"unexpected_secret\":\"fixture\"}");char OwnedHash[65];MdoExtensionHashBytes(Data,(size_t)Bytes,OwnedHash);SdkPut(Origin,"sha256",OwnedHash);
 bool OriginOk=Owner&&!MdoExtensionTaskOrigin(Owner,Origin,Error);xrtValueObjectRemove(Origin,XRT_STR_LITERAL("unexpected_secret"));OriginOk=OriginOk&&MdoExtensionTaskOrigin(Owner,Origin,Error);xrtValueRelease(Origin);QaTaskFlag(R,"verified_origin_rejects_unexpected_fields",OriginOk);
 MdoExtensionTaskUseEnd(Owner);OwnedOk=OwnedOk&&MdoExtensionTaskSeal(Owned,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;Owner=OwnedOk?MdoExtensionTaskUseBegin(Owned,false,NULL,Error):NULL;
 bool Progress=Owner&&MdoExtensionTaskTransfer(Owner,Bytes,"submitting",Error)&&!MdoExtensionTaskTransfer(Owner,Bytes+1,"uploading",Error)&&MdoExtensionTaskCancel(Owned,&Out,Error);xrtValueRelease(Out);Out=NULL;
 Progress=Progress&&!MdoExtensionTaskTransfer(Owner,Bytes,"submitting",Error);MdoExtensionTaskUseEnd(Owner);Progress=Progress&&MdoExtensionTaskForget(Owned,Error);QaTaskFlag(R,"progress_persists_and_honors_cancellation",Progress);

 /* Exercise the actual product router, write admission, body decoder and SDK
  * adapter. Only the server identity is supplied; no service/socket is made. */
 XS_ServerInfo Server={0};Server.Name="archive-fixture";Server.Port=58013;SdkServer=&Server;g_MdoApiInitialized=true;bool Api=MdoApiWriteInit()&&MdoApiExtensionTasksInit();
 mdo_sdk_context_v1 C={0};C.Size=sizeof(C);C.Version=MDO_SDK_VERSION;C.Api=MdoApplicationApi();C.ExtensionId="archive-fixture";C.Origin=MDO_ORIGIN_USER;mdo_error_v1 Failure={0};
 xvalue* LegacyBody=SdkParse("{\"body\":{\"action\":\"import\",\"package\":{\"format\":\"mdo.extension.v2\",\"resources\":[]}}}");bool Retired=mdoCall(&C,"product.ecosystem.post",LegacyBody,&Out,&Failure)!=MDO_RESULT_OK;QaTaskFlag(R,"ecosystem_rejects_json_distribution",Retired);xrtValueRelease(Out);Out=NULL;xrtValueRelease(LegacyBody);

 char Hash[65];MdoExtensionHashBytes(Data,(size_t)Bytes,Hash);xvalue* Body=xrtValueObject(),*Request=xrtValueObject();SdkPut(Body,"purpose","install");SdkPut(Body,"kind","skills");SdkPut(Body,"sha256",Hash);SdkPutUInt(Body,"bytes",Bytes);xrtValueObjectSet(Request,XRT_STR_LITERAL("body"),Body);
 Api=Api&&mdoCall(&C,"product.extension_tasks.post",Request,&Out,&Failure)==MDO_RESULT_OK;char SdkId[33]={0};if(Api)snprintf(SdkId,sizeof(SdkId),"%s",SdkField(SdkGet(Out,"data"),"id"));xrtValueRelease(Out);Out=NULL;xrtValueRelease(Body);xrtValueRelease(Request);
 Request=SdkParse("{\"path\":{\"offset\":\"0\"}}");xvalue* Path=xrtValueObjectGet(Request,XRT_STR_LITERAL("path"));SdkPut(Path,"task",SdkId);char* Native=xrtFormat("%s/payload.zip",SdkField(P,"root"));SdkPut(Request,"body_file",Native);xrtFree(Native);SdkPutUInt(Request,"body_bytes",Bytes);
 Api=Api&&mdoCall(&C,"product.extension_tasks.by_task.chunks.by_offset.put",Request,&Out,&Failure)==MDO_RESULT_OK;QaTaskFlag(R,"sdk_raw_upload_from_file",Api);xrtValueRelease(Out);Out=NULL;
 SdkPut(Request,"body_base64","UEs=");bool Reject=mdoCall(&C,"product.extension_tasks.by_task.chunks.by_offset.put",Request,&Out,&Failure)!=MDO_RESULT_OK;QaTaskFlag(R,"sdk_archive_base64_rejected",Reject);xrtValueRelease(Out);Out=NULL;xrtValueObjectRemove(Request,XRT_STR_LITERAL("body_base64"));
 Api=Api&&mdoCall(&C,"product.extension_tasks.by_task.seal.post",Request,&Out,&Failure)==MDO_RESULT_OK;QaTaskFlag(R,"sdk_seal_via_real_handler",Api);xrtValueRelease(Out);Out=NULL;
 Native=xrtFormat("%s/sdk-download.zip",SdkField(P,"root"));SdkPut(Request,"response_file",Native);xrtFree(Native);Api=Api&&mdoCall(&C,"product.extension_tasks.by_task.file.by_offset.get",Request,&Out,&Failure)==MDO_RESULT_OK&&!SdkGet(Out,"base64");
 F=MdoHomeOpenRead("sdk-download.zip");char Actual[65];uint64 ActualBytes=0;Api=Api&&F&&MdoExtFileDigest(F,NULL,Actual,&ActualBytes)&&ActualBytes==Bytes&&!strcmp(Actual,Hash);xrtClose(F);QaTaskFlag(R,"sdk_raw_download_to_file",Api);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Request);
 xvalue* Install=xrtValueObject();SdkPut(Install,"id","sdk-installed");SdkPut(Install,"expected_revision","");xrtValueObjectSet(Request=xrtValueObject(),XRT_STR_LITERAL("body"),Install);Path=xrtValueObject();SdkPut(Path,"task",SdkId);xrtValueObjectSet(Request,XRT_STR_LITERAL("path"),Path);
 bool Applied=Api&&mdoCall(&C,"product.extension_tasks.by_task.install.post",Request,&Out,&Failure)==MDO_RESULT_OK;xrtValueRelease(Out);Out=MdoExtensionTaskRead(SdkId,Error);uint64 Stored=1;Applied=Applied&&Out&&!strcmp(SdkField(Out,"state"),"complete")&&MdoExtArchiveNumber(Out,"stored_bytes",&Stored)&&!Stored;QaTaskFlag(R,"completed_install_releases_temporary_archive",Applied);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Request);xrtValueRelease(Path);xrtValueRelease(Install);
 MdoExtensionsRemove("skills","sdk-installed",&Out,Error);xrtValueRelease(Out);Out=NULL;SdkPut(R,"sdk_error",Failure.Message);MdoExtensionTaskCancel(SdkId,&Out,Error);xrtValueRelease(Out);MdoExtensionTaskForget(SdkId,Error);
 char CancelId[33]={0};bool Pinned=QaBeginTask("review","",Json,strlen(Json),CancelId,Error)&&MdoExtensionTaskWrite(CancelId,0,Json,strlen(Json),&Out,Error);xrtValueRelease(Out);Out=NULL;Pinned=Pinned&&MdoExtensionTaskSeal(CancelId,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;
 MdoExtensionTaskUse* Use=Pinned?MdoExtensionTaskUseBegin(CancelId,false,NULL,Error):NULL;Pinned=Pinned&&Use&&MdoExtensionTaskCancel(CancelId,&Out,Error)&&!strcmp(SdkField(Out,"state"),"cancel_requested")&&xrtCancelRequested(MdoExtensionTaskUseCancel(Use))&&!MdoExtensionTaskForget(CancelId,Error);xrtValueRelease(Out);Out=NULL;
 MdoExtensionTaskUseEnd(Use);Out=MdoExtensionTaskRead(CancelId,Error);Pinned=Pinned&&Out&&!strcmp(SdkField(Out,"state"),"cancelled");xrtValueRelease(Out);Out=NULL;Pinned=Pinned&&MdoExtensionTaskForget(CancelId,Error);QaTaskFlag(R,"cancellation_waits_for_file_owner",Pinned);
 g_MdoApiInitialized=false;SdkServer=NULL;MdoApiExtensionTasksUnit();MdoApiWriteUnit();MdoExtensionTasksUnit();xrtFree(Data);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('--url', required=True); p.add_argument('--home', type=Path, required=True); p.add_argument('--project', default='default'); a = p.parse_args()
    out = ROOT / '.build/extension-tasks-native'; out.mkdir(parents=True, exist_ok=True); fixture = out / ('home-' + uuid.uuid4().hex[:12]); fixture.mkdir()
    with zipfile.ZipFile(fixture / 'payload.zip', 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('extension.json', json.dumps(dict(kind='skills', name='Uploaded skill', alias='uploaded-skill', entry='SKILL.md', platforms=['any'])))
        z.writestr('SKILL.md', '---\nname: uploaded-skill\ndescription: Test\n---\nARCHIVE-BINARY-CONTENT-MARKER\n')
        z.writestr('.hidden.bin', bytes(range(256)) * 4)
        z.writestr('empty/', b'')
    try:
        result = product(a, fixture, CONTRACT, out / 'result.json')
        for key, value in result.items():
            if key not in ('archive', 'sdk_error'): assert value is True, (key, result)
        assert len(result) >= 15, result
        print(json.dumps(result, ensure_ascii=False))
    finally:
        if fixture.resolve().parent == out.resolve() and fixture.name.startswith('home-'): shutil.rmtree(fixture)


if __name__ == '__main__': main()
