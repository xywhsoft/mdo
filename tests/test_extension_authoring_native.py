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
 xvalue* R=xrtValueObject();char Error[512]={0},Hash[65],Old[65];bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);bool Ok=Home&&MdoExtensionsInit()&&MdoExtensionTasksInit();
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
 xvalue* Third=SdkParse("{\"kind\":\"skills\",\"id\":\"third-import\",\"expected_revision\":\"\",\"manifest\":{\"kind\":\"skills\",\"name\":\"Imported skill\",\"alias\":\"Imported-Skill\",\"entry\":\"SKILL.md\",\"platforms\":[\"any\"]}}");Native=xrtFormat("%s/third-party",SdkField(P,"root"));SdkPut(Third,"directory",Native);bool ThirdParty=mdoCall(&C,"extension.import",Third,&Out,&E)==MDO_RESULT_OK;bool OriginalManifest=true;xfileinfo ThirdInfo;char* ManifestPath=xrtFormat("%s/extension.json",Native);ThirdParty=ThirdParty&&!xrtPathStat(ManifestPath,false,&ThirdInfo);xrtClearError();xrtFree(ManifestPath);xrtFree(Native);xrtValueRelease(Third);xrtValueRelease(Out);Out=NULL;
 xvalue* ThirdFiles=ThirdParty?MdoExtensionsFiles("skills","third-import",NULL,Error):NULL;bool KeptHidden=false,KeptEmpty=false,HasManifest=false;for(size_t I=0;I<xrtValueCount(ThirdFiles);I++){cstr Path=SdkField(xrtValueArrayGet(ThirdFiles,I),"path");if(!strcmp(Path,".refs/deep/data.bin"))KeptHidden=true;if(!strcmp(Path,"empty"))KeptEmpty=true;if(!strcmp(Path,"extension.json"))HasManifest=true;}xrtValueRelease(ThirdFiles);QaFlag(R,"third_party_directory_gets_manifest_without_source_edits",ThirdParty&&KeptHidden&&KeptEmpty&&HasManifest);
 XS_ServerInfo SourceServer={0};SourceServer.Name="source-import-fixture";SourceServer.Port=58013;SdkServer=&SourceServer;g_MdoApiInitialized=true;bool SourceReady=MdoApiWriteInit()&&MdoApiExtensionsInit();xvalue* Import=SdkParse("{\"query\":{\"project\":\"default\"},\"body\":{\"origin\":\"project\",\"provider\":\"agents\",\"id\":\"source\",\"target_id\":\"Imported-From-Project\",\"scope\":\"user\"}}");bool SourceImport=SourceReady&&mdoCall(&C,"product.skills.migrate.post",Import,&Out,&E)==MDO_RESULT_OK;Item=SourceImport?MdoExtensionsRead("skills","Imported-From-Project",Error):NULL;SourceImport=SourceImport&&Item&&!strcmp(SdkField(Item,"alias"),"Imported-From-Project");xrtValueRelease(Item);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Import);QaFlag(R,"explicit_source_import_creates_only_home_extension",SourceImport);MdoApiExtensionsUnit();MdoApiWriteUnit();g_MdoApiInitialized=false;SdkServer=NULL;

 xvalue* Request=SdkParse("{\"kind\":\"skills\",\"id\":\"sdk-local\",\"manifest\":{\"kind\":\"skills\",\"name\":\"SDK Skill\",\"alias\":\"sdk-skill\",\"entry\":\"SKILL.md\",\"platforms\":[\"any\"]},\"entry_text\":\"---\\nname: sdk-skill\\ndescription: SDK example\\n---\\nUse the directory.\\n\"}");Out=NULL;
 bool Sdk=mdoCall(&C,"extension.create",Request,&Out,&E)==MDO_RESULT_OK;xrtValueRelease(Out);Out=NULL;
 Sdk=Sdk&&mdoCall(&C,"extension.pack",Request,&Out,&E)==MDO_RESULT_OK&&SdkField(Out,"archive")&&SdkField(Out,"sha256");QaFlag(R,"sdk_create_and_zip_path",Sdk);xrtValueRelease(Out);Out=NULL;
 char* Metadata=SdkJson(SdkGet(Request,"manifest"));bool Repair=MdoHomeAtomicWrite("skills/sdk-local/extension.json","{",1,false)&&MdoExtensionsFingerprint("skills","sdk-local",Hash,NULL,Error);
 SdkPut(Request,"path","extension.json");SdkPut(Request,"text",Metadata);SdkPut(Request,"expected_revision",Hash);Repair=Repair&&mdoCall(&C,"extension.file.write",Request,&Out,&E)==MDO_RESULT_OK;QaFlag(R,"corrupt_manual_metadata_can_be_repaired",Repair);xrtFree(Metadata);xrtValueRelease(Out);Out=NULL;
 bool Deferred=MdoExtensionsFingerprint("skills","sdk-local",Hash,NULL,Error)&&MdoExtensionsScheduleRemove("skills","sdk-local",Hash,&Out,Error);xrtValueRelease(Out);Out=NULL;
 Deferred=Deferred&&MdoHomeAtomicWrite("skills/sdk-local/new-resource.txt","new",3,false)&&MdoExtensionsRecover(Error);Item=MdoExtensionsRead("skills","sdk-local",Error);Deferred=Deferred&&Item&&SdkField(Item,"pending_error");xrtValueRelease(Item);QaFlag(R,"deferred_delete_does_not_delete_changed_directory",Deferred);
 xvalue* Warnings=xrtValueArray();bool Warning=MdoExtensionsWarnings(Warnings)&&xrtValueCount(Warnings)==1&&!strcmp(SdkField(xrtValueArrayGet(Warnings,0),"code"),"uninstall_changed");QaFlag(R,"deferred_problem_joins_common_warning",Warning);xrtValueRelease(Warnings);
 bool Cancel=mdoCall(&C,"extension.cancel_remove",Request,&Out,&E)==MDO_RESULT_OK&&!MdoExtOpBool(Out,"pending_remove",false);xrtValueRelease(Out);Out=NULL;QaFlag(R,"pending_uninstall_can_be_cancelled",Cancel);
 MdoExtensionsFingerprint("skills","sdk-local",Hash,NULL,Error);SdkPut(Request,"expected_revision",Hash);QaFlag(R,"sdk_directory_removed",mdoCall(&C,"extension.remove",Request,&Out,&E)==MDO_RESULT_OK&&MdoExtOpBool(Out,"removed",false));xrtValueRelease(Out);xrtValueRelease(Request);

 xvalue* Platform=SdkParse("{\"platforms\":[\"linux-x86_64\"]}");bool PlatformMatch=MdoExtensionSupportsPlatform(Platform,"linux-x86_64-glibc")&&MdoExtensionSupportsPlatform(Platform,"linux-x86_64-musl")&&!MdoExtensionSupportsPlatform(Platform,"windows-x86_64");xrtValueRelease(Platform);Platform=SdkParse("{\"platforms\":[\"linux-x86_64-musl\"]}");PlatformMatch=PlatformMatch&&MdoExtensionSupportsPlatform(Platform,"linux-x86_64-musl")&&!MdoExtensionSupportsPlatform(Platform,"linux-x86_64-glibc");xrtValueRelease(Platform);QaFlag(R,"platform_and_libc_selection",PlatformMatch);
 xvalue* WrongManifest=SdkParse("{\"kind\":\"skills\",\"name\":\"Other platform\",\"alias\":\"1.Mixed_Alias\",\"entry\":\"SKILL.md\",\"platforms\":[\"macos-arm64\"]}");bool WrongPlatform=MdoExtensionsCreate("skills","other-platform",WrongManifest,"---\nname: Other\ndescription: fixture\n---\nTest",45,false,&Out,Error);xrtValueRelease(Out);Out=NULL;
 WrongPlatform=WrongPlatform&&!MdoExtensionsSetEnabled("skills","other-platform",true,Error)&&strstr(Error,"platform");QaFlag(R,"foreign_platform_cannot_be_enabled",WrongPlatform);xrtValueRelease(WrongManifest);
 char WrongHash[65];uint64 WrongBytes=0;bool Cross=MdoExtensionsPack("skills","other-platform","data/extension-tasks/cross-platform.zip",NULL,WrongHash,&WrongBytes,Error)&&!MdoExtensionsInstall("skills","cross-platform","data/extension-tasks/cross-platform.zip",NULL,false,NULL,&Out,Error)&&strstr(Error,"platform");QaFlag(R,"foreign_zip_rejected_before_install",Cross);xrtValueRelease(Out);Out=NULL;

 SdkPut(R,"error",Error);MdoExtensionTasksUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
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
    for directory in (fixture/'third-party',fixture/'workspace/.agents/skills/source'):
        directory.mkdir(parents=True);(directory/'SKILL.md').write_text('---\nname: Source Skill\ndescription: Explicit directory import\n---\nUse this reference',encoding='utf-8');(directory/'.refs/deep').mkdir(parents=True);(directory/'.refs/deep/data.bin').write_bytes(bytes(range(256)));(directory/'empty').mkdir()
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
