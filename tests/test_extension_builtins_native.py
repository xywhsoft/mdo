"""Actual product bundled directory ZIP installation and durable opt-out."""
import argparse,json,shutil,uuid
from pathlib import Path
from native_product_runner import product
ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
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
 Rows=MdoExtensionsList("skills",Error);xrtValueObjectSetNew(R,XRT_STR_LITERAL("skill_directories"),xrtValueBool(Rows&&xrtValueCount(Rows)==2));xrtValueRelease(Rows);
 MdoExtPut(R,"error",Error);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args();out=ROOT/'.build/builtin-directory-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir()
 try:
  result=product(a,fixture,CONTRACT,out/'result.json');assert all(result.get(k) for k in ('installed','builtin_verified','uninstall_disable_survive_restart','skill_directories')),result;print(json.dumps(result))
 finally:
  if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)
if __name__=='__main__':main()
