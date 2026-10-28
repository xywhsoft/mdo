"""All nine directory editor routes, real ZIP exports and store ownership."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
static void QaFlag(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject(),*Out=NULL;char Error[512]={0};bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V),Ok=Home&&MdoExtensionsInit()&&MdoExtensionTasksInit();
 XS_ServerInfo Server={0};Server.Name="directory-fixture";Server.Port=58013;SdkServer=&Server;g_MdoApiInitialized=true;Ok=Ok&&MdoApiWriteInit()&&MdoApiExtensionTasksInit();
 mdo_sdk_context_v1 C={0};C.Size=sizeof(C);C.Version=MDO_SDK_VERSION;C.Api=MdoApplicationApi();C.ExtensionId="directory-fixture";C.Origin=MDO_ORIGIN_USER;mdo_error_v1 E={0};
 const cstr Kinds[]={"agents","subagents","tools","mcp","skills","commands","external-tools","knowledge","plugins"};size_t Passed=0;
 for(size_t I=0;Ok&&I<9;I++){
  cstr Kind=Kinds[I];xvalue* Request=SdkParse("{\"path\":{\"id\":\"native-api\"},\"body\":{\"expected_revision\":\"\",\"trust_code\":true,\"edits\":[{\"path\":\"entry.txt\",\"text\":\"native API resource\"},{\"path\":\".hidden/deep/file.txt\",\"text\":\"kept\"}],\"manifest\":{\"name\":\"API directory\",\"alias\":\"api-resource\",\"entry\":\"entry.txt\",\"platforms\":[\"any\"]}}}");
  xvalue* Path=xrtValueObjectGet(Request,XRT_STR_LITERAL("path")),*Body=xrtValueObjectGet(Request,XRT_STR_LITERAL("body")),*Manifest=xrtValueObjectGet(Body,XRT_STR_LITERAL("manifest"));SdkPut(Path,"kind",Kind);SdkPut(Manifest,"kind",Kind);
  Ok=mdoCall(&C,"product.extensions.by_kind.by_id.put",Request,&Out,&E)==MDO_RESULT_OK;xrtValueRelease(Out);Out=NULL;
  Ok=Ok&&mdoCall(&C,"product.extensions.by_kind.by_id.get",Request,&Out,&E)==MDO_RESULT_OK;const xvalue* Data=SdkGet(Out,"data");char Hash[65]={0};if(Data)snprintf(Hash,sizeof(Hash),"%s",SdkField(Data,"revision"));Ok=Ok&&strlen(Hash)==64&&MdoExtOpBool(Data,"editable",false)&&SdkGet(Data,"manifest");xrtValueRelease(Out);Out=NULL;
  xvalue* Q=SdkParse("{\"path\":\".hidden/deep/file.txt\"}");xrtValueObjectSet(Request,XRT_STR_LITERAL("query"),Q);xrtValueRelease(Q);Ok=Ok&&mdoCall(&C,"product.extensions.by_kind.by_id.file.get",Request,&Out,&E)==MDO_RESULT_OK&&!strcmp(SdkField(SdkGet(Out,"data"),"text"),"kept");xrtValueRelease(Out);Out=NULL;
  Q=SdkParse("{\"offset\":1,\"limit\":1}");xrtValueObjectSet(Request,XRT_STR_LITERAL("query"),Q);xrtValueRelease(Q);Ok=Ok&&mdoCall(&C,"product.extensions.by_kind.by_id.files.get",Request,&Out,&E)==MDO_RESULT_OK&&xrtValueCount(SdkGet(SdkGet(Out,"data"),"items"))==1;xrtValueRelease(Out);Out=NULL;xrtValueObjectRemove(Request,XRT_STR_LITERAL("query"));
  Ok=Ok&&mdoCall(&C,"product.extensions.by_kind.by_id.pack.post",Request,&Out,&E)==MDO_RESULT_OK;char Task[33]={0};Data=SdkGet(Out,"data");if(Data&&SdkField(Data,"id"))snprintf(Task,sizeof(Task),"%s",SdkField(Data,"id"));char* Archive=Task[0]?MdoExtensionTaskArchive(Task,Error):NULL;xfile F=Archive?MdoHomeOpenRead(Archive):NULL;MdoZip Zip={0};Ok=Ok&&F&&MdoZipOpen(&Zip,F)&&MdoZipFind(&Zip,".hidden/deep/file.txt")>=0&&MdoZipFind(&Zip,"extension.json")>=0;MdoZipClose(&Zip);xrtClose(F);xrtFree(Archive);xrtValueRelease(Out);Out=NULL;
  if(Task[0]){xvalue* Ignored=NULL;MdoExtensionTaskCancel(Task,&Ignored,Error);xrtValueRelease(Ignored);MdoExtensionTaskForget(Task,Error);}
  SdkPut(Body,"expected_revision",Hash);Ok=Ok&&mdoCall(&C,"product.extensions.by_kind.by_id.delete",Request,&Out,&E)==MDO_RESULT_OK;xrtValueRelease(Out);Out=NULL;xrtValueRelease(Request);if(Ok)Passed++;else{SdkPut(R,"failed_kind",Kind);SdkPut(R,"failed_message",E.Message);}
 }
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("nine_directory_routes"),xrtValueBool(Passed==9));SdkPutUInt(R,"types",Passed);
 xvalue* Bad=SdkParse("{\"path\":{\"kind\":\"mcp\",\"id\":\"old-json\"},\"body\":{\"content\":\"{}\",\"files\":[{\"path\":\"program.exe\",\"base64\":\"AA==\"}]}}");
 QaFlag(R,"old_embedded_file_editor_rejected",mdoCall(&C,"product.extensions.by_kind.by_id.put",Bad,&Out,&E)!=MDO_RESULT_OK);xrtValueRelease(Out);xrtValueRelease(Bad);
 SdkPut(R,"error",E.Message);g_MdoApiInitialized=false;SdkServer=NULL;MdoApiExtensionTasksUnit();MdoApiWriteUnit();MdoExtensionTasksUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/extension-directory-api-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir()
    try:
        result=product(a,fixture,CONTRACT,out/'result.json')
        assert result.get('types')==9 and result.get('nine_directory_routes') and result.get('old_embedded_file_editor_rejected'),result
        print(json.dumps(result,ensure_ascii=False))
    finally:
        if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)

if __name__=='__main__':main()
