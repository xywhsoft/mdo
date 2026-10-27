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
 xvalue* R=xrtValueObject(),*Out=NULL;char Error[512]={0},Hash[65],Before[65];bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V),Ok=Home&&MdoExtensionsInit();
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
 xrtValueRelease(M);xrtValueRelease(E);SdkPut(R,"error",Error);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
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
