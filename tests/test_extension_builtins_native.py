"""Verify real bundled ZIP installation and uninstall/restart receipts."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_extension_runner import run
from test_extension_directory_native import HOME_STUBS

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--home', type=Path, required=True)
    parser.add_argument('--project', default='default')
    args = parser.parse_args()
    parent = ROOT / '.build/builtin-directory-native'; parent.mkdir(parents=True, exist_ok=True)
    fixture = parent / ('home-' + uuid.uuid4().hex[:12]); fixture.mkdir()
    inner = '#include <xsbase.h>\n#include "/app/include/mdo/extensions.h"\n#include "/app/include/mdo/home.h"\n#include "/app/include/mdo/extension_manifest.h"\n'
    inner += 'static xvfs QaVfs;\n' + HOME_STUBS + r'''
xfile MdoResourceOpenRead(cstr P){return MdoHomeOpenRead(P);}
xvfs MdoHomeApplicationVfs(void){return QaVfs;}
bool MdoHomeRemoveEmptyDirectory(cstr Path){return xrtRootRemove(QaRoot,Path);}
#include "/app/include/mdo/extension_files.h"
#include "/app/src/extensions/manager.c"
#include "/app/src/extensions/builtins.c"
xvalue* QaBuiltins(cstr Root,xvfs V){
 QaVfs=V;QaRoot=xrtRootOpen(Root);char Error[512]={0};xvalue* R=xrtValueObject();
 bool Ok=QaRoot&&MdoExtensionsInit()&&MdoExtensionsRecover(Error)&&MdoExtensionsSeedBuiltins(Error);
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
 MdoExtPut(R,"error",Error);MdoExtensionsUnit();xrtRootClose(QaRoot);QaRoot=NULL;return R;
}
'''
    (fixture / 'contract.c').write_text(inner, encoding='utf-8')
    ident = 'builtin-directory-qa-' + uuid.uuid4().hex[:12]
    source = '/* mdo-command ' + json.dumps({'id': ident, 'description': 'Builtin directory lifecycle regression', 'effects': ['read', 'workspace-write'], 'timeout_ms': 60000}) + ' */\n' + r'''
#include "mdo/command.h"
#include <xsbase.h>
#include <libtcc.h>
static char QaLog[8192];static void QaError(void* U,cstr S){(void)U;snprintf(QaLog,sizeof(QaLog),"%s",S);}
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 (void)C;const xvalue* P=xrtValueArrayGet(A,0);xstrview Root={0},App={0};
 xrtValueGetString(xrtValueObjectGet(P,XRT_STR_LITERAL("root")),&Root);xrtValueGetString(xrtValueObjectGet(P,XRT_STR_LITERAL("app")),&App);
 xvfs V=xrtVfsCreate();xvfsdisk Disk=xrtVfsDiskCreate(App.Data,XVFS_DISK_READ),Fixture=xrtVfsDiskCreate(Root.Data,XVFS_DISK_READ);
 xvfsmount Mount=V&&Disk?xrtVfsDiskMount(V,"/app",0,XVFS_CASE_SENSITIVE,Disk,0):NULL;
 xvfsmount Test=V&&Fixture?xrtVfsDiskMount(V,"/app/qa",10,XVFS_CASE_SENSITIVE,Fixture,0):NULL;
 xs_tcc_config Config;xsTccConfigInit(&Config);Config.ApplicationVfs=V;Config.ApplicationRoot=App.Data;
 TCCState* T=Mount&&Test?xsCreateTCCEx(&Config,NULL):NULL;union{void* P;xvalue*(*F)(cstr,xvfs);} Entry={0};
 if(T){tcc_set_error_func(T,NULL,QaError);if(tcc_compile_string(T,"#include \"/app/qa/contract.c\"\n")==0&&tcc_relocate(T)==0)Entry.P=tcc_get_symbol(T,"QaBuiltins");}
 if(Entry.F)*R=Entry.F(Root.Data,V);else{*R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("compile_error"),xrtValueString(xrtStrView(QaLog)));}
 if(T)xsDestroyTCC(T);if(Test)xrtVfsMountDestroy(Test);if(Mount)xrtVfsMountDestroy(Mount);if(Fixture)xrtVfsDiskDestroy(Fixture);if(Disk)xrtVfsDiskDestroy(Disk);if(V)xrtVfsDestroy(V);
 return MDO_RESULT_OK;
}
'''
    try:
        result = run(args, ident, source, {'root': str(fixture), 'app': str(ROOT / 'app')}, parent / 'result.json')
        for key in ('installed', 'builtin_verified', 'uninstall_disable_survive_restart', 'skill_directories'):
            assert result.get(key), (key, result)
        print(json.dumps(result))
    finally:
        if fixture.resolve().parent == parent.resolve() and fixture.name.startswith('home-'):
            shutil.rmtree(fixture)


if __name__ == '__main__':
    main()
