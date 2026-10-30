"""Run directory discovery and C commands against real auxiliary files."""
import argparse
import json
from pathlib import Path
import shutil
import uuid

from native_extension_runner import run
from test_extension_directory_native import HOME_STUBS

ROOT = Path(__file__).resolve().parents[1]
TEST = r'''
char* MdoHomeExternalPath(cstr Path){return xrtPathJoin(xrtRootPath(QaRoot),Path);}
xfile MdoResourceOpenRead(cstr Path){return MdoHomeOpenRead(Path);}
static char QaCompilerError[2048];
static void QaCompileError(void* User,cstr Message){(void)User;snprintf(QaCompilerError,sizeof(QaCompilerError),"%s",Message);}
static bool QaExecute(cstr Directory,cstr Source,cstr Include,int64 Expected){
 xvfs V=xrtVfsCreate();xvfsdisk Files=xrtVfsDiskCreate(xrtRootPath(QaRoot),XVFS_DISK_READ),Sdk=xrtVfsDiskCreate(Include,XVFS_DISK_READ);
 xvfsmount A=V&&Files?xrtVfsDiskMount(V,"/app/extensions",0,XVFS_CASE_SENSITIVE,Files,0):NULL;
 xvfsmount B=V&&Sdk?xrtVfsDiskMount(V,"/app/sdk",0,XVFS_CASE_SENSITIVE,Sdk,0):NULL;
 xs_tcc_config Config;xsTccConfigInit(&Config);Config.ApplicationVfs=V;Config.ApplicationRoot=xrtRootPath(QaRoot);
 TCCState* T=A&&B?xsCreateTCCEx(&Config,NULL):NULL;if(!T)return false;
 tcc_set_error_func(T,NULL,QaCompileError);tcc_add_include_path(T,"/app/sdk");
 char* Native=MdoHomeExternalPath(Directory),*Virtual=xrtFormat("/app/extensions/%s",Directory);tcc_add_include_path(T,Virtual);xrtFree(Virtual);
 union{void* P;mdo_command_run_v1 F;} Entry={0};bool Ok=tcc_compile_string(T,Source)==0&&tcc_relocate(T)==0;
 if(Ok)Entry.P=tcc_get_symbol(T,MDO_COMMAND_ENTRY);
 mdo_command_context_v1 Context={0};Context.Size=sizeof(Context);Context.AbiVersion=1;
 Context.CommandId="QA-Alias";Context.DirectoryId="0123456789ABCDEFGHJKMNPQRSTVWXYZ";Context.ExtensionRoot=Native;
 xvalue* Result=NULL;Ok=Ok&&Entry.F&&Entry.F(&Context,NULL,&Result)==MDO_RESULT_OK;int64 N=0;
 xstrview Text={0};Ok=Ok&&xrtValueGetInt(xrtValueObjectGet(Result,XRT_STR_LITERAL("value")),&N)&&N==Expected&&
  xrtValueGetString(xrtValueObjectGet(Result,XRT_STR_LITERAL("text")),&Text)&&xrtStrEqual(Text,XRT_STR_LITERAL("resource from directory"));
 xrtValueRelease(Result);xrtFree(Native);xsDestroyTCC(T);xrtVfsMountDestroy(A);xrtVfsMountDestroy(B);xrtVfsDiskDestroy(Files);xrtVfsDiskDestroy(Sdk);xrtVfsDestroy(V);return Ok;
}
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 (void)C;const xvalue* P=xrtValueArrayGet(A,0);xstrview Root={0},Include={0};
 xrtValueGetString(xrtValueObjectGet(P,XRT_STR_LITERAL("root")),&Root);
 xrtValueGetString(xrtValueObjectGet(P,XRT_STR_LITERAL("include")),&Include);
 QaRoot=xrtRootOpen(Root.Data);if(!QaRoot||!MdoExtensionsInit())return MDO_RESULT_ERROR;
 *R=xrtValueObject();char Error[512]={0},Revision[65]={0},After[65]={0};char* Source=NULL;xvalue* Meta=NULL;
 bool Ok=MdoCommandResolve(NULL,"QA-Alias",&Source,&Meta,Revision,Error);
 cstr Id=CommandText(Meta,"directory_id");
 bool Alias=Ok&&Id&&!strcmp(Id,"0123456789ABCDEFGHJKMNPQRSTVWXYZ")&&strstr(CommandText(Meta,"path"),"/nested/command.c");
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("xid_keeps_alias"),xrtValueBool(Alias));
 bool Executed=Alias&&QaExecute(CommandText(Meta,"directory_root"),Source,Include.Data,17);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("compiled_header_and_resource"),xrtValueBool(Executed));
 MdoExtensionLease* Lease=MdoExtensionsAcquire("commands",Id,Error);xvalue* Removed=NULL;
 bool Pinned=Lease&&!MdoExtensionsRemove("commands",Id,&Removed,Error);xrtValueRelease(Removed);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("running_directory_pinned"),xrtValueBool(Pinned));
 MdoExtensionsRelease(Lease);
 bool Changed=Executed&&MdoHomeAtomicWrite("commands/0123456789ABCDEFGHJKMNPQRSTVWXYZ/helper.h","#define QA_VALUE 29\n",20,false);
 Changed=Changed&&MdoExtensionsFingerprint("commands",Id,After,NULL,Error)&&strcmp(After,Revision)&&QaExecute(CommandText(Meta,"directory_root"),Source,Include.Data,29);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("auxiliary_edit_changes_execution"),xrtValueBool(Changed));
 char* Entry=CommandText(Meta,"path")?xrtStrDup(CommandText(Meta,"path")):NULL;
 char* Description=Entry?MdoExtensionRead("commands/0123456789ABCDEFGHJKMNPQRSTVWXYZ/extension.json",true,65536,NULL):NULL;
 bool Duplicate=Description&&MdoHomeAtomicWrite("commands/other-directory/extension.json",Description,strlen(Description),false)&&
   MdoHomeAtomicWrite("commands/other-directory/nested/command.c",Source,strlen(Source),false);
 xvalue* Catalog=Duplicate?MdoCommandCatalog(NULL):NULL;bool Valid=true;
 xrtValueGetBool(xrtValueObjectGet(xrtValueObjectGet(Catalog,XRT_STR_LITERAL("QA-Alias")),XRT_STR_LITERAL("valid")),&Valid);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("duplicate_alias_blocked"),xrtValueBool(Duplicate&&Catalog&&!Valid));xrtValueRelease(Catalog);
 xrtFree(Description);xrtFree(Entry);xrtValueRelease(Meta);xrtFree(Source);Source=NULL;Meta=NULL;
 bool Legacy=!MdoCommandResolve(NULL,"legacy-flat",&Source,&Meta,After,Error);xrtFree(Source);xrtValueRelease(Meta);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("flat_command_not_discovered"),xrtValueBool(Legacy));
 MdoExtensionsSetEnabled("commands","other-directory",false,Error);
 Removed=NULL;bool Uninstalled=MdoExtensionsRemove("commands","0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Removed,Error);xrtValueRelease(Removed);
 bool Present=true;xfileinfo Info;Uninstalled=Uninstalled&&MdoHomeExternalStat("commands/0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Present,&Info)&&!Present;
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("whole_directory_removed"),xrtValueBool(Uninstalled));
 MdoExtPut(*R,"compiler_error",QaCompilerError);MdoExtPut(*R,"error",Error);
 MdoExtensionsUnit();xrtRootClose(QaRoot);QaRoot=NULL;return MDO_RESULT_OK;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--home', type=Path, required=True)
    parser.add_argument('--project', default='default')
    args = parser.parse_args()
    out = ROOT / '.build/command-directory-native'; out.mkdir(parents=True, exist_ok=True)
    fixture = out / ('home-' + uuid.uuid4().hex[:12]); fixture.mkdir()
    directory = fixture / 'commands/0123456789ABCDEFGHJKMNPQRSTVWXYZ'; (directory / 'nested').mkdir(parents=True)
    manifest = {'kind': 'commands', 'id':'command:qa.alias', 'name': 'Directory QA', 'alias': 'QA-Alias', 'entry': 'nested/command.c', 'platforms': ['any']}
    (directory / 'extension.json').write_text(json.dumps(manifest), encoding='utf-8')
    meta = {'id': 'QA-Alias', 'description': 'Native auxiliary C command', 'effects': ['read']}
    command = '/* mdo-command ' + json.dumps(meta) + ' */\n' + r'''
