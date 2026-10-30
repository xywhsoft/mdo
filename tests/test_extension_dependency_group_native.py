"""Real XS/TCC contracts for dependency plans, group commits and recovery."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import uuid
import zipfile
from extension_zip_fixture import directory_zip
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static void QaGroupFlag(xvalue* R,cstr K,bool B){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(B));}
static xvalue* QaGroupPlan(cstr Op,cstr Ref,bool Cascade,const xvalue* Packages,char Error[512]){
 xvalue* Q=xrtValueObject(),*P=NULL;MdoExtPut(Q,"action",Op);MdoExtPut(Q,"ref",Ref);xrtValueObjectSetNew(Q,XRT_STR_LITERAL("cascade"),xrtValueBool(Cascade));MdoExtensionsPlan(Q,Packages,&P,Error);xrtValueRelease(Q);return P;
}
static bool QaGroupWanted(cstr Ref,bool Wanted){char E[512];xvalue* R=MdoExtensionsResolve(Ref,E);bool B=R&&MdoGraphBool(R,"desired_enabled",false)==Wanted;xrtValueRelease(R);return B;}
static bool QaGroupCancel(const xvalue* Items,bool Retiring,void* C,char E[512]){(void)Items;(void)E;if(Retiring)xrtCancelRequest(C);return true;}
static bool QaGroupPartial(const xvalue* Items,bool Retiring,void* Data,char E[512]){
 if(!Retiring)return true;
 (void)Data;(void)E;xdir D=MdoHomeOpenDirectory(MDO_EXT_GROUPS,XDIR_STAT);xdirentry Entry;char* Base=NULL;
 while(D&&xrtDirNext(D,&Entry)==XDIR_NEXT_ITEM)if(Entry.Info.Type==XFILE_TYPE_DIRECTORY){Base=xrtFormat(MDO_EXT_GROUPS "/%.*s",(int)Entry.Name.Size,Entry.Name.Data);break;}if(D)xrtDirClose(D);
 for(size_t I=0;Base&&I<xrtValueCount(Items);I++){
  const xvalue* Item=xrtValueArrayGet(Items,I);cstr Op=MdoExtensionText(Item,"operation",16);if(strcmp(Op,"install"))continue;
  char *Root=MdoExtensionDirectoryPath(MdoExtensionText(Item,"kind",32),MdoExtensionText(Item,"directory_id",96)),*Prior=xrtFormat("%s/%u/prior",Base,(unsigned)I),*Incoming=xrtFormat("%s/%u/incoming",Base,(unsigned)I);
  bool Ok=Root&&Prior&&Incoming&&(!MdoGraphBool(Item,"old_exists",false)||MdoHomeRenameNoReplace(Root,Prior))&&MdoHomeRenameNoReplace(Incoming,Root);xrtFree(Root);xrtFree(Prior);xrtFree(Incoming);if(Ok)break;
 }xrtFree(Base);return false;
}
xvalue* QaProductContract(const xvalue* Input,xvfs V){
 char Error[512]={0};xvalue* R=xrtValueObject(),*Out=NULL;bool Home=QaProductHome(SdkField(Input,"root"),SdkField(Input,"app"),V),Ok=Home&&MdoExtensionsInit();
 xvalue *All=MdoExtReadJson("packages.json",1048576,false),*Downloads=MdoExtReadJson("downloads.json",1048576,false);const xvalue* Initial=xrtValueObjectGet(All,XRT_STR_LITERAL("initial"));
 xvalue* Plan=Ok?QaGroupPlan("install","knowledge:qa.mdo",false,Initial,Error):NULL;
 QaGroupFlag(R,"complete_plan",Plan&&MdoGraphBool(Plan,"ready",false)&&xrtValueCount(xrtValueObjectGet(Plan,XRT_STR_LITERAL("items")))==3);
 bool Applied=Plan&&MdoExtensionsApplyGroup(Plan,Downloads,true,NULL,NULL,NULL,&Out,Error);QaGroupFlag(R,"three_directories_commit",Applied&&MdoExtensionsPreflight("knowledge:qa.mdo",Error)&&MdoExtensionsPreflight("knowledge:qa.xs",Error)&&MdoExtensionsPreflight("knowledge:qa.xrt",Error));xrtValueRelease(Out);Out=NULL;
 xvalue* Loaded=Plan?MdoExtensionsPlanRead(MdoExtensionText(Plan,"id",32),MdoExtensionText(Plan,"revision",64),Error):NULL;QaGroupFlag(R,"plan_round_trip",Loaded!=NULL);xrtValueRelease(Loaded);
 xvalue* RuntimeReceipt=Plan?MdoExtensionsPlanReceipt(MdoExtensionText(Plan,"id",32),MdoExtensionText(Plan,"revision",64),Error):NULL;bool RuntimeSaved=RuntimeReceipt&&xrtValueObjectSetNew(RuntimeReceipt,XRT_STR_LITERAL("runtime_ready"),xrtValueBool(false))&&MdoExtPut(RuntimeReceipt,"runtime_error","Restart to publish the new frontend")&&MdoExtensionsPlanReceiptRefresh(MdoExtensionText(Plan,"id",32),MdoExtensionText(Plan,"revision",64),RuntimeReceipt,Error);xrtValueRelease(RuntimeReceipt);
 bool Retry=Plan&&MdoExtensionsApplyGroup(Plan,NULL,false,NULL,NULL,NULL,&Out,Error);QaGroupFlag(R,"lost_reply_is_idempotent",Retry);QaGroupFlag(R,"final_runtime_status_survives_lost_reply",RuntimeSaved&&Retry&&!MdoGraphBool(Out,"runtime_ready",true));xrtValueRelease(Out);Out=NULL;xrtValueRelease(Plan);
 MdoExtensionLease* Lease=MdoExtensionsAcquire("knowledge","knowledge:qa.mdo",Error);size_t Pins=0;for(MdoExtensionLease* L=Lease;L;L=L->GroupNext)Pins++;QaGroupFlag(R,"lease_pins_transitive_closure",Pins==3);
 const xvalue* Update=xrtValueObjectGet(All,XRT_STR_LITERAL("update"));Plan=QaGroupPlan("install","knowledge:qa.xrt",false,Update,Error);
 QaGroupFlag(R,"update_lists_reverse_users",Plan&&xrtValueCount(xrtValueObjectGet(Plan,XRT_STR_LITERAL("items")))==3);
 char Original[65]={0},Current[65]={0};MdoExtensionsFingerprint("knowledge","knowledge:qa.xrt",Original,NULL,Error);
 bool Busy=Plan&&!MdoExtensionsApplyGroup(Plan,Downloads,true,NULL,NULL,NULL,&Out,Error);SdkPut(R,"busy_error",Error);QaGroupFlag(R,"busy_group_restores_files_and_intent",Busy&&MdoExtensionsFingerprint("knowledge","knowledge:qa.xrt",Current,NULL,Error)&&!strcmp(Current,Original)&&QaGroupWanted("knowledge:qa.mdo",true)&&QaGroupWanted("knowledge:qa.xrt",true));xrtValueRelease(Out);Out=NULL;MdoExtensionsRelease(Lease);
 xcancel* Cancel=xrtCancelCreate();bool Cancelled=Plan&&!MdoExtensionsApplyGroup(Plan,Downloads,true,Cancel,QaGroupCancel,Cancel,&Out,Error);QaGroupFlag(R,"cancel_after_suspend_rolls_back_whole_state",Cancelled&&QaGroupWanted("knowledge:qa.mdo",true)&&MdoExtensionsFingerprint("knowledge","knowledge:qa.xrt",Current,NULL,Error)&&!strcmp(Current,Original));xrtCancelDestroy(Cancel);xrtValueRelease(Out);Out=NULL;
 bool Partial=Plan&&!MdoExtensionsApplyGroup(Plan,Downloads,true,NULL,QaGroupPartial,NULL,&Out,Error);QaGroupFlag(R,"partial_publication_recovers_all_directories",Partial&&MdoExtensionsFingerprint("knowledge","knowledge:qa.xrt",Current,NULL,Error)&&!strcmp(Current,Original)&&MdoExtensionsPreflight("knowledge:qa.mdo",Error));xrtValueRelease(Out);Out=NULL;
 bool Updated=Plan&&MdoExtensionsApplyGroup(Plan,Downloads,true,NULL,NULL,NULL,&Out,Error);QaGroupFlag(R,"update_restores_reverse_users",Updated&&MdoExtensionsFingerprint("knowledge","knowledge:qa.xrt",Current,NULL,Error)&&strcmp(Current,Original)&&MdoExtensionsPreflight("knowledge:qa.mdo",Error));xrtValueRelease(Out);Out=NULL;xrtValueRelease(Plan);

 /* Simulate a genuinely old approved archive: no inferred identity, same work. */
 char* LegacyDir=MdoExtensionsDirectoryId("knowledge","knowledge:qa.mdo",Error),*LegacyPath=LegacyDir?xrtFormat("knowledge/%s/extension.json",LegacyDir):NULL,*LegacyKey=LegacyDir?MdoExtKey("knowledge",LegacyDir):NULL;
 xvalue* LegacyMeta=LegacyPath?MdoExtReadJson(LegacyPath,65536,false):NULL,*LegacyState=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);xvalue* LegacyRecord=LegacyKey?xrtValueObjectGet(LegacyState,xrtStrView(LegacyKey)):NULL;
 if(LegacyMeta)xrtValueObjectRemove(LegacyMeta,XRT_STR_LITERAL("id"));if(LegacyRecord)xrtValueObjectRemove(LegacyRecord,XRT_STR_LITERAL("logical_id"));
 bool LegacyOk=LegacyMeta&&LegacyRecord&&MdoExtSaveJson(LegacyPath,LegacyMeta,65536)&&MdoExtensionsFingerprint("knowledge",LegacyDir,Current,NULL,Error)&&MdoExtPut(LegacyRecord,"directory_digest",Current)&&MdoExtSaveJson(MDO_EXT_STATE,LegacyState,MDO_EXT_STATE_LIMIT);
 xvalue* LegacyPackages=xrtValueArray();if(LegacyPackages)xrtValueArrayAppend(LegacyPackages,xrtValueArrayGet(Initial,2));Plan=LegacyOk?QaGroupPlan("install","knowledge:qa.mdo",false,LegacyPackages,Error):NULL;
 xvalue* OriginalDownloads=MdoExtReadJson("downloads.json",1048576,false);bool Migrated=Plan&&MdoGraphBool(Plan,"ready",false)&&MdoExtensionsApplyGroup(Plan,OriginalDownloads,true,NULL,NULL,NULL,&Out,Error);
 QaGroupFlag(R,"verified_legacy_store_upgrade_binds_same_work",Migrated&&MdoExtensionsPreflight("knowledge:qa.mdo",Error));xrtValueRelease(Plan);xrtValueRelease(Out);Out=NULL;
 /* A manually edited legacy directory must not be silently replaced. */
 xvalue* FreshState=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);xvalue* FreshRecord=LegacyKey?xrtValueObjectGet(FreshState,xrtStrView(LegacyKey)):NULL;if(FreshRecord)xrtValueObjectRemove(FreshRecord,XRT_STR_LITERAL("logical_id"));
 bool Edited=FreshRecord&&MdoExtSaveJson(LegacyPath,LegacyMeta,65536)&&MdoExtSaveJson(MDO_EXT_STATE,FreshState,MDO_EXT_STATE_LIMIT);Plan=Edited?QaGroupPlan("install","knowledge:qa.mdo",false,LegacyPackages,Error):NULL;QaGroupFlag(R,"edited_legacy_archive_requires_explicit_repair",Edited&&!Plan);xrtValueRelease(Plan);
 /* Restore only this test's original identity and approved fingerprint. */
 MdoExtPut(LegacyMeta,"id","knowledge:qa.mdo");MdoExtPut(FreshRecord,"logical_id","knowledge:qa.mdo");MdoExtSaveJson(LegacyPath,LegacyMeta,65536);MdoExtensionsFingerprint("knowledge",LegacyDir,Current,NULL,Error);MdoExtPut(FreshRecord,"directory_digest",Current);MdoExtSaveJson(MDO_EXT_STATE,FreshState,MDO_EXT_STATE_LIMIT);
 xrtValueRelease(FreshState);xrtValueRelease(LegacyState);xrtValueRelease(LegacyMeta);xrtValueRelease(LegacyPackages);xrtValueRelease(OriginalDownloads);xrtFree(LegacyDir);xrtFree(LegacyPath);xrtFree(LegacyKey);
 Plan=QaGroupPlan("disable","knowledge:qa.mdo",false,NULL,Error);cstr PlanId=MdoExtensionText(Plan,"id",32),PlanRevision=MdoExtensionText(Plan,"revision",64);QaGroupFlag(R,"cancelled_review_releases_plan",Plan&&MdoExtensionsPlanDiscard(PlanId,PlanRevision,Error)&&!MdoExtensionsPlanRead(PlanId,PlanRevision,Error));xrtValueRelease(Plan);
 Plan=QaGroupPlan("disable","knowledge:qa.mdo",false,NULL,Error);xrtValueObjectSetNew(Plan,XRT_STR_LITERAL("created"),xrtValueUInt(1));xrtValueObjectRemove(Plan,XRT_STR_LITERAL("revision"));char ExpiredRevision[65];MdoExtValueHash(Plan,ExpiredRevision);MdoExtPut(Plan,"revision",ExpiredRevision);char* ExpiredPath=xrtFormat(MDO_EXT_PLANS "/%s.json",MdoExtensionText(Plan,"id",32));MdoExtSaveJson(ExpiredPath,Plan,MDO_EXT_PLAN_BYTES);
 QaGroupFlag(R,"expired_review_cannot_be_applied",!MdoExtensionsPlanRead(MdoExtensionText(Plan,"id",32),ExpiredRevision,Error)&&!MdoExtensionsApplyGroup(Plan,NULL,false,NULL,NULL,NULL,&Out,Error));xrtValueRelease(Plan);xrtValueRelease(Out);Out=NULL;MdoExtPlansPrune(Error);bool ExpiredExists=true;xfileinfo ExpiredInfo;QaGroupFlag(R,"expired_review_is_reclaimed",MdoExtStat(ExpiredPath,&ExpiredExists,&ExpiredInfo)&&!ExpiredExists);xrtFree(ExpiredPath);
 QaGroupFlag(R,"direct_disable_guards_reverse_dependencies",!MdoExtensionsSetEnabled("knowledge","knowledge:qa.xrt",false,Error));
 Plan=QaGroupPlan("disable","knowledge:qa.xrt",false,NULL,Error);QaGroupFlag(R,"noncascade_plan_rejects_enabled_users",Plan==NULL);xrtValueRelease(Plan);
 Plan=QaGroupPlan("disable","knowledge:qa.xrt",true,NULL,Error);bool Disabled=Plan&&MdoExtensionsApplyGroup(Plan,NULL,false,NULL,NULL,NULL,&Out,Error);QaGroupFlag(R,"cascade_disable_is_atomic",Disabled&&QaGroupWanted("knowledge:qa.xrt",false)&&QaGroupWanted("knowledge:qa.xs",false)&&QaGroupWanted("knowledge:qa.mdo",false));xrtValueRelease(Plan);xrtValueRelease(Out);Out=NULL;
 Plan=QaGroupPlan("enable","knowledge:qa.mdo",false,NULL,Error);bool Enabled=Plan&&MdoGraphBool(Plan,"ready",false)&&MdoExtensionsApplyGroup(Plan,NULL,false,NULL,NULL,NULL,&Out,Error);QaGroupFlag(R,"enable_includes_required_closure",Enabled&&MdoExtensionsPreflight("knowledge:qa.mdo",Error));xrtValueRelease(Plan);xrtValueRelease(Out);Out=NULL;
 Plan=QaGroupPlan("disable","knowledge:qa.mdo",false,NULL,Error);bool Changed=MdoExtensionsSetEnabled("knowledge","knowledge:qa.mdo",false,Error);QaGroupFlag(R,"stale_plan_cannot_overwrite_new_preferences",Changed&&Plan&&!MdoExtensionsApplyGroup(Plan,NULL,false,NULL,NULL,NULL,&Out,Error)&&QaGroupWanted("knowledge:qa.mdo",false));xrtValueRelease(Plan);xrtValueRelease(Out);Out=NULL;
 xvalue* DisabledUpdate=xrtValueArray();xrtValueArrayAppend(DisabledUpdate,xrtValueArrayGet(Initial,2));Plan=QaGroupPlan("install","knowledge:qa.mdo",false,DisabledUpdate,Error);QaGroupFlag(R,"disabled_store_update_preserves_activation_intent",Plan&&MdoGraphBool(Plan,"ready",false)&&MdoExtensionsApplyGroup(Plan,NULL,false,NULL,NULL,NULL,&Out,Error)&&QaGroupWanted("knowledge:qa.mdo",false)&&QaGroupWanted("knowledge:qa.xs",true));xrtValueRelease(Plan);xrtValueRelease(Out);Out=NULL;xrtValueRelease(DisabledUpdate);
 Plan=QaGroupPlan("remove","knowledge:qa.xrt",true,NULL,Error);bool Removed=Plan&&MdoExtensionsApplyGroup(Plan,NULL,false,NULL,NULL,NULL,&Out,Error);xvalue* Missing=MdoExtensionsResolve("knowledge:qa.xrt",Error);QaGroupFlag(R,"cascade_uninstall_keeps_users_disabled",Removed&&!Missing&&QaGroupWanted("knowledge:qa.xs",false)&&QaGroupWanted("knowledge:qa.mdo",false));xrtValueRelease(Missing);xrtValueRelease(Plan);xrtValueRelease(Out);Out=NULL;

 /* A process can stop after durable commit but before backup cleanup. */
 char* Remnant=xrtXidMakeString(),*RemnantBase=xrtFormat(MDO_EXT_GROUPS "/%s",Remnant),*RemnantStage=xrtFormat("%s/stage.json",RemnantBase),*RemnantFile=xrtFormat("%s/0/incoming/leftover.txt",RemnantBase);xvalue* StageIntent=SdkParse("{\"protocol\":1,\"count\":1}");
 bool Staged=Remnant&&MdoExtSaveJson(RemnantStage,StageIntent,4096)&&MdoHomeAtomicWrite(RemnantFile,"owned staged data",17,false);QaGroupFlag(R,"terminal_cleanup_recovers_json_integer_counts",Staged&&MdoExtGroupRecover(Remnant,Error));xrtValueRelease(StageIntent);xrtFree(Remnant);xrtFree(RemnantBase);xrtFree(RemnantStage);xrtFree(RemnantFile);
 /* Validate the second directory before touching the first proved candidate. */
 char* Tamper=xrtXidMakeString(),*TamperBase=xrtFormat(MDO_EXT_GROUPS "/%s",Tamper),*TamperJournal=xrtFormat("%s/journal.json",TamperBase),*TamperBefore=xrtFormat("%s/before.json",TamperBase),*TamperAfter=xrtFormat("%s/after.json",TamperBase);xvalue* ProofState=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true),*ProofItems=xrtValueArray(),*ProofJournal=xrtValueObject();char StateProof[65];bool Proof=ProofState&&ProofItems&&ProofJournal&&MdoExtValueHash(ProofState,StateProof)&&MdoExtSaveJson(TamperBefore,ProofState,MDO_EXT_STATE_LIMIT)&&MdoExtSaveJson(TamperAfter,ProofState,MDO_EXT_STATE_LIMIT);
 for(size_t I=0;Proof&&I<2;I++){cstr Directory=I?"qa-proof-b":"qa-proof-a";char* Path=xrtFormat("knowledge/%s/file.txt",Directory),*RootPath=MdoExtensionDirectoryPath("knowledge",Directory);xvalue* Item=xrtValueObject();char Digest[65];Proof=Path&&RootPath&&Item&&MdoHomeAtomicWrite(Path,"preserve foreign edit",21,false)&&MdoExtensionsFingerprint("knowledge",Directory,Digest,NULL,Error)&&MdoExtPut(Item,"kind","knowledge")&&MdoExtPut(Item,"directory_id",Directory)&&MdoExtPut(Item,"operation","install")&&MdoExtPut(Item,"new_digest",I?"0000000000000000000000000000000000000000000000000000000000000000":Digest)&&xrtValueObjectSetNew(Item,XRT_STR_LITERAL("old_exists"),xrtValueBool(false))&&xrtValueArrayAppend(ProofItems,Item);xrtFree(Path);xrtFree(RootPath);xrtValueRelease(Item);}
 Proof=Proof&&MdoExtPut(ProofJournal,"phase","prepared")&&MdoExtPut(ProofJournal,"before_hash",StateProof)&&MdoExtPut(ProofJournal,"after_hash",StateProof)&&MdoExtPut(ProofJournal,"suspended_hash",StateProof)&&xrtValueObjectSet(ProofJournal,XRT_STR_LITERAL("items"),ProofItems)&&MdoExtSaveJson(TamperJournal,ProofJournal,MDO_EXT_PLAN_BYTES);bool PreservedA=false,PreservedB=false;xfileinfo ProofInfo;
 QaGroupFlag(R,"tampered_group_preserves_every_directory_before_rollback",Proof&&!MdoExtGroupRecover(Tamper,Error)&&MdoHomeExternalStat("knowledge/qa-proof-a/file.txt",&PreservedA,&ProofInfo)&&PreservedA&&MdoHomeExternalStat("knowledge/qa-proof-b/file.txt",&PreservedB,&ProofInfo)&&PreservedB);
 MdoExtRemoveTree("knowledge/qa-proof-a");MdoExtRemoveTree("knowledge/qa-proof-b");MdoExtRemoveTree(TamperBase);xrtValueRelease(ProofItems);xrtValueRelease(ProofJournal);xrtValueRelease(ProofState);xrtFree(Tamper);xrtFree(TamperBase);xrtFree(TamperJournal);xrtFree(TamperBefore);xrtFree(TamperAfter);
 QaGroupFlag(R,"startup_recovery_clean",MdoExtensionsRecoverStartup(Error)&&xrtValueCount(MdoExtensionsRecoveryWarnings())==0);SdkPut(R,"error",Error);xrtValueRelease(All);xrtValueRelease(Downloads);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''


def main():
    p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/extension-dependencies/groups';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:10]);fixture.mkdir()
    packages={};downloads=[]
    for name,version,deps in [('xrt',1,[]),('xs',1,['knowledge:qa.xrt']),('mdo',1,['knowledge:qa.xs','knowledge:qa.xrt']),('xrt',2,[])]:
        directory=fixture/'data/incoming';directory.mkdir(parents=True,exist_ok=True);path=directory/f'{name}{version}.zip';directory_zip(path,'knowledge',alias=name)
        with zipfile.ZipFile(path) as z:files={info.filename:z.read(info) for info in z.infolist()}
        meta=json.loads(files['extension.json']);meta.update(id=f'knowledge:qa.{name}',version=str(version),dependencies=[dict(id=d) for d in deps]);files['extension.json']=json.dumps(meta);files['index.md']=f'# {name} version {version}\n'
        with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
            for file,content in files.items():z.writestr(file,content)
        identity=f'knowledge:qa.{name}';xid=hashlib.sha256(name.encode()).hexdigest()[:32];sha=hashlib.sha256(path.read_bytes()).hexdigest();entry=dict(id=identity,kind='knowledge',xid=xid,version_id=hashlib.sha256(f'{name}{version}'.encode()).hexdigest()[:32],sha256=sha,bytes=path.stat().st_size,manifest=meta)
        packages[name+str(version)]=entry;downloads.append(dict(id=identity,archive=f'data/incoming/{name}{version}.zip',credits={**entry,'source':'store'}))
    # The update and initial groups use different versions of the same logical ID.
    contract=CONTRACT.replace('Plan,Downloads,true', 'Plan,Downloads,true',1)
    (fixture/'packages.json').write_text(json.dumps(dict(initial=[packages[k] for k in ('xrt1','xs1','mdo1')],update=[packages['xrt2']])),encoding='utf-8')
    (fixture/'downloads.json').write_text(json.dumps(downloads[:3]),encoding='utf-8')
    # Switch the download provider after initial installation, preserving its identity.
    (fixture/'update-downloads.json').write_text(json.dumps([downloads[3]]),encoding='utf-8')
    contract=contract.replace('MdoExtensionLease* Lease=', 'xrtValueRelease(Downloads);Downloads=MdoExtReadJson("update-downloads.json",1048576,false);\n MdoExtensionLease* Lease=')
    passed=False
    try:
        result=product(a,fixture,contract,out/'result.json')
        assert result and 'compile_error' not in result,result
        for name,value in result.items():
            if not name.endswith('error'):assert value is True,(name,result)
        passed=True
        print('PASS',len(result)-2,'native group transaction contracts')
    finally:
        if passed and fixture.resolve().parent==out.resolve():shutil.rmtree(fixture)

if __name__=='__main__':main()
