"""Compile/relocate current application sources in the existing native host.

This creates only a compiler state: no second service, copied runtime, Home or
application initialization. Useful while changing the directory infrastructure.
"""
import argparse
import json
from pathlib import Path
import uuid
from native_extension_runner import run

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--home', type=Path, required=True)
    parser.add_argument('--project', default='default')
    args = parser.parse_args()
    ident = 'compile-qa-' + uuid.uuid4().hex[:12]
    meta = {'id': ident, 'description': 'Compile product source without starting it', 'effects': ['read'], 'requires_session': False, 'timeout_ms': 60000}
    source = '/* mdo-command ' + json.dumps(meta) + ' */\n' + r'''
#include "mdo/command.h"
#include <xsbase.h>
#include <libtcc.h>
#include <string.h>
static char QaLog[8192];
static void QaError(void* User,cstr Message){(void)User;size_t Used=strlen(QaLog);if(Used<sizeof(QaLog)-2)snprintf(QaLog+Used,sizeof(QaLog)-Used,"%s\n",Message);}
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 (void)C;xstrview Root={0};xrtValueGetString(xrtValueArrayGet(A,0),&Root);
 xvfs V=xrtVfsCreate();xvfsdisk Disk=xrtVfsDiskCreate(Root.Data,XVFS_DISK_READ);
 xvfsmount Mount=V&&Disk?xrtVfsDiskMount(V,"/app",0,XVFS_CASE_SENSITIVE,Disk,0):NULL;
 xs_tcc_config Config;xsTccConfigInit(&Config);Config.ApplicationVfs=V;Config.ApplicationRoot=Root.Data;
 xerror* Error=NULL;TCCState* T=Mount?xsCreateTCCEx(&Config,&Error):NULL;
 bool Compiled=false,Relocated=false;
 if(T){tcc_set_error_func(T,NULL,QaError);tcc_set_options(T,"-std=c11");tcc_add_include_path(T,"/app/generated");
  Compiled=tcc_compile_string(T,"#include \"/app/generated/mdo_unity.c\"\n")==0;
  if(Compiled)Relocated=tcc_relocate(T)==0;
 }
 *R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("compiled"),xrtValueBool(Compiled));xrtValueObjectSetNew(*R,XRT_STR_LITERAL("relocated"),xrtValueBool(Relocated));
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("diagnostics"),xrtValueString(xrtStrView(QaLog)));
 if(Error)xrtValueObjectSetNew(*R,XRT_STR_LITERAL("creation_error"),xrtValueString(xrtStrView(xrtErrorMessage(Error))));
 xrtErrorFree(Error);if(T)xsDestroyTCC(T);if(Mount)xrtVfsMountDestroy(Mount);if(Disk)xrtVfsDiskDestroy(Disk);if(V)xrtVfsDestroy(V);
 return MDO_RESULT_OK;
}
'''
    out = ROOT / '.build/extension-directory-native'; out.mkdir(parents=True, exist_ok=True)
    # The shared runner wraps the argument into argv exactly once.
    result = run(args, ident, source, str(ROOT / 'app'), out / 'product-compile.json')
    assert result['compiled'] and result['relocated'], result
    print(json.dumps(result))


if __name__ == '__main__':
    main()
