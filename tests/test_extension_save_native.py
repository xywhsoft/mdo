"""Atomic manifest/text saves preserve resources and recover interrupted stages."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static void QaFlag(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
static bool QaContent(cstr Path,cstr Expected){xfile F=MdoHomeOpenRead(Path);char B[4096]={0};size_t N=0;bool Ok=F&&xrtRead(F,B,sizeof(B),&N)&&N==strlen(Expected)&&!memcmp(B,Expected,N);xrtClose(F);return Ok;}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject(),*Out=NULL;char Error[512]={0},Hash[65],Before[65];bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V),Ok=Home&&MdoExtensionsInit()&&MdoExtensionTasksInit();
 bool Atomic=MdoHomeAtomicWrite("probe/file.tmp","preserved",9,false)&&MdoHomeAtomicWrite("probe/file","old",3,false)&&MdoHomeAtomicWrite("probe/file","new",3,true);
 QaFlag(R,"atomic_sibling_resources_preserved",Atomic&&QaContent("probe/file.tmp","preserved")&&QaContent("probe/file.bak","old")&&QaContent("probe/file","new"));
 char Long[280]="probe/";memset(Long+6,'a',255);Long[261]=0;QaFlag(R,"maximum_basename_atomic_write",MdoHomeAtomicWrite(Long,"long",4,false)&&QaContent(Long,"long"));
 bool Collision=MdoHomeAtomicWrite("probe/occupied","keep",4,false),Created=true;
 xrtMutexLock(g_MdoHome.Lock);Collision=Collision&&!MdoHomeWriteTempLocked("probe/occupied","bad",3,&Created)&&!Created;xrtMutexUnlock(g_MdoHome.Lock);xrtClearError();
 QaFlag(R,"exclusive_collision_preserves_other_file",Collision&&QaContent("probe/occupied","keep"));
 xvalue* M=SdkParse("{\"kind\":\"tools\",\"name\":\"Two sources\",\"alias\":\"save-test\",\"entry\":\"src/tool.c\",\"native\":\"src/helper.c\",\"platforms\":[\"any\"]}");
 xvalue* E=SdkParse("[{\"path\":\"src/tool.c\",\"text\":\"// entry\\n\"},{\"path\":\"src/helper.c\",\"text\":\"// helper\\n\"}]");
 Ok=Ok&&MdoExtensionsSave("tools","qa-save",M,E,"",true,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;QaFlag(R,"multiple_declared_files_created_together",Ok);
 Ok=Ok&&MdoHomeAtomicWrite("tools/qa-save/src/tool.c.tmp","preserve sibling",16,false)&&MdoHomeAtomicWrite("tools/qa-save/dependency.bin","binary resource",15,false)&&MdoExtEnsure("tools/qa-save/empty")&&MdoExtensionsFingerprint("tools","qa-save",Hash,NULL,Error);snprintf(Before,sizeof(Before),"%s",Hash);
 xrtValueRelease(E);E=SdkParse("[{\"path\":\"src/tool.c\",\"text\":\"// replaced\\n\"},{\"path\":\".hidden/deep/new.h\",\"text\":\"header\\n\"}]");
 Ok=Ok&&MdoExtensionsSave("tools","qa-save",M,E,Hash,true,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;
 QaFlag(R,"unedited_files_and_siblings_survive",Ok&&QaContent("tools/qa-save/src/tool.c","// replaced\n")&&QaContent("tools/qa-save/src/tool.c.tmp","preserve sibling")&&QaContent("tools/qa-save/dependency.bin","binary resource")&&QaContent("tools/qa-save/.hidden/deep/new.h","header\n"));
 QaFlag(R,"stale_save_keeps_current_directory",!MdoExtensionsSave("tools","qa-save",M,E,Before,true,NULL,&Out,Error)&&QaContent("tools/qa-save/src/tool.c","// replaced\n"));xrtValueRelease(Out);Out=NULL;
 MdoExtensionsFingerprint("tools","qa-save",Hash,NULL,Error);SdkPut(M,"entry","missing.c");
 QaFlag(R,"invalid_complete_directory_keeps_original",!MdoExtensionsSave("tools","qa-save",M,E,Hash,true,NULL,&Out,Error)&&QaContent("tools/qa-save/src/tool.c","// replaced\n"));xrtValueRelease(Out);Out=NULL;SdkPut(M,"entry","src/tool.c");
 xrtValueRelease(E);E=SdkParse("[{\"path\":\"SRC/tool.c\",\"text\":\"case collision\"}]");
 QaFlag(R,"case_alias_cannot_replace_another_file",!MdoExtensionsSave("tools","qa-save",M,E,Hash,true,NULL,&Out,Error)&&QaContent("tools/qa-save/src/tool.c","// replaced\n"));xrtValueRelease(Out);Out=NULL;
 bool Staged=MdoExtStage("data/extension-tasks/qa-orphan","tools","qa-save")&&MdoHomeAtomicWrite("data/extension-tasks/qa-orphan/incoming/file","partial",7,false)&&MdoHomeAtomicWrite("data/extension-tasks/qa-export/archive.zip","owned export",12,false)&&MdoExtensionsRecover(Error);
 bool Exists=true;xfileinfo Info;QaFlag(R,"pretransaction_stage_cleaned_export_retained",Staged&&MdoHomeExternalStat("data/extension-tasks/qa-orphan",&Exists,&Info)&&!Exists&&QaContent("data/extension-tasks/qa-export/archive.zip","owned export"));

 uint8 Binary[2048];for(size_t I=0;I<sizeof(Binary);I++)Binary[I]=(uint8)(I%256);char BinaryHash[65],FileTask[33]={0};MdoExtensionHashBytes(Binary,sizeof(Binary),BinaryHash);xvalue* Start=xrtValueObject();SdkPut(Start,"purpose","file");SdkPut(Start,"sha256",BinaryHash);SdkPutUInt(Start,"bytes",sizeof(Binary));bool StagedFile=MdoExtensionTaskBegin(Start,&Out,Error);if(StagedFile)snprintf(FileTask,sizeof(FileTask),"%s",SdkField(Out,"id"));xrtValueRelease(Out);Out=NULL;xrtValueRelease(Start);StagedFile=StagedFile&&MdoExtensionTaskWrite(FileTask,0,Binary,sizeof(Binary),&Out,Error);xrtValueRelease(Out);Out=NULL;StagedFile=StagedFile&&MdoExtensionTaskSeal(FileTask,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;
 MdoExtensionsFingerprint("tools","qa-save",Hash,NULL,Error);xrtValueRelease(E);E=SdkParse("[{\"path\":\"dependency.bin\"},{\"path\":\"src/tool.c\",\"text\":\"// binary save\\n\"}]");SdkPut(xrtValueArrayGet(E,0),"task",FileTask);mdo_sdk_context_v1 C={0};C.Size=sizeof(C);C.Version=MDO_SDK_VERSION;C.Api=MdoApplicationApi();C.ExtensionId="file-task-author";mdo_error_v1 Failure={0};xvalue* Request=SdkParse("{\"kind\":\"tools\",\"id\":\"qa-save\",\"trust_code\":true}");SdkPut(Request,"expected_revision",Hash);xrtValueObjectSet(Request,XRT_STR_LITERAL("manifest"),M);xrtValueObjectSet(Request,XRT_STR_LITERAL("edits"),E);bool BinarySave=StagedFile&&mdoCall(&C,"extension.save",Request,&Out,&Failure)==MDO_RESULT_OK;xrtValueRelease(Request);xrtValueRelease(Out);Out=NULL;
 xfile Resource=MdoHomeOpenRead("tools/qa-save/dependency.bin");uint8 Actual[2048];BinarySave=BinarySave&&Resource&&xrtReadFull(Resource,Actual,sizeof(Actual),NULL)&&!memcmp(Binary,Actual,sizeof(Binary))&&QaContent("tools/qa-save/src/tool.c","// binary save\n");xrtClose(Resource);Out=MdoExtensionTaskRead(FileTask,Error);BinarySave=BinarySave&&Out&&!strcmp(SdkField(Out,"state"),"complete");xrtValueRelease(Out);Out=NULL;QaFlag(R,"raw_resource_and_text_commit_atomically",BinarySave);
 Start=xrtValueObject();SdkPut(Start,"purpose","file");SdkPut(Start,"sha256",BinaryHash);SdkPutUInt(Start,"bytes",sizeof(Binary));StagedFile=MdoExtensionTaskBegin(Start,&Out,Error);if(StagedFile)snprintf(FileTask,sizeof(FileTask),"%s",SdkField(Out,"id"));xrtValueRelease(Out);Out=NULL;xrtValueRelease(Start);StagedFile=StagedFile&&MdoExtensionTaskWrite(FileTask,0,Binary,sizeof(Binary),&Out,Error);xrtValueRelease(Out);Out=NULL;StagedFile=StagedFile&&MdoExtensionTaskSeal(FileTask,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;
 char* FilePath=MdoExtensionTaskArchive(FileTask,Error);Actual[0]^=1;bool Tampered=StagedFile&&FilePath&&MdoHomeAtomicWrite(FilePath,Actual,sizeof(Actual),false);xrtFree(FilePath);MdoExtensionsFingerprint("tools","qa-save",Hash,NULL,Error);snprintf(Before,sizeof(Before),"%s",Hash);SdkPut(xrtValueArrayGet(E,0),"task",FileTask);Tampered=Tampered&&!MdoExtensionsSave("tools","qa-save",M,E,Hash,true,NULL,&Out,Error)&&MdoExtensionsFingerprint("tools","qa-save",Hash,NULL,Error)&&!strcmp(Before,Hash);xrtValueRelease(Out);Out=NULL;QaFlag(R,"tampered_resource_task_preserves_original_directory",Tampered);
 SdkPut(xrtValueArrayGet(E,0),"base64","ZmlsZQ==");QaFlag(R,"binary_json_save_fields_rejected",!MdoExtensionsSave("tools","qa-save",M,E,Hash,true,NULL,&Out,Error));xrtValueRelease(Out);Out=NULL;MdoExtensionTaskCancel(FileTask,&Out,Error);xrtValueRelease(Out);Out=NULL;
 xrtValueRelease(M);xrtValueRelease(E);SdkPut(R,"error",Error);MdoExtensionTasksUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/extension-save-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir()
    try:
        result=product(a,fixture,CONTRACT,out/'result.json')
        for key,value in result.items():
            if key!='error': assert value is True,(key,result)
        assert len(result)>=10,result
        assert not list((fixture/'probe').glob('.mdo-tmp-*'))
        print(json.dumps(result,ensure_ascii=False))
    finally:
        if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'): shutil.rmtree('\\\\?\\'+str(fixture.resolve()))

if __name__=='__main__': main()
