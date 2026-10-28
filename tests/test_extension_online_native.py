"""Read live first-party HTTPS, download an ordinary ZIP and install/remove it.
No login, production writes, external program execution or model calls.
"""
import argparse,json,shutil,uuid
from pathlib import Path
from native_product_runner import product
ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
static void QaFlag(xvalue* R,cstr K,bool B){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(B));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 char E[512]={0};xvalue* R=xrtValueObject(),*Out=NULL,*Input=SdkParse("{\"action\":\"catalog\",\"kind\":\"external-tools\",\"platform\":\"windows-x86_64\",\"q\":\"SQLite\",\"limit\":10}");
 bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V),Ok=Home&&MdoExtensionsInit()&&MdoExtensionTasksInit()&&MdoCliInit(NULL);
 Ok=Ok&&MdoExtensionServiceRead(Input,NULL,&Out,E);const xvalue* Row=xrtValueArrayGet(SdkGet(Out,"items"),0);char Work[33]={0},Version[33]={0};
 if(Row&&SdkField(Row,"xid")&&SdkField(Row,"version_id")){snprintf(Work,sizeof(Work),"%s",SdkField(Row,"xid"));snprintf(Version,sizeof(Version),"%s",SdkField(Row,"version_id"));}else Ok=false;
 QaFlag(R,"real_https_catalog",Ok);xrtValueRelease(Input);xrtValueRelease(Out);Out=NULL;
 char Task[33]={0};Ok=Ok&&MdoExtensionServiceDownload("external-tools",Version,NULL,&Out,E);if(Ok)snprintf(Task,sizeof(Task),"%s",SdkField(Out,"id"));QaFlag(R,"raw_zip_sha_crc_verified",Ok);xrtValueRelease(Out);Out=NULL;
 Input=SdkParse("{\"trust_code\":true,\"enabled\":false}");Ok=Ok&&MdoExtensionServiceInstall(Task,Input,NULL,&Out,E);QaFlag(R,"installed_at_platform_xid",Ok&&SdkField(Out,"id")&&!strcmp(SdkField(Out,"id"),Work));xrtValueRelease(Input);xrtValueRelease(Out);Out=NULL;
 xvalue* Item=Ok?MdoExtensionsRead("external-tools",Work,E):NULL;QaFlag(R,"published_description_and_immutable_source",Item&&!strcmp(SdkField(Item,"source"),"store")&&SdkField(Item,"description")&&strstr(SdkField(Item,"description"),"SQLite"));xrtValueRelease(Item);
 QaFlag(R,"complete_uninstall",Ok&&MdoExtensionsRemove("external-tools",Work,&Out,E));xrtValueRelease(Out);Out=NULL;
 MdoCliUnit();MdoExtensionTasksUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();SdkPut(R,"error",E);return R;
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument("--url",required=True);p.add_argument("--home",type=Path,required=True);p.add_argument("--project",default="default");a=p.parse_args()
 out=ROOT/".build/extension-online-native";out.mkdir(parents=True,exist_ok=True);fixture=out/("home-"+uuid.uuid4().hex[:12]);fixture.mkdir()
 try:
  r=product(a,fixture,CONTRACT,out/"result.json");assert all(r.get(k) for k in ("real_https_catalog","raw_zip_sha_crc_verified","installed_at_platform_xid","published_description_and_immutable_source","complete_uninstall")),r;print(json.dumps(r))
 finally:
  assert fixture.resolve().parent==out.resolve() and fixture.name.startswith("home-");shutil.rmtree(fixture)
if __name__=="__main__":main()
