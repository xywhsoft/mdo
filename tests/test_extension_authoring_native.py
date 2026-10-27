"""Common authoring and whole native-directory import, no JSON file payloads."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static void QaFlag(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();char Error[512]={0},Hash[65],Old[65];bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);bool Ok=Home&&MdoExtensionsInit();
 xvalue* Manifest=SdkParse("{\"kind\":\"tools\",\"name\":\"Local C tool\",\"alias\":\"qa-tool\",\"entry\":\"src/tool.c\",\"platforms\":[\"any\"]}"),*Out=NULL;
 bool Unapproved=!MdoExtensionsCreate("tools","qa-local",Manifest,"// source\n",10,false,&Out,Error);xrtValueRelease(Out);Out=NULL;QaFlag(R,"code_approval",Unapproved);
 Ok=Ok&&MdoExtensionsCreate("tools","qa-local",Manifest,"// source\n",10,true,&Out,Error);xrtValueRelease(Out);Out=NULL;QaFlag(R,"atomic_creation",Ok);
 xvalue* Item=Ok?MdoExtensionsRead("tools","qa-local",Error):NULL;bool Enabled=true;xrtValueGetBool(SdkGet(Item,"enabled"),&Enabled);QaFlag(R,"created_disabled",Item&&!Enabled);xrtValueRelease(Item);xrtValueRelease(Manifest);
 Ok=Ok&&MdoExtensionsFingerprint("tools","qa-local",Hash,NULL,Error);snprintf(Old,sizeof(Old),"%s",Hash);
 Ok=Ok&&MdoExtensionsEdit("tools","qa-local","write",".venv/a/b/helper.h",NULL,"#define QA 1\n",13,Hash,&Out,Error);if(Out)snprintf(Hash,sizeof(Hash),"%s",SdkField(Out,"revision"));xrtValueRelease(Out);Out=NULL;QaFlag(R,"hidden_deep_write",Ok);
 QaFlag(R,"stale_save_rejected",!MdoExtensionsEdit("tools","qa-local","write","src/tool.c",NULL,"lost",4,Old,&Out,Error));xrtValueRelease(Out);Out=NULL;
 QaFlag(R,"entry_removal_rejected",!MdoExtensionsEdit("tools","qa-local","remove","src/tool.c",NULL,NULL,0,Hash,&Out,Error));xrtValueRelease(Out);Out=NULL;
 Manifest=SdkParse("{\"kind\":\"tools\",\"name\":\"Bad entry\",\"alias\":\"qa\",\"entry\":\"absent.c\",\"platforms\":[\"any\"]}");char* Text=SdkJson(Manifest);
 QaFlag(R,"missing_entry_metadata_rejected",!MdoExtensionsEdit("tools","qa-local","write","extension.json",NULL,Text,strlen(Text),Hash,&Out,Error));xrtFree(Text);xrtValueRelease(Manifest);xrtValueRelease(Out);Out=NULL;
 Ok=Ok&&MdoExtensionsSetEnabled("tools","qa-local",true,Error);MdoExtensionLease* Lease=Ok?MdoExtensionsAcquire("tools","qa-local",Error):NULL;
 QaFlag(R,"leased_edit_rejected",Lease&&!MdoExtensionsEdit("tools","qa-local","write","src/tool.c",NULL,"// new",6,Hash,&Out,Error));xrtValueRelease(Out);Out=NULL;MdoExtensionsRelease(Lease);
 char* Native=xrtFormat("%s/source",SdkField(P,"root"));bool Imported=MdoExtensionsImportDirectory("commands","0123456789ABCDEFGHJKMNPQRSTVWXYZ",Native,"",true,NULL,&Out,Error);xrtFree(Native);xrtValueRelease(Out);Out=NULL;QaFlag(R,"whole_native_directory_import",Imported);
 xvalue* Files=Imported?MdoExtensionsFiles("commands","0123456789ABCDEFGHJKMNPQRSTVWXYZ",NULL,Error):NULL;bool Hidden=false,Empty=false,Dependency=false;
 for(size_t I=0;I<xrtValueCount(Files);I++){cstr Path=SdkField(xrtValueArrayGet(Files,I),"path");if(!strcmp(Path,".hidden/deep/data.bin"))Hidden=true;if(!strcmp(Path,"empty"))Empty=true;if(!strcmp(Path,"include/helper.h"))Dependency=true;}
 QaFlag(R,"all_native_files_kept",Hidden&&Empty&&Dependency);xrtValueRelease(Files);
 Native=xrtFormat("%s/source",SdkField(P,"root"));bool Duplicate=!MdoExtensionsImportDirectory("commands","0123456789ABCDEFGHJKMNPQRSTVWXYZ",Native,"",true,NULL,&Out,Error);xrtFree(Native);xrtValueRelease(Out);Out=NULL;QaFlag(R,"existing_identity_precondition",Duplicate);
 bool Matched=Imported&&MdoExtensionsFingerprint("commands","0123456789ABCDEFGHJKMNPQRSTVWXYZ",Hash,NULL,Error);
 Native=xrtFormat("%s/source",SdkField(P,"root"));Matched=Matched&&MdoExtensionsImportDirectory("commands","0123456789ABCDEFGHJKMNPQRSTVWXYZ",Native,Hash,true,NULL,&Out,Error);xrtFree(Native);xrtValueRelease(Out);Out=NULL;QaFlag(R,"replace_with_current_fingerprint",Matched);
 bool Removed=MdoExtensionsRemove("tools","qa-local",&Out,Error);xrtValueRelease(Out);Out=NULL;Removed=Removed&&MdoExtensionsRemove("commands","0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Out,Error);xrtValueRelease(Out);QaFlag(R,"whole_directories_removed",Removed);
 mdo_sdk_context_v1 C={0};C.Size=sizeof(C);C.Version=MDO_SDK_VERSION;C.Api=MdoApplicationApi();C.ExtensionId="qa-author";mdo_error_v1 E={0};
 xvalue* Request=SdkParse("{\"kind\":\"skills\",\"id\":\"sdk-local\",\"manifest\":{\"kind\":\"skills\",\"name\":\"SDK Skill\",\"alias\":\"sdk-skill\",\"entry\":\"SKILL.md\",\"platforms\":[\"any\"]},\"entry_text\":\"---\\nname: sdk-skill\\ndescription: SDK example\\n---\\nUse the directory.\\n\"}");Out=NULL;
 bool Sdk=mdoCall(&C,"extension.create",Request,&Out,&E)==MDO_RESULT_OK;xrtValueRelease(Out);Out=NULL;
 Sdk=Sdk&&mdoCall(&C,"extension.pack",Request,&Out,&E)==MDO_RESULT_OK&&SdkField(Out,"archive")&&SdkField(Out,"sha256");QaFlag(R,"sdk_create_and_zip_path",Sdk);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Request);
 QaFlag(R,"sdk_directory_removed",MdoExtensionsRemove("skills","sdk-local",&Out,Error));xrtValueRelease(Out);
 SdkPut(R,"error",Error);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('--url', required=True); p.add_argument('--home', type=Path, required=True); p.add_argument('--project', default='default'); a = p.parse_args()
    out = ROOT / '.build/extension-authoring-native'; out.mkdir(parents=True, exist_ok=True); fixture = out / ('home-' + uuid.uuid4().hex[:12]); source = fixture / 'source'; source.mkdir(parents=True)
    (source / 'extension.json').write_text(json.dumps(dict(kind='commands', name='Directory command', alias='qa-command', entry='command.c', platforms=['any'])))
    (source / 'command.c').write_text('// command source')
    (source / 'include').mkdir(); (source / 'include/helper.h').write_text('#define FIXTURE 1\n')
    (source / '.hidden/deep').mkdir(parents=True); (source / '.hidden/deep/data.bin').write_bytes(bytes(range(256)))
    (source / 'empty').mkdir()
    try:
        result = product(a, fixture, CONTRACT, out / 'result.json')
        for key, value in result.items():
            if key != 'error': assert value is True, (key, result)
        assert len(result) >= 13, result
        assert not (fixture / 'tools/qa-local').exists() and not (fixture / 'commands/0123456789ABCDEFGHJKMNPQRSTVWXYZ').exists()
        print(json.dumps(result, ensure_ascii=False))
    finally:
        if fixture.resolve().parent == out.resolve() and fixture.name.startswith('home-'): shutil.rmtree(fixture)


if __name__ == '__main__': main()
