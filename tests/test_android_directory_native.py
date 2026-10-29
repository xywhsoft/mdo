"""Run the actual APK ZIP installer/cache on the existing host (no Android VM).

The fixture uses an any-platform inert ELF marker, not a real Android execution
claim. Real APK compilation and device execution are separate release gates.
"""
import argparse,hashlib,io,json,shutil,struct,uuid,zipfile
from pathlib import Path
from native_product_runner import product
ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
static void AndroidFlag(xvalue* R,cstr Key,bool B){xrtValueObjectSetNew(R,xrtStrView(Key),xrtValueBool(B));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();char E[512]={0};cstr Root=SdkField(P,"root");bool Home=QaProductHome(Root,SdkField(P,"app"),V),Ok=Home&&MdoExtensionsInit();
 char* Apk=xrtPathJoin(Root,"fixture.apk"),*Native=xrtPathJoin(Root,"native"),*Program=xrtPathJoin(Native,"libfixture.so");
 bool Loaded=Ok&&MdoAndroidToolsLoad(Apk,Native,E);AndroidFlag(R,"signed_apk_index_loads",Loaded);if(!Loaded)SdkPut(R,"load_error",E);
 xfile F=MdoHomeOpenRead("external-tools/fixture/bin/program");char B[32]={0};size_t N=0;bool Bytes=F&&xrtRead(F,B,sizeof(B),&N)&&N==7&&!memcmp(B,"\x7f" "ELFone",7);xrtClose(F);
 xvalue* Files=MdoExtensionsFiles("external-tools","fixture",NULL,E);bool Hidden=false,Empty=false;for(size_t I=0;I<xrtValueCount(Files);I++){const xvalue* Row=xrtValueArrayGet(Files,I);cstr Path=SdkField(Row,"path");if(Path&&!strcmp(Path,".hidden/data.txt"))Hidden=true;if(Path&&!strcmp(Path,"empty"))Empty=true;}xrtValueRelease(Files);AndroidFlag(R,"whole_directory_regular_bytes_hidden_and_empty",Bytes&&Hidden&&Empty);
 bool Ready=Loaded&&MdoAndroidToolPrepare("fixture",E);if(!Ready){SdkPut(R,"binding_error",E);SdkPut(R,"os_error",xrtErrorMessage(xrtGetError()));xrtValueObjectSetNew(R,XRT_STR_LITERAL("binding_os_code"),xrtValueInt(xrtErrorSystemCode(xrtGetError())));}char* Executable=MdoAndroidToolProgram("fixture","bin/program"),*Target=Executable?xrtPathReal(Executable):NULL,*Expected=xrtPathReal(Program);AndroidFlag(R,"derived_native_and_data_bindings",Ready&&Target&&Expected&&!strcmp(Target,Expected));xrtFree(Target);xrtFree(Expected);xrtFree(Executable);
 char* Link=MdoHomeExternalPath("data/external-tools/android/fixture/.hidden/data.txt");xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ;F=Link?xrtFileOpen(Link,&O):NULL;N=0;bool Data=F&&xrtRead(F,B,sizeof(B),&N)&&N==4&&!memcmp(B,"data",4);xrtClose(F);xrtFree(Link);AndroidFlag(R,"data_binding_reads_canonical_file",Data);
 AndroidFlag(R,"disabled_survives_apk_reload",MdoExtensionsSetEnabled("external-tools","fixture",false,E)&&MdoAndroidToolsLoad(Apk,Native,E));xvalue* Meta=MdoExtensionsRead("external-tools","fixture",E);bool Enabled=true;xrtValueGetBool(xrtValueObjectGet(Meta,XRT_STR_LITERAL("enabled")),&Enabled);AndroidFlag(R,"reload_preserves_disabled",!Enabled);xrtValueRelease(Meta);
 AndroidFlag(R,"native_hash_mismatch_refused",MdoHomeAtomicWrite("native/libfixture.so","bad",3,false)&&!MdoAndroidToolPrepare("fixture",E));
 xvalue* Removed=NULL;bool RemovedOk=MdoExtensionsRemove("external-tools","fixture",&Removed,E)&&MdoAndroidToolClean("fixture");xrtValueRelease(Removed);AndroidFlag(R,"uninstall_cleans_directory_and_derived_links",RemovedOk);
 bool Exists=true;xfileinfo Info;bool Restart=MdoAndroidToolsLoad(Apk,Native,E)&&MdoHomeExternalStat("external-tools/fixture",&Exists,&Info)&&!Exists;AndroidFlag(R,"restart_does_not_resurrect_uninstalled",Restart);
 F=MdoHomeOpenRead("fixture.apk");MdoZip Z={0};AndroidFlag(R,"extension_zip_still_rejects_apk_signing_gap",F&&!MdoZipOpen(&Z,F));MdoZipClose(&Z);xrtClose(F);
 MdoAndroidToolsUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();xrtFree(Apk);xrtFree(Native);xrtFree(Program);return R;
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',required=True,type=Path);p.add_argument('--project',default='default');a=p.parse_args();out=ROOT/'.build/android-directory-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir();(fixture/'native').mkdir();program=b'\x7fELFone';(fixture/'native/libfixture.so').write_bytes(program)
 inner=io.BytesIO()
 with zipfile.ZipFile(inner,'w',compression=zipfile.ZIP_DEFLATED) as z:
  for path,data in {'extension.json':json.dumps(dict(kind='external-tools',id='external-tool:qa.fixture',name='Fixture',alias='fixture',entry='tool.json',platforms=['any'])),'tool.json':json.dumps(dict(program='bin/program',prompt='Fixture native program')),'bin/program':program,'.hidden/data.txt':'data','empty/':''}.items():z.writestr(path,data)
 raw=inner.getvalue();asset='assets/mdo-extensions/fixture.zip';index=dict(extensions=[dict(id='fixture',asset=asset,sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw))],files=[dict(tool='fixture',path='bin/program',native='libfixture.so',sha256=hashlib.sha256(program).hexdigest())]);outer=io.BytesIO()
 with zipfile.ZipFile(outer,'w') as z:z.writestr(asset,raw);z.writestr('assets/mdo-runtime.json',json.dumps(index))
 data=outer.getvalue();end=len(data)-22;central=struct.unpack_from('<I',data,end+16)[0];block=struct.pack('<Q',24)+struct.pack('<Q',24)+b'APK Sig Block 42';data=bytearray(data[:central]+block+data[central:]);struct.pack_into('<I',data,end+len(block)+16,central+len(block));(fixture/'fixture.apk').write_bytes(data)
 try:
  result=product(a,fixture,CONTRACT,out/'result.json')
  skipped=__import__('os').name=='nt' and result.get('binding_os_code')==1314
  exceptions={'binding_error','os_error','binding_os_code','derived_native_and_data_bindings','data_binding_reads_canonical_file'} if skipped else set()
  assert len(result)>=9 and all(v is True for k,v in result.items() if k not in exceptions),result
  print(json.dumps(dict(result=result,binding_skipped_windows_privilege=skipped)))
 finally:
  if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)
if __name__=='__main__':main()
