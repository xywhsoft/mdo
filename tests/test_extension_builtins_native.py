"""Actual product bundled directory ZIP installation and durable opt-out."""
import argparse,json,shutil,uuid
from pathlib import Path
from native_product_runner import product
ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
static bool QaBuiltinLegacy(cstr Id,bool Approve,char E[512]){
 char* Path=xrtFormat("commands/%s/extension.json",Id),*Key=MdoExtKey("commands",Id);xvalue* Meta=MdoExtReadJson(Path,65536,false),*State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);xvalue* Record=xrtValueObjectGet(State,xrtStrView(Key));char Hash[65];
 if(Meta)xrtValueObjectRemove(Meta,XRT_STR_LITERAL("id"));if(Record)xrtValueObjectRemove(Record,XRT_STR_LITERAL("logical_id"));
 bool Ok=Meta&&Record&&MdoExtSaveJson(Path,Meta,65536)&&MdoExtensionsFingerprint("commands",Id,Hash,NULL,E)&&(!Approve||MdoExtPut(Record,"directory_digest",Hash))&&MdoExtSaveJson(MDO_EXT_STATE,State,MDO_EXT_STATE_LIMIT);
 xrtFree(Path);xrtFree(Key);xrtValueRelease(Meta);xrtValueRelease(State);return Ok;
}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 char Error[512]={0};xvalue* R=xrtValueObject();bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);
 bool Ok=Home&&MdoExtensionsInit()&&MdoExtensionsRecover(Error)&&MdoExtensionsSeedBuiltins(Error);
 xvalue* Rows=Ok?MdoExtensionsList("commands",Error):NULL;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("installed"),xrtValueBool(Ok&&xrtValueCount(Rows)==11));xrtValueRelease(Rows);
 xvalue* Current=Ok?MdoExtensionsRead("commands","new",Error):NULL;
 Ok=Ok&&Current&&!strcmp(MdoExtensionText(Current,"source",16),"builtin")&&MdoExtensionsVerify("commands","new",NULL,Error);xrtValueRelease(Current);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("builtin_verified"),xrtValueBool(Ok));
 xvalue* Result=NULL;Ok=Ok&&MdoExtensionsSetEnabled("commands","help",false,Error)&&MdoExtensionsRemove("commands","new",&Result,Error);xrtValueRelease(Result);
 MdoExtensionsUnit();Ok=Ok&&MdoExtensionsInit()&&MdoExtensionsRecover(Error)&&MdoExtensionsSeedBuiltins(Error);
 bool Exists=true;xfileinfo Info;Ok=Ok&&MdoHomeExternalStat("commands/new",&Exists,&Info)&&!Exists;
 Current=Ok?MdoExtensionsRead("commands","help",Error):NULL;bool Enabled=true;
 xrtValueGetBool(xrtValueObjectGet(Current,XRT_STR_LITERAL("enabled")),&Enabled);Ok=Ok&&Current&&!Enabled;xrtValueRelease(Current);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("uninstall_disable_survive_restart"),xrtValueBool(Ok));

 bool Legacy=Ok&&QaBuiltinLegacy("help",true,Error)&&MdoExtensionsSeedBuiltins(Error);Current=MdoExtensionsRead("commands","help",Error);xrtValueObjectSetNew(R,XRT_STR_LITERAL("normal_seed_migrates_unchanged_identity"),xrtValueBool(Legacy&&Current&&MdoExtensionRefValid(MdoExtensionText(Current,"id",MDO_EXTENSION_REF_BYTES))));xrtValueRelease(Current);
 bool Migrated=Legacy&&MdoExtensionsMigrateBuiltins(Error);Current=MdoExtensionsRead("commands","help",Error);bool HelpDisabled=Current&&!MdoGraphBool(Current,"enabled",true);xrtValueRelease(Current);Exists=true;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("explicit_builtin_migration_preserves_disabled_and_removed"),xrtValueBool(Migrated&&HelpDisabled&&MdoHomeExternalStat("commands/new",&Exists,&Info)&&!Exists));
 bool Edited=QaBuiltinLegacy("theme",false,Error);char EditedBefore[65]={0},EditedAfter[65]={0};MdoExtensionsFingerprint("commands","theme",EditedBefore,NULL,Error);
 bool Skipped=Edited&&MdoExtensionsSeedBuiltins(Error)&&MdoExtensionsFingerprint("commands","theme",EditedAfter,NULL,Error)&&!strcmp(EditedBefore,EditedAfter);Current=MdoExtensionsRead("commands","theme",Error);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("edited_builtin_preserves_startup_and_warns"),xrtValueBool(Skipped&&MdoExtensionsBuiltinUpdatePending("commands","theme",Current)));xrtValueRelease(Current);
 bool Refused=Skipped&&!MdoExtensionsMigrateBuiltins(Error)&&MdoExtensionsFingerprint("commands","theme",EditedAfter,NULL,Error)&&!strcmp(EditedBefore,EditedAfter);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("edited_legacy_builtin_is_preserved"),xrtValueBool(Refused));
 Rows=MdoExtensionsList("skills",Error);xrtValueObjectSetNew(R,XRT_STR_LITERAL("skill_directories"),xrtValueBool(Rows&&xrtValueCount(Rows)==2));xrtValueRelease(Rows);
 MdoExtPut(R,"error",Error);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args();out=ROOT/'.build/builtin-directory-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir()
 try:
  result=product(a,fixture,CONTRACT,out/'result.json');assert all(result.get(k) for k in ('installed','builtin_verified','uninstall_disable_survive_restart','skill_directories','normal_seed_migrates_unchanged_identity','explicit_builtin_migration_preserves_disabled_and_removed','edited_legacy_builtin_is_preserved','edited_builtin_preserves_startup_and_warns')),result;print(json.dumps(result))
 finally:
  if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)
if __name__=='__main__':main()