#include "mdo/command.h"
#include <xsbase.h>
#include "helper.h"
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 (void)A;char* P=xrtPathJoin(C->ExtensionRoot,"resources/note.txt");size_t N=0;
 char* Text=(char*)xrtFileReadAllLimit(P,1024,&N);xrtFree(P);if(!Text)return MDO_RESULT_ERROR;
 *R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("text"),xrtValueString(xrtStrViewN(Text,N)));
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("value"),xrtValueInt(QA_VALUE));xrtFree(Text);return MDO_RESULT_OK;
}
'''
    (directory / 'nested/command.c').write_text(command, encoding='utf-8')
    (directory / 'helper.h').write_text('#define QA_VALUE 17\n', encoding='utf-8')
    (directory / 'resources').mkdir(); (directory / 'resources/note.txt').write_bytes(b'resource from directory')
    (fixture / 'commands/legacy-flat.c').write_text(command.replace('QA-Alias', 'legacy-flat'), encoding='utf-8')
    ident = 'command-directory-qa-' + uuid.uuid4().hex[:12]
    inner = '#include "/app/sdk/mdo/command.h"\n#include <xsbase.h>\n#include <libtcc.h>\n'
    for path in ('include/mdo/extensions.h', 'include/mdo/home.h', 'include/mdo/extension_manifest.h'):
        inner += '#include "/app/product/' + path + '"\n'
    inner += HOME_STUBS
    for path in ('app/src/extensions/manager.c', 'app/src/commands/catalog.c'):
        inner += '#include "/app/product/' + path.removeprefix('app/') + '"\n'
    inner += TEST.replace('mdoCommandRun(const mdo_command_context_v1* C', 'QaRun(const mdo_command_context_v1* C')
    (fixture / 'contract.c').write_text(inner, encoding='utf-8')
    source = '/* mdo-command ' + json.dumps({'id': ident, 'description': 'Native directory command regression', 'effects': ['read', 'workspace-write'], 'timeout_ms': 60000}) + ' */\n' + r'''
