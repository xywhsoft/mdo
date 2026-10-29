"""Windows directory sharing: transient holders, permanent holders and collisions."""
import argparse
import json
import os
from pathlib import Path
import shutil
import uuid
from native_product_runner import product
ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
__declspec(dllimport) void* __stdcall CreateFileA(cstr,unsigned long,unsigned long,void*,unsigned long,unsigned long,void*);
__declspec(dllimport) int __stdcall CloseHandle(void*);
static int32 QaReleaseDirectory(void* Data){xrtSleep(80);CloseHandle(Data);return 0;}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);
 bool Ok=Home&&MdoHomeAtomicWrite("mcp/locked/marker","keep",4,false);char* Native=MdoHomeExternalPath("mcp/locked");
 void* Invalid=(void*)(intptr_t)-1;void* H=Native?CreateFileA(Native,0x80000000ul,3,NULL,3,0x02000000ul,NULL):Invalid;
 bool Held=Ok&&H!=Invalid&&!xsRootRenameNoReplace(g_MdoHome.Root,"mcp/locked","mcp/moved");
 const xerror* Cause=xrtGetError();SdkPutUInt(R,"sharing_system_code",Cause?(uint64)xrtErrorSystemCode(Cause):0);xrtClearError();
 xthread* T=Held?xrtThreadCreate(QaReleaseDirectory,H,0):NULL;
 bool Renamed=T&&MdoHomeRenameNoReplace("mcp/locked","mcp/moved");if(T){xrtThreadWait(T);xrtThreadDestroy(T);}else if(H!=Invalid)CloseHandle(H);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("transient_holder_retired"),xrtValueBool(Held&&Renamed));
 if(!Renamed)(void)MdoHomeRenameNoReplace("mcp/locked","mcp/moved");
 Ok=MdoHomeAtomicWrite("mcp/destination/marker","original",8,false);
 bool Collision=Ok&&!MdoHomeRenameNoReplace("mcp/moved","mcp/destination");
 char* Text=MdoExtensionRead("mcp/destination/marker",true,32,NULL);Collision=Collision&&Text&&!strcmp(Text,"original");xrtFree(Text);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("collision_never_overwrites"),xrtValueBool(Collision));
 xrtFree(Native);Native=MdoHomeExternalPath("mcp/moved");H=Native?CreateFileA(Native,0x80000000ul,3,NULL,3,0x02000000ul,NULL):Invalid;
 uint64 Start=MdoMonotonicUs();bool Refused=H!=Invalid&&!MdoHomeRenameNoReplace("mcp/moved","mcp/still-locked");uint64 Elapsed=MdoMonotonicUs()-Start;
 if(H!=Invalid)CloseHandle(H);xrtFree(Native);SdkPutUInt(R,"permanent_wait_us",Elapsed);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("permanent_holder_is_bounded"),xrtValueBool(Refused&&Elapsed<1000000));
 char Error[512]={0};xvalue* Meta=SdkParse("{\"kind\":\"mcp\",\"name\":\"Held directory\",\"alias\":\"held\",\"entry\":\"mcp.json\",\"platforms\":[\"any\"]}"),*Out=NULL;
 bool Created=MdoExtensionsInit()&&MdoExtensionsCreate("mcp","held-install",Meta,"{}",2,true,&Out,Error);xrtValueRelease(Meta);xrtValueRelease(Out);Out=NULL;
 Native=MdoHomeExternalPath("mcp/held-install");H=Native?CreateFileA(Native,0x80000000ul,3,NULL,3,0x02000000ul,NULL):Invalid;
 bool Failed=Created&&H!=Invalid&&!MdoExtensionsRemove("mcp","held-install",&Out,Error);xrtValueRelease(Out);
 if(H!=Invalid)CloseHandle(H);xrtFree(Native);
 bool Exists=false;xfileinfo Info;Failed=Failed&&strstr(Error,"retire previous directory")&&strstr(Error,"32")&&MdoHomeExternalStat("mcp/held-install/mcp.json",&Exists,&Info)&&Exists;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("failed_transaction_keeps_cause_and_installation"),xrtValueBool(Failed));SdkPut(R,"failure_message",Error);
 MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
 if os.name!='nt':print('SKIP Windows sharing regression on a non-Windows host');return
 out=ROOT/'.build/extension-rename-native';out.mkdir(exist_ok=True,parents=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir()
 try:
  result=product(a,fixture,CONTRACT,out/'result.json');assert all(result.get(k) for k in ('transient_holder_retired','collision_never_overwrites','permanent_holder_is_bounded','failed_transaction_keeps_cause_and_installation')),result;print(json.dumps(result))
 finally:
  if fixture.parent==out and fixture.name.startswith('home-'):shutil.rmtree(fixture)
if __name__=='__main__':main()
