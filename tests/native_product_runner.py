"""Invoke current product C directly in the existing host's compiler runtime."""
import json
from pathlib import Path
import uuid
from native_extension_runner import run

ROOT = Path(__file__).resolve().parents[1]

HOME = r'''
static bool QaProductHome(cstr Root,cstr App,xvfs V){
    memset(&g_MdoHome,0,sizeof(g_MdoHome));g_MdoHome.Lock=xrtMutexCreate();
    xrtVfsRef(V);g_MdoHome.ApplicationVfs=V;g_MdoHome.ApplicationRoot=xrtStrDup(App);g_MdoHome.Path=xrtStrDup(Root);
    if(!g_MdoHome.Lock||!g_MdoHome.ApplicationRoot||!g_MdoHome.Path||!MdoHomeCaptureBuiltinDefaults()||!MdoHomeMountLocked()){MdoHomeUnit();return false;}
    g_MdoHome.Initialized=true;return true;
}
'''


def product(args, fixture, contract, evidence):
    (fixture / 'contract.c').write_text(HOME + contract, encoding='utf-8')
    ident = 'product-directory-qa-' + uuid.uuid4().hex[:12]
    source = '/* mdo-command ' + json.dumps({'id': ident, 'description': 'Product directory native regression', 'effects': ['read', 'workspace-write'], 'timeout_ms': 60000}) + ' */\n' + r'''
#include "mdo/command.h"
#include <xsbase.h>
#include <libtcc.h>
static char QaLog[8192];static void QaError(void* U,cstr S){(void)U;size_t N=strlen(QaLog);if(N<sizeof(QaLog)-2)snprintf(QaLog+N,sizeof(QaLog)-N,"%s\n",S);}
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 (void)C;const xvalue* P=xrtValueArrayGet(A,0);xstrview Root={0},App={0};
 xrtValueGetString(xrtValueObjectGet(P,XRT_STR_LITERAL("root")),&Root);xrtValueGetString(xrtValueObjectGet(P,XRT_STR_LITERAL("app")),&App);
 xvfs V=xrtVfsCreate();xvfsdisk Disk=xrtVfsDiskCreate(App.Data,XVFS_DISK_READ),Fixture=xrtVfsDiskCreate(Root.Data,XVFS_DISK_READ);
 xvfsmount Mount=V&&Disk?xrtVfsDiskMount(V,"/app",0,XVFS_CASE_SENSITIVE,Disk,0):NULL;
 xvfsmount Test=V&&Fixture?xrtVfsDiskMount(V,"/app/qa",10,XVFS_CASE_SENSITIVE,Fixture,0):NULL;
 xs_tcc_config Config;xsTccConfigInit(&Config);Config.ApplicationVfs=V;Config.ApplicationRoot=App.Data;
 TCCState* T=Mount&&Test?xsCreateTCCEx(&Config,NULL):NULL;union{void* P;xvalue*(*F)(const xvalue*,xvfs);} Entry={0};
 if(T){tcc_set_error_func(T,NULL,QaError);tcc_add_include_path(T,"/app/generated");
  if(tcc_compile_string(T,"#include \"/app/generated/mdo_unity.c\"\n#include \"/app/qa/contract.c\"\n")==0&&tcc_relocate(T)==0)Entry.P=tcc_get_symbol(T,"QaProductContract");}
 if(Entry.F)*R=Entry.F(P,V);else{*R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("compile_error"),xrtValueString(xrtStrView(QaLog)));}
 if(T)xsDestroyTCC(T);if(Test)xrtVfsMountDestroy(Test);if(Mount)xrtVfsMountDestroy(Mount);if(Fixture)xrtVfsDiskDestroy(Fixture);if(Disk)xrtVfsDiskDestroy(Disk);if(V)xrtVfsDestroy(V);
 return MDO_RESULT_OK;
}
'''
    return run(args, ident, source, {'root': str(fixture), 'app': str(ROOT / 'app')}, evidence)
