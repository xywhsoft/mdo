"""Real SQLite/ZIP publication, dependency plans and identity ownership."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
import zipfile
from locked_sdk import sdk_root
from native_product_runner import product
from extension_zip_fixture import directory_zip, platform_store_source
from test_platform_directory_store_native import CONTRACT, PLATFORM

ROOT=Path(__file__).resolve().parents[1]
TEST=r'''
static xvalue* QaDependencyPublish(MdDirectoryStore* S,cstr Root,cstr File,cstr Work,uint64 Revision,char Error[512]){
 char* Path=xrtFormat("%s/%s.zip",Root,File);char Upload[33]={0};bool Ok=Path&&QaStoreFinish(S,1000,"knowledge",Path,Upload,Error);xrtFree(Path);
 xvalue* Input=QaStoreSubmitBody(Upload,true),*Out=NULL;
 if(Work){MdStorePut(Input,"work",Work);SdkPutUInt(Input,"expected_revision",Revision);}
 Ok=Ok&&MdStoreSubmit(S,1000,true,Input,&Out,Error);xrtValueRelease(Input);if(!Ok){xrtValueRelease(Out);return NULL;}
 char Version[33];snprintf(Version,sizeof(Version),"%s",MdoExtensionText(Out,"version_id",32));xrtValueRelease(Out);Out=NULL;
 Input=SdkParse("{\"action\":\"approve\",\"expected_revision\":1,\"reason\":\"Dependency contract\"}");MdStorePut(Input,"version",Version);
 Ok=MdStoreReview(S,9,true,Input,&Out,Error);xrtValueRelease(Input);if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 (void)V;xvalue* R=xrtValueObject();char Error[512]={0};char* Directory=xrtFormat("%s/store",SdkField(P,"root"));MdDirectoryStore S;
 bool Ok=MdStoreInit(&S,Directory,Error);xrtFree(Directory);cstr Root=SdkField(P,"root");
 xvalue* Xrt=Ok?QaDependencyPublish(&S,Root,"xrt",NULL,0,Error):NULL;
 xvalue* Xs=Xrt?QaDependencyPublish(&S,Root,"xs",NULL,0,Error):NULL;
 xvalue* Mdo=Xs?QaDependencyPublish(&S,Root,"mdo",NULL,0,Error):NULL;
 QaStoreFlag(R,"publish_dependency_chain",Xrt&&Xs&&Mdo);
 xvalue* Plan=Mdo?MdStoreInstallPlan(&S,MdoExtensionText(Mdo,"version_id",32),"windows-x86_64",Error):NULL;
 const xvalue* Items=SdkGet(Plan,"items");
 bool Ordered=Plan&&xrtValueCount(Items)==3&&!strcmp(SdkField(xrtValueArrayGet(Items,0),"id"),"knowledge:qa.xrt")&&!strcmp(SdkField(xrtValueArrayGet(Items,1),"id"),"knowledge:qa.xs")&&!strcmp(SdkField(xrtValueArrayGet(Items,2),"id"),"knowledge:qa.mdo");
 QaStoreFlag(R,"topological_plan_deduplicates_shared_dependencies",Ordered);xrtValueRelease(Plan);
 xvalue* Detail=MdStoreResolve(&S,"knowledge:qa.mdo",NULL,Error);const xvalue* Deps=SdkGet(Detail,"dependency_details");
 bool Links=Detail&&xrtValueCount(Deps)==3;size_t Available=0;
 for(size_t I=0;Links&&I<xrtValueCount(Deps);I++){const xvalue* D=xrtValueArrayGet(Deps,I);if(MdStoreBool(D,"available",false)){cstr Url=SdkField(D,"url");Links=Url&&!strncmp(Url,"/mdo/ecosystem?extension=knowledge:",34);Available++;}else Links=MdStoreBool(D,"optional",false)&&!SdkGet(D,"owner")&&!SdkGet(D,"xid");}
 QaStoreFlag(R,"dependency_detail_links_and_private_information_boundaries",Links&&Available==2);xrtValueRelease(Detail);
 Detail=MdStoreResolve(&S,"knowledge:qa.xrt",NULL,Error);QaStoreFlag(R,"reverse_public_dependencies",Detail&&xrtValueCount(SdkGet(Detail,"dependents"))==2);xrtValueRelease(Detail);
 xvalue* Bad=QaDependencyPublish(&S,Root,"missing",NULL,0,Error);QaStoreFlag(R,"missing_required_dependency_rejects_publication",Bad==NULL);xrtValueRelease(Bad);
 Bad=QaDependencyPublish(&S,Root,"cycle",MdoExtensionText(Xrt,"work",32),(uint64)MdStoreInt(Xrt,"work_revision",0),Error);QaStoreFlag(R,"transitive_cycle_rejects_new_version",Bad==NULL);xrtValueRelease(Bad);
 Bad=QaDependencyPublish(&S,Root,"renamed",MdoExtensionText(Xrt,"work",32),(uint64)MdStoreInt(Xrt,"work_revision",0),Error);QaStoreFlag(R,"work_identity_is_immutable",Bad==NULL);xrtValueRelease(Bad);
 char Upload[33]={0};char* Path=xrtFormat("%s/xrt.zip",Root);bool Forgery=QaStoreFinish(&S,2000,"knowledge",Path,Upload,Error);xrtFree(Path);xvalue* Body=QaStoreSubmitBody(Upload,false),*Out=NULL;
 Forgery=Forgery&&!MdStoreSubmit(&S,2000,false,Body,&Out,Error);xrtValueRelease(Out);xrtValueRelease(Body);QaStoreFlag(R,"another_author_cannot_claim_identity",Forgery);
 Detail=MdStoreResolve(&S,"knowledge:qa.xrt","linux-x86_64-musl",Error);QaStoreFlag(R,"failed_updates_preserve_published_identity",Detail&&!strcmp(SdkField(Detail,"version_id"),SdkField(Xrt,"version_id")));xrtValueRelease(Detail);
 Plan=MdStoreInstallPlan(&S,SdkField(Mdo,"version_id"),"unsupported-platform",Error);QaStoreFlag(R,"unknown_platform_is_rejected",Plan==NULL);xrtValueRelease(Plan);
 xrtValueRelease(Xrt);xrtValueRelease(Xs);xrtValueRelease(Mdo);MdStoreUnit(&S);return R;
}
'''

def main():
    p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/extension-dependencies/store';out.mkdir(parents=True,exist_ok=True);fixture=out/('case-'+uuid.uuid4().hex[:10]);fixture.mkdir()
    shutil.copyfile(sdk_root()/'lib/sqlite3.h',fixture/'sqlite3.h')
    cases={'xrt':('knowledge:qa.xrt',[]),'xs':('knowledge:qa.xs',[dict(id='knowledge:qa.xrt')]),'mdo':('knowledge:qa.mdo',[dict(id='knowledge:qa.xs'),dict(id='knowledge:qa.xrt'),dict(id='knowledge:qa.optional-private',optional=True)]),'missing':('knowledge:qa.missing',[dict(id='knowledge:qa.absent')]),'cycle':('knowledge:qa.xrt',[dict(id='knowledge:qa.mdo')]),'renamed':('knowledge:qa.renamed',[])}
    for name,(identity,dependencies) in cases.items():
        path=fixture/(name+'.zip');directory_zip(path,'knowledge',alias=name)
        with zipfile.ZipFile(path) as z:files={i.filename:z.read(i) for i in z.infolist()}
        meta=json.loads(files['extension.json']);meta.update(id=identity,dependencies=dependencies);files['extension.json']=json.dumps(meta)
        with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
            for file,data in files.items():z.writestr(file,data)
    setup='QaSqlite=LoadLibraryW(L"C:/Program Files/Python312/DLLs/sqlite3.dll");if(!QaSqlite){xsDestroyTCC(T);T=NULL;}'
    names=('initialize','bind_int','bind_text','prepare_v2','finalize','step','exec','column_count','column_name','column_type','column_int64','column_text','column_bytes','bind_int64','open','busy_timeout','close','changes')
    setup+=''.join(f'if(T)tcc_add_symbol(T,"sqlite3_{n}",GetProcAddress(QaSqlite,"sqlite3_{n}"));' for n in names)
    try:
        helpers=CONTRACT.split('xvalue* QaProductContract(')[0]
        result=product(a,fixture,platform_store_source(PLATFORM)+'\n'+helpers+TEST,out/'result.json',compiler_setup=setup,compiler_prelude='#include <windows.h>\nstatic HMODULE QaSqlite;',compiler_cleanup='if(QaSqlite)FreeLibrary(QaSqlite);')
        assert result and all(v is True for v in result.values()),result
        print('PASS',len(result),'native publication/identity/dependency contracts')
    finally:
        if fixture.resolve().parent==out.resolve():shutil.rmtree(fixture)
if __name__=='__main__':main()
