"""Actual bundled MCP program, independent private connections, and ZIP export."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import uuid
import zipfile
from native_product_runner import product
from mcp_template_fixture import template

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static bool QaMcpConnection(cstr Id,cstr Marker,cstr Digest,bool Enabled){
    xvalue* C=xrtValueObject(),*Values=xrtValueObject();
    bool Ok=C&&Values&&SdkPut(C,"id",Id)&&SdkPut(C,"extension_id","0123456789ABCDEFGHJKMNPQRSTVWXYZ")&&SdkPut(C,"variant","default")&&
        SdkPut(C,"name",Id)&&SdkPut(C,"approval_digest",Digest)&&SdkPut(Values,"marker",Marker)&&SdkPut(Values,"token","env:MDO_QA_MCP_TOKEN")&&
        xrtValueObjectSetNew(C,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled))&&xrtValueObjectSet(C,XRT_STR_LITERAL("values"),Values);
    char* Path=xrtFormat("data/mcp/connections/%s.json",Id),*Text=Ok?xrtJsonStringify(C,false,NULL):NULL;
    Ok=Text&&Path&&MdoHomeAtomicWrite(Path,Text,strlen(Text),false);xrtFree(Path);xrtFree(Text);xrtValueRelease(C);xrtValueRelease(Values);return Ok;
}
static bool QaMcpMarker(cstr Id,cstr Expected){
    char* Result=NULL;xwork_error Error;xworkErrorInit(&Error);bool Ok=MdoMcpRead(Id,"resources/read","{\"uri\":\"fixture://marker\"}",NULL,MdoDeadlineAfterUs(10000000),&Result,&Error);
    Ok=Ok&&Result&&strstr(Result,Expected);xrtFree(Result);return Ok;
}
xvalue* QaProductContract(const xvalue* P,xvfs V){
    cstr Root=SdkField(P,"root"),App=SdkField(P,"app");xvalue* R=xrtValueObject();char Error[512]={0},Digest[65]={0};
    bool Home=QaProductHome(Root,App,V),Ok=Home&&MdoExtensionsInit()&&MdoExtensionsFingerprint("mcp","0123456789ABCDEFGHJKMNPQRSTVWXYZ",Digest,NULL,Error);
    Ok=Ok&&xrtEnvSet("MDO_QA_MCP_TOKEN","private-fixture-token")&&QaMcpConnection("qa-one","alpha-private-only",Digest,true)&&QaMcpConnection("qa-two","beta-private-only",Digest,true);
    xwork_runtime_config Config;xworkRuntimeConfigInit(&Config);xwork_error Work;xworkErrorInit(&Work);xwork_runtime* Runtime=Ok?xworkRuntimeCreate(&Config,&Work):NULL;
    Ok=Ok&&Runtime&&MdoMcpManagerInit(Runtime);xrtValueObjectSetNew(R,XRT_STR_LITERAL("loaded"),xrtValueBool(Ok));
    if(!Ok&&xrtGetError())SdkPut(R,"load_error",xrtErrorMessage(xrtGetError()));
    MdoMcpCatalog* C=Ok?MdoMcpCatalogSnapshot():NULL;bool Two=C&&C->Count==2;
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("two_private_connections"),xrtValueBool(Two));MdoMcpCatalogRelease(C);
    bool First=Two&&QaMcpMarker("qa-one","alpha-private-only"),Second=Two&&QaMcpMarker("qa-two","beta-private-only");
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("first_real_protocol"),xrtValueBool(First));xrtValueObjectSetNew(R,XRT_STR_LITERAL("second_real_protocol"),xrtValueBool(Second));
    if((!First||!Second)&&xrtGetError())SdkPut(R,"protocol_error",xrtErrorMessage(xrtGetError()));
    char ArchiveHash[65];uint64 Bytes=0;bool Packed=Ok&&MdoExtensionsPack("mcp","0123456789ABCDEFGHJKMNPQRSTVWXYZ","data/extension-tasks/export/mcp.zip",NULL,ArchiveHash,&Bytes,Error);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("zip_packed"),xrtValueBool(Packed));
    xvalue* Removed=NULL;bool Pinned=Ok&&!MdoExtensionsRemove("mcp","0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Removed,Error);xrtValueRelease(Removed);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("running_process_pins_directory"),xrtValueBool(Pinned));
    bool Changed=Ok&&MdoHomeAtomicWrite("mcp/0123456789ABCDEFGHJKMNPQRSTVWXYZ/support/banner.txt","updated support",15,false)&&MdoMcpManagerReload();
    C=Changed?MdoMcpCatalogSnapshot():NULL;Changed=Changed&&C&&C->Count==0;MdoMcpCatalogRelease(C);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("changed_directory_requires_approval"),xrtValueBool(Changed));
    bool Reapproved=Changed&&MdoExtensionsFingerprint("mcp","0123456789ABCDEFGHJKMNPQRSTVWXYZ",Digest,NULL,Error)&&
        QaMcpConnection("qa-one","alpha-private-only",Digest,true)&&QaMcpConnection("qa-two","beta-private-only",Digest,true)&&MdoMcpManagerReload()&&QaMcpMarker("qa-one","alpha-private-only");
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("approval_and_reconnect"),xrtValueBool(Reapproved));
    MdoMcpDiagnostics* D=MdoMcpDiagnosticsSnapshot();xvalue* Messages=xrtValueArray();for(size_t I=0;D&&I<D->Count;I++)xrtValueArrayAppendNew(Messages,xrtValueString(xrtStrView(D->Entries[I].Message)));
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("diagnostics"),Messages);MdoMcpDiagnosticsRelease(D);
    MdoMcpManagerUnit();if(Runtime)xworkRuntimeRelease(Runtime);Removed=NULL;
    bool Gone=MdoExtensionsRemove("mcp","0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Removed,Error);xrtValueRelease(Removed);xrtValueObjectSetNew(R,XRT_STR_LITERAL("whole_directory_uninstalled"),xrtValueBool(Gone));
    bool Exists=false;xfileinfo Info;bool Private=MdoHomeExternalStat("data/mcp/connections/qa-one.json",&Exists,&Info)&&Exists;
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("private_state_separate"),xrtValueBool(Private));SdkPut(R,"error",Error);
    xrtEnvSet("MDO_QA_MCP_TOKEN",NULL);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True); parser.add_argument('--home', type=Path, required=True)
    parser.add_argument('--project', default='default'); parser.add_argument('--cc', type=Path, default=Path('E:/software/w64devkit/bin/gcc.exe'))
    args = parser.parse_args(); out = ROOT / '.build/mcp-directory-native'; out.mkdir(parents=True, exist_ok=True)
    fixture = out / ('home-' + uuid.uuid4().hex[:12]); fixture.mkdir()
    directory = fixture / 'mcp/0123456789ABCDEFGHJKMNPQRSTVWXYZ'; (directory / 'bin').mkdir(parents=True); (directory / 'support').mkdir()
    shutil.copyfile(ROOT / 'tests/fixtures/mcp-directory-server.c', directory / 'server.c')
    subprocess.run([str(args.cc), '-Os', '-s', '-Wall', '-Wextra', str(directory / 'server.c'), '-o', str(directory / 'bin/server.exe')], check=True, capture_output=True)
    (directory / 'support/banner.txt').write_text('directory support', encoding='utf-8')
    (directory / 'extension.json').write_text(json.dumps({'kind': 'mcp', 'name': 'Bundled MCP', 'alias': 'bundled-fixture', 'entry': 'mcp.json', 'platforms': ['windows-x86_64']}), encoding='utf-8')
    recipe = template('bundled-fixture', 'stdio')
    recipe['inputs'] = [dict(id='marker', type='text', label='Marker', description='Private value', required=True, secret=False),
                        dict(id='token', type='text', label='Token', description='Private credential', required=True, secret=True)]
    recipe['variants'][0]['config']['transport'] = dict(type='stdio', program='bin/server.exe', arguments=[dict(input='marker')], environment=[dict(name='QA_PRIVATE_TOKEN', secret_ref='input:token')], inherit_environment=False)
    (directory / 'mcp.json').write_text(json.dumps(recipe), encoding='utf-8')
    try:
        result = product(args, fixture, CONTRACT, out / 'result.json')
        for key in ('loaded', 'two_private_connections', 'first_real_protocol', 'second_real_protocol', 'zip_packed', 'running_process_pins_directory', 'changed_directory_requires_approval', 'approval_and_reconnect', 'whole_directory_uninstalled', 'private_state_separate'):
            assert result.get(key), (key, result)
        archive = fixture / 'data/extension-tasks/export/mcp.zip'
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None and z.read('bin/server.exe').startswith(b'MZ')
            assert set(z.namelist()) == {'extension.json', 'mcp.json', 'bin/', 'bin/server.exe', 'support/', 'support/banner.txt', 'server.c'}
            for name in z.namelist():
                data = z.read(name)
                assert b'alpha-private-only' not in data and b'private-fixture-token' not in data and b'env:MDO_QA_MCP_TOKEN' not in data, name
        shutil.copyfile(archive, out / 'exported-mcp.zip')
        print(json.dumps(result, ensure_ascii=False))
    finally:
        if fixture.resolve().parent == out.resolve() and fixture.name.startswith('home-'):
            shutil.rmtree(fixture)


if __name__ == '__main__': main()