#include "mdo/command.h"
#include <xsbase.h>
#include <libtcc.h>
static char QaLog[8192];
static void QaError(void* User,cstr Message){(void)User;snprintf(QaLog,sizeof(QaLog),"%s",Message);}
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 const xvalue* P=xrtValueArrayGet(A,0);xstrview Paths[3]={0};const char* Keys[]={"root","product","include"},*Mounts[]={"/app","/app/product","/app/sdk"};
 xvfs V=xrtVfsCreate();xvfsdisk Disk[3]={0};xvfsmount Mount[3]={0};bool Ok=V!=NULL;
 for(size_t I=0;I<3;I++){xrtValueGetString(xrtValueObjectGet(P,xrtStrView(Keys[I])),&Paths[I]);Disk[I]=xrtVfsDiskCreate(Paths[I].Data,XVFS_DISK_READ);
  Mount[I]=V&&Disk[I]?xrtVfsDiskMount(V,Mounts[I],0,XVFS_CASE_SENSITIVE,Disk[I],0):NULL;Ok=Ok&&Mount[I];}
 xs_tcc_config Config;xsTccConfigInit(&Config);Config.ApplicationVfs=V;Config.ApplicationRoot=Paths[0].Data;
 TCCState* T=Ok?xsCreateTCCEx(&Config,NULL):NULL;union{void* P;mdo_command_run_v1 F;} Entry={0};
 if(T){tcc_set_error_func(T,NULL,QaError);Ok=tcc_compile_string(T,"#include \"/app/contract.c\"\n")==0&&tcc_relocate(T)==0;if(Ok)Entry.P=tcc_get_symbol(T,"QaRun");}
 mdo_result Code=Ok&&Entry.F?Entry.F(C,A,R):MDO_RESULT_ERROR;
 if(Code!=MDO_RESULT_OK){*R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("compile_error"),xrtValueString(xrtStrView(QaLog)));}
 if(T)xsDestroyTCC(T);for(size_t I=0;I<3;I++){if(Mount[I])xrtVfsMountDestroy(Mount[I]);if(Disk[I])xrtVfsDiskDestroy(Disk[I]);}if(V)xrtVfsDestroy(V);
 return MDO_RESULT_OK;
}
'''
    try:
        result = run(args, ident, source, {'root': str(fixture), 'include': str(ROOT / 'include'), 'product': str(ROOT / 'app')}, out / 'native-result.json')
        for key in ('xid_keeps_alias', 'compiled_header_and_resource', 'running_directory_pinned', 'auxiliary_edit_changes_execution', 'duplicate_alias_blocked', 'flat_command_not_discovered', 'whole_directory_removed'):
            assert result.get(key), (key, result)
        print(json.dumps(result, ensure_ascii=False))
    finally:
        if fixture.resolve().parent == out.resolve() and fixture.name.startswith('home-'):
            shutil.rmtree(fixture)


if __name__ == '__main__':
    main()
