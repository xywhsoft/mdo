"""Run an owned C contract command in an existing product, without cloning it."""
import hashlib
import json
from pathlib import Path
import shutil
import time
import urllib.error
import urllib.request
import uuid


def run(args, ident, source, argument, evidence):
    # The currently running acceptance build limits command entries to 128 KiB.
    # Compile larger contracts from a small owned source fixture, without
    # changing that build, creating a service or copying any runtime files.
    fixture = None
    if len(source.encode('utf-8')) > 120000:
        root = Path(__file__).resolve().parents[1]
        fixture = root / '.build/native-contracts' / uuid.uuid4().hex
        fixture.mkdir(parents=True)
        (fixture / 'contract.c').write_text(source, encoding='utf-8')
        source = source.splitlines()[0] + '\n' + r'''
#include "mdo/command.h"
#include <xsbase.h>
#include <libtcc.h>
static char QaErrors[8192];static void QaCompileError(void* U,cstr S){(void)U;size_t N=strlen(QaErrors);if(N<sizeof(QaErrors)-2)snprintf(QaErrors+N,sizeof(QaErrors)-N,"%s\n",S);}
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 xvfs V=xrtVfsCreate();xvfsdisk App=xrtVfsDiskCreate(QA_APP,XVFS_DISK_READ),Fixture=xrtVfsDiskCreate(QA_FIXTURE,XVFS_DISK_READ);
 xvfsmount M=V&&App?xrtVfsDiskMount(V,"/app",0,XVFS_CASE_SENSITIVE,App,0):NULL,F=V&&Fixture?xrtVfsDiskMount(V,"/app/qa-contract",10,XVFS_CASE_SENSITIVE,Fixture,0):NULL;
 xs_tcc_config Config;xsTccConfigInit(&Config);Config.ApplicationVfs=V;Config.ApplicationRoot=QA_APP;
 TCCState* T=M&&F?xsCreateTCCEx(&Config,NULL):NULL;union{void* P;mdo_result(*F)(const mdo_command_context_v1*,const xvalue*,xvalue**);} Entry={0};
 if(T){tcc_set_error_func(T,NULL,QaCompileError);tcc_add_include_path(T,"/app/generated/module-sdk");if(tcc_compile_string(T,"#include \"/app/qa-contract/contract.c\"\n")==0&&tcc_relocate(T)==0)Entry.P=tcc_get_symbol(T,"mdoCommandRun");}
 mdo_result Code=MDO_RESULT_OK;if(Entry.F)Code=Entry.F(C,A,R);else{*R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("compile_error"),xrtValueString(xrtStrView(QaErrors)));}
 if(T)xsDestroyTCC(T);if(F)xrtVfsMountDestroy(F);if(M)xrtVfsMountDestroy(M);if(Fixture)xrtVfsDiskDestroy(Fixture);if(App)xrtVfsDiskDestroy(App);if(V)xrtVfsDestroy(V);return Code;
}
'''.replace('QA_APP', json.dumps((root / 'app').as_posix())).replace('QA_FIXTURE', json.dumps(fixture.as_posix()))
    encoded = source.encode('utf-8')
    def request(method, path, value=None):
        headers = {}
        if method == 'POST':
            with urllib.request.urlopen(args.url + '/api/v1/bootstrap', timeout=15) as r:
                headers['X-Mdo-Write-Token'] = r.headers.get('X-Mdo-Write-Token', '')
            headers['Content-Type'] = 'application/json'
        req = urllib.request.Request(args.url + path, data=json.dumps(value).encode() if value is not None else None, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                return json.load(r)
        except urllib.error.HTTPError as error:
            raise AssertionError((error.code, error.read().decode('utf-8'))) from error
    def call(method, params):
        return request('POST', '/api/v1/application/call', {'method': method, 'params': params, 'context': {'project': args.project}})['data']
    # Current products only discover complete Home directories. The older
    # existing acceptance host is still usable while testing a source upgrade.
    try:
        with urllib.request.urlopen(args.url + '/api/v1/extensions/commands',timeout=5) as response:
            directory_mode = response.status == 200
    except urllib.error.HTTPError as error:
        if error.code != 404: raise
        directory_mode = False
    directory = args.home / 'commands' / ident if directory_mode else None
    command = directory / 'command.c' if directory else args.home / 'commands' / (ident + '.c')
    if command.exists() or (directory and directory.exists()):
        raise AssertionError('Refusing to replace an existing command')
    command.parent.mkdir(parents=True,exist_ok=True)
    manifest = json.dumps(dict(kind='commands',name=ident,alias=ident,entry='command.c',platforms=['any'])).encode()
    if directory: (directory / 'extension.json').write_bytes(manifest)
    command.write_bytes(encoded)
    try:
        invocation = 'cmd-' + uuid.uuid4().hex
        body = {'invocation_id': invocation, 'command_id': ident, 'raw_arguments': json.dumps([argument], ensure_ascii=False)}
        call('product.command_invocations.post', {'query': {'project': args.project}, 'body': body})
        for _ in range(300):
            status = call('product.command_invocations.by_invocation.get', {'path': {'invocation': invocation}})['data']
            if status.get('terminal'):
                break
            time.sleep(.2)
        evidence.write_text(json.dumps(status, ensure_ascii=False, indent=2), encoding='utf-8')
        assert status.get('state') == 'succeeded', status
        return status['result']
    finally:
        if command.exists() and hashlib.sha256(command.read_bytes()).digest() == hashlib.sha256(encoded).digest():
            command.unlink()
            if directory and (directory / 'extension.json').read_bytes() == manifest:
                (directory / 'extension.json').unlink()
                directory.rmdir()
        if fixture and fixture.parent == root / '.build/native-contracts':
            shutil.rmtree(fixture)
