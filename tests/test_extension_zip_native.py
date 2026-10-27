"""Exercise the shared C ZIP codec in an existing XS/TCC product host.

No copied runtime, provider request or executed archive payload. Every temporary
command is hash-checked before removal. ZIP input/output lives in .build only.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import time
import urllib.request
import urllib.error
import uuid
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def headers():
    folder = ROOT / 'app/include/mdo'
    def inline(name):
        text = (folder / name).read_text(encoding='utf-8')
        for child in ('extension_zip.h', 'extension_path.h', 'extension_casefold.h', 'extension_identity.h'):
            if '#include "' + child + '"' in text:
                text = text.replace('#include "' + child + '"', inline(child))
        return text
    return inline('extension_manifest.h')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--home', type=Path, required=True)
    parser.add_argument('--project', default='default')
    args = parser.parse_args()
    out = ROOT / '.build/extension-zip-native'
    out.mkdir(parents=True, exist_ok=True)
    cases = []
    def fixture(name, files, accepted=True, method=zipfile.ZIP_DEFLATED):
        buffer = io.BytesIO()
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            with zipfile.ZipFile(buffer, 'w', compression=method) as z:
                for file, content, mode in files:
                    info = zipfile.ZipInfo(file)
                    info.compress_type = method
                    info.external_attr = mode << 16
                    z.writestr(info, content)
        data = buffer.getvalue()
        path = out / (name + '.zip')
        path.write_bytes(data)
        cases.append({'name': name, 'path': str(path), 'expected': accepted})
        return path

    regular = 0o100644
    fixture('standard-deflate', [('extension.json', b'{"kind":"mcp"}', regular), ('.venv/empty', b'', regular), ('empty/', b'', 0o40755), ('bin/server', b'abc' * 100000, 0o100755)])
    fixture('standard-store', [('extension.json', b'{}', regular)], method=zipfile.ZIP_STORED)
    fixture('utf8', [('文档/说明.md', '内容'.encode(), regular)])
    fixture('many-files', [(f'lib/{i}.txt', b'', regular) for i in range(1200)])
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        with z.open('lib/force64.bin', 'w', force_zip64=True) as f:
            f.write(b'zip64-local-extra')
    path = out / 'zip64-local-extra.zip'; path.write_bytes(buffer.getvalue())
    cases.append({'name': 'zip64-local-extra', 'path': str(path), 'expected': True})
    for name, files in [
        ('traversal', [('../outside', b'x', regular)]),
        ('absolute', [('/outside', b'x', regular)]),
        ('backslash', [('bin\\outside', b'x', regular)]),
        ('reserved-device', [('aux.txt', b'x', regular)]),
        ('reserved-console', [('CONOUT$', b'x', regular)]),
        ('reserved-superscript', [('COM¹.txt', b'x', regular)]),
        ('duplicate', [('x', b'a', regular), ('x', b'b', regular)]),
        ('ascii-alias', [('A', b'a', regular), ('a', b'b', regular)]),
        ('unicode-alias', [('Ä.exe', b'a', regular), ('ä.exe', b'b', regular)]),
        ('prefix-collision', [('bin', b'a', regular), ('bin/x', b'b', regular)]),
        ('prefix-with-interloper', [('a', b'a', regular), ('a-b', b'b', regular), ('a/x', b'x', regular)]),
        ('implicit-parent-alias', [('Ä/a', b'a', regular), ('ä/b', b'b', regular)]),
        ('explicit-parent-alias', [('Ä/', b'', 0o40755), ('ä/a', b'a', regular)]),
        ('explicit-parent-interloper', [('Ä/', b'', 0o40755), ('ä-b', b'b', regular), ('ä/a', b'a', regular)]),
        ('dotless-i-alias', [('I', b'a', regular), ('ı', b'b', regular)]),
        ('symlink', [('link', b'../outside', 0o120777)]),
        ('directory-mismatch', [('bin', b'', 0o40755)]),
    ]:
        fixture(name, files, False)
    # ZipInfo normalizes os.sep on Windows; mutate both stored names to test
    # an actual backslash archive rather than its normalized Python output.
    path = out / 'backslash.zip'
    path.write_bytes(path.read_bytes().replace(b'bin/outside', b'bin\\outside'))
    path = fixture('corrupt-crc', [('x', b'abcdef', regular)], method=zipfile.ZIP_STORED)
    data = bytearray(path.read_bytes()); data[31] ^= 0x20; path.write_bytes(data); cases[-1]['expected'] = False
    path = fixture('local-name-mismatch', [('x', b'abc', regular)])
    data = bytearray(path.read_bytes()); data[30] = ord('y'); path.write_bytes(data); cases[-1]['expected'] = False
    path = fixture('trailing-data', [('x', b'abc', regular)])
    path.write_bytes(path.read_bytes() + b'not part of zip'); cases[-1]['expected'] = False
    path = fixture('truncated', [('x', b'abc', regular)])
    path.write_bytes(path.read_bytes()[:-1]); cases[-1]['expected'] = False
    path = fixture('encrypted-flag', [('x', b'abc', regular)])
    data = bytearray(path.read_bytes()); data[6] |= 1; at = data.index(b'PK\x01\x02'); data[at + 8] |= 1; path.write_bytes(data); cases[-1]['expected'] = False
    path = fixture('oversized-declaration', [('x', b'abc', regular)])
    data = bytearray(path.read_bytes()); at = data.index(b'PK\x01\x02'); struct.pack_into('<I', data, at + 24, 0x70000000); path.write_bytes(data); cases[-1]['expected'] = False
    entries = {'agents': 'agent.md', 'subagents': 'agent.c', 'tools': 'tool.c', 'mcp': 'mcp.json', 'skills': 'SKILL.md', 'commands': 'command.c', 'external-tools': 'bin/qa', 'knowledge': 'knowledge.json', 'plugins': 'plugin.json'}
    for kind, entry in entries.items():
        manifest = {'kind': kind, 'name': 'QA extension', 'alias': 'qa-name', 'entry': entry, 'platforms': ['any']}
        fixture('manifest-' + kind, [('extension.json', json.dumps(manifest).encode(), regular), (entry, b'not executed', regular)])
        cases[-1]['describe'] = True
    def manifest_fixture(name, changes, extra=()):
        m = {'kind': 'mcp', 'name': 'QA', 'alias': 'qa', 'entry': 'mcp.json', 'platforms': ['any']}
        m.update(changes)
        fixture(name, [('extension.json', json.dumps(m).encode(), regular), ('mcp.json', b'{}', regular), *extra], False)
        cases[-1]['describe'] = True
    manifest_fixture('manifest-missing-entry', {'entry': 'missing.c'})
    manifest_fixture('manifest-old-envelope', {'resources': []})
    manifest_fixture('manifest-old-type', {'kind': 'toolpacks'})
    manifest_fixture('manifest-missing-native', {'native': 'missing.c'})
    manifest_fixture('manifest-self-entry', {'entry': 'extension.json'})
    manifest_fixture('manifest-invalid-alias', {'alias': '../escape'})
    fixture('manifest-wrapper', [('payload/extension.json', b'{}', regular)], False); cases[-1]['describe'] = True
    binary = bytes(range(256)) * 1025
    source_file = out / 'source.bin'; source_file.write_bytes(binary)
    written = out / 'native-written.zip'
    ident = 'zip-qa-' + uuid.uuid4().hex[:12]
    meta = {'id': ident, 'description': 'Native ZIP contract regression', 'effects': ['read', 'workspace-write'], 'requires_session': False, 'timeout_ms': 60000}
    source = '/* mdo-command ' + json.dumps(meta) + ' */\n#include "mdo/command.h"\n#include <xsbase.h>\n' + headers() + r'''
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 (void)C;const xvalue* Request=xrtValueArrayGet(A,0);const xvalue* Cases=xrtValueObjectGet(Request,XRT_STR_LITERAL("cases"));*R=xrtValueObject();xvalue* Rows=xrtValueArray();
 bool Contract=true;for(size_t K=0;K<9;K++)Contract=Contract&&MdoExtensionDirectoryKind(MdoExtensionKinds[K]);
 char* Root=MdoExtensionDirectoryPath("mcp","V--u87hExb-w2R9P--SuTetSA3Hd0dq7");
 Contract=Contract&&Root&&!strcmp(Root,"mcp/V--u87hExb-w2R9P--SuTetSA3Hd0dq7")&&!MdoExtensionDirectoryKind("toolpacks")&&!MdoExtensionDirectoryId("../outside");xrtFree(Root);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("directory_contract"),xrtValueBool(Contract));
 for(size_t I=0;I<xrtValueCount(Cases);I++){
  const xvalue* Item=xrtValueArrayGet(Cases,I);xstrview Path={0};xrtValueGetString(xrtValueObjectGet(Item,XRT_STR_LITERAL("path")),&Path);
  xfile F=xrtOpen(Path.Data,XFILE_READ);MdoZip Z={0};bool Ok=MdoZipOpen(&Z,F);if(Ok)Ok=MdoZipValidate(&Z,NULL);
  bool Describe=false;xrtValueGetBool(xrtValueObjectGet(Item,XRT_STR_LITERAL("describe")),&Describe);
  if(Ok&&Describe){xvalue* Manifest=MdoExtensionZipManifest(&Z,NULL);Ok=Manifest!=NULL;xrtValueRelease(Manifest);}
  xvalue* Row=xrtValueObject();xrtValueObjectSet(Row,XRT_STR_LITERAL("name"),xrtValueObjectGet(Item,XRT_STR_LITERAL("name")));
  xrtValueObjectSetNew(Row,XRT_STR_LITERAL("accepted"),xrtValueBool(Ok));xrtValueObjectSetNew(Row,XRT_STR_LITERAL("error"),xrtValueString(xrtStrView(Z.Error)));xrtValueArrayAppendNew(Rows,Row);MdoZipClose(&Z);xrtClose(F);
 }
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("cases"),Rows);xstrview From={0},To={0};
 xrtValueGetString(xrtValueObjectGet(Request,XRT_STR_LITERAL("writer_input")),&From);xrtValueGetString(xrtValueObjectGet(Request,XRT_STR_LITERAL("writer_output")),&To);
 xfile Input=xrtOpen(From.Data,XFILE_READ),Output=xrtOpen(To.Data,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);MdoZipWriter W;MdoZipWriterInit(&W,Output);
 bool Ok=Input&&Output&&MdoZipWriterAdd(&W,"lib/数据.bin",false,0755,Input,NULL)&&MdoZipWriterAdd(&W,"empty",true,0755,NULL,NULL)&&MdoZipWriterFinish(&W);
    xrtValueObjectSetNew(*R,XRT_STR_LITERAL("written"),xrtValueBool(Ok));xrtValueObjectSetNew(*R,XRT_STR_LITERAL("writer_error"),xrtValueString(xrtStrView(W.Index.Error)));
 MdoZipWriterUnit(&W);xrtClose(Input);xrtClose(Output);
 xfile Written=xrtOpen(To.Data,XFILE_READ);MdoZip Back={0};bool Readable=MdoZipOpen(&Back,Written)&&MdoZipValidate(&Back,NULL);MdoZipClose(&Back);xrtClose(Written);
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("native_roundtrip"),xrtValueBool(Readable));return MDO_RESULT_OK;
}
'''
    command = args.home / 'commands' / (ident + '.c')
    command.parent.mkdir(parents=True, exist_ok=True)
    encoded = source.encode('utf-8'); command.write_bytes(encoded)
    def request(method, path, value=None):
        headers_ = {}
        if method == 'POST':
            with urllib.request.urlopen(args.url + '/api/v1/bootstrap', timeout=15) as r:
                headers_['X-Mdo-Write-Token'] = r.headers.get('X-Mdo-Write-Token', '')
            headers_['Content-Type'] = 'application/json'
        req = urllib.request.Request(args.url + path, data=json.dumps(value).encode() if value is not None else None, method=method, headers=headers_)
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                return json.load(r)
        except urllib.error.HTTPError as error:
            raise AssertionError((error.code, error.read().decode('utf-8'))) from error
    def call(method, params):
        data = request('POST', '/api/v1/application/call', {'method': method, 'params': params, 'context': {'project': args.project}})
        return data['data']
    try:
        invocation = 'cmd-' + uuid.uuid4().hex
        body = {'invocation_id': invocation, 'command_id': ident, 'raw_arguments': json.dumps([{'cases': cases, 'writer_input': str(source_file), 'writer_output': str(written)}], ensure_ascii=False)}
        call('product.command_invocations.post', {'query': {'project': args.project}, 'body': body})
        for _ in range(300):
            status = call('product.command_invocations.by_invocation.get', {'path': {'invocation': invocation}})['data']
            if status.get('terminal'):
                break
            time.sleep(.2)
        (out / 'native-result.json').write_text(json.dumps(status, ensure_ascii=False, indent=2), encoding='utf-8')
        assert status.get('state') == 'succeeded', status
        result = status['result']
        assert result['directory_contract'], result
        by_name = {r['name']: r for r in result['cases']}
        for case in cases:
            assert by_name[case['name']]['accepted'] == case['expected'], (case, by_name[case['name']])
        assert result['written'], result
        assert result['native_roundtrip'], result
        with zipfile.ZipFile(written) as z:
            assert z.testzip() is None
            assert z.read('lib/数据.bin') == binary
            assert z.getinfo('empty/').is_dir()
            assert z.getinfo('lib/数据.bin').external_attr >> 16 & 0o777 == 0o755
        print(json.dumps({'cases': len(cases), 'writer_roundtrip': True, 'native_command_bytes': len(encoded)}))
    finally:
        if command.exists() and hashlib.sha256(command.read_bytes()).digest() == hashlib.sha256(encoded).digest():
            command.unlink()


if __name__ == '__main__':
    main()
