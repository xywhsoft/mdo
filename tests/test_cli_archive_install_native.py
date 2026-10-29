"""Install the actual official tool ZIPs through the product C implementation.

Payload programs are not executed. A disposable Home is removed after every
archive has been installed, disabled and uninstalled as a complete directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import uuid

from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();xvalue* Rows=xrtValueArray();char Error[512]={0};
 bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V),Ok=Home&&MdoExtensionsInit();
 char* Raw=MdoExtensionRead("archives.json",true,65536,NULL);xvalue* Input=Raw?xrtJsonParse(xrtStrView(Raw)):NULL;xrtFree(Raw);Ok=Ok&&Input;
 for(size_t I=0;Ok&&I<xrtValueCount(Input);I++){
  const xvalue* Item=xrtValueArrayGet(Input,I);cstr Alias=SdkField(Item,"alias"),Id=SdkField(Item,"id"),Path=SdkField(Item,"path");
  xvalue* Credits=xrtValueObject(),*Out=NULL,*Meta=NULL;SdkPut(Credits,"source","store");SdkPut(Credits,"id",Id);SdkPut(Credits,"sha256",SdkField(Item,"sha256"));
  bool Installed=MdoExtensionsInstall("external-tools",Alias,Path,Credits,true,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Credits);
  Meta=Installed?MdoExtensionsRead("external-tools",Alias,Error):NULL;
  bool Identity=Meta&&SdkField(Meta,"id")&&!strcmp(SdkField(Meta,"id"),Id)&&SdkField(Meta,"directory_id")&&!strcmp(SdkField(Meta,"directory_id"),Alias);
  bool Disabled=Identity&&MdoExtensionsSetEnabled("external-tools",Id,false,Error);
  bool Removed=Disabled&&MdoExtensionsRemove("external-tools",Alias,&Out,Error);xrtValueRelease(Out);xrtValueRelease(Meta);
  char* Directory=MdoExtensionsRoot("external-tools",Alias);bool Exists=true;xfileinfo Info;
  Removed=Removed&&Directory&&MdoHomeExternalStat(Directory,&Exists,&Info)&&!Exists;xrtFree(Directory);
  xvalue* Row=xrtValueObject();SdkPut(Row,"id",Id);xrtValueObjectSetNew(Row,XRT_STR_LITERAL("installed"),xrtValueBool(Installed));xrtValueObjectSetNew(Row,XRT_STR_LITERAL("identity"),xrtValueBool(Identity));xrtValueObjectSetNew(Row,XRT_STR_LITERAL("disabled"),xrtValueBool(Disabled));xrtValueObjectSetNew(Row,XRT_STR_LITERAL("removed"),xrtValueBool(Removed));SdkPut(Row,"error",Error);xrtValueArrayAppendNew(Rows,Row);Ok=Installed&&Identity&&Disabled&&Removed;
 }
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("archives"),Rows);xrtValueObjectSetNew(R,XRT_STR_LITERAL("passed"),xrtValueBool(Ok));SdkPut(R,"error",Error);
 xrtValueRelease(Input);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True)
    p.add_argument('--project',default='default');p.add_argument('--catalog',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    a.report.parent.mkdir(parents=True,exist_ok=True)
    fixture=a.report.parent/('cli-install-'+uuid.uuid4().hex[:12]);downloads=fixture/'data/extension-tasks/fixture';downloads.mkdir(parents=True)
    rows=json.loads(a.catalog.read_text(encoding='utf-8'))['tools']
    assert rows,'No generated tool archives selected'
    inputs=[]
    try:
        for row in rows:
            source=a.catalog.parent/row['file'];assert hashlib.sha256(source.read_bytes()).hexdigest()==row['sha256']
            destination=downloads/row['file'];shutil.copyfile(source,destination)
            inputs.append(dict(alias=row['alias'],id=row['id'],path=destination.relative_to(fixture).as_posix(),sha256=row['sha256']))
        (fixture/'archives.json').write_text(json.dumps(inputs),encoding='utf-8')
        result=product(a,fixture,CONTRACT,a.report)
        assert result.get('passed') and len(result['archives'])==len(inputs),result
        print('PASS',len(inputs),'actual official ZIPs: install, typed identity, disable and complete uninstall')
    finally:
        if fixture.resolve().parent==a.report.parent.resolve() and fixture.name.startswith('cli-install-'):
            shutil.rmtree(fixture)


if __name__=='__main__':main()
