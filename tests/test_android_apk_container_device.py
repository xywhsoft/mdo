"""Test APK signing-block alignment on an independent ARM64 QA installation."""
import argparse
import base64
import io
import json
from pathlib import Path
import struct
import time
import uuid
import zipfile

from android_release_client import AndroidReleaseClient


def codec_headers():
    folder = Path(__file__).resolve().parents[1] / 'app/include/mdo'
    def inline(name):
        text = (folder / name).read_text(encoding='utf-8')
        for child in ('extension_path.h', 'extension_casefold.h'):
            directive = '#include "' + child + '"'
            if directive in text:
                text = text.replace(directive, inline(child))
        return text
    return inline('extension_zip.h')


def fixtures():
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w') as archive:
        archive.writestr('assets/mdo-runtime.json', '{}')
    original = stream.getvalue()
    central = struct.unpack_from('<I', original, len(original) - 6)[0]
    def signed(padding=b'', footer=24, header=24):
        block = struct.pack('<Q', header) + struct.pack('<Q', footer) + b'APK Sig Block 42'
        data = bytearray(original[:central] + padding + block + original[central:])
        struct.pack_into('<I', data, len(data) - 6, central + len(padding) + len(block))
        return bytes(data)
    aligned = (-central) % 4096 or 4096
    assert 0 < aligned < 4096
    return [
        ('ordinary-zip', original, False, True),
        ('signed-apk', signed(), True, True),
        ('aligned-apk', signed(b'\0' * aligned), True, True),
        ('signed-extension-rejected', signed(), False, False),
        ('aligned-extension-rejected', signed(b'\0' * aligned), False, False),
        ('nonzero-padding', signed(b'x' + b'\0' * (aligned - 1)), True, False),
        ('excessive-padding', signed(b'\0' * (aligned + 4096)), True, False),
        ('unaligned-padding', signed(b'\0'), True, False),
        ('undersized-block', signed(footer=0), True, False),
        ('oversized-block', signed(footer=2**64 - 1), True, False),
        ('mismatched-block-size', signed(header=25), True, False),
    ]


SOURCE = r'''
mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 const xvalue* Cases=xrtValueArrayGet(A,0);*R=xrtValueArray();
 for(size_t I=0;I<xrtValueCount(Cases);I++){
  const xvalue* Case=xrtValueArrayGet(Cases,I);xstrview Path={0};bool Apk=false;
  xrtValueGetString(xrtValueObjectGet(Case,XRT_STR_LITERAL("path")),&Path);
  xrtValueGetBool(xrtValueObjectGet(Case,XRT_STR_LITERAL("apk")),&Apk);
  char* Installed=Path.Size?NULL:xrtEnvGet("MDO_APK_PATH");
  xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ;
  xfile F=xrtFileOpen(Installed?Installed:Path.Data,&O);MdoZip Z={0};
  bool Ok=F&&MdoZipOpenContainer(&Z,F,Apk);xvalue* Row=xrtValueObject();
  xrtValueObjectSet(Row,XRT_STR_LITERAL("name"),xrtValueObjectGet(Case,XRT_STR_LITERAL("name")));
  xrtValueObjectSetNew(Row,XRT_STR_LITERAL("accepted"),xrtValueBool(Ok));
  xrtValueObjectSetNew(Row,XRT_STR_LITERAL("error"),xrtValueString(xrtStrView(Z.Error)));
  xrtValueArrayAppendNew(*R,Row);MdoZipClose(&Z);xrtClose(F);xrtFree(Installed);
 }return MDO_RESULT_OK;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    client = AndroidReleaseClient(args.adb, args.serial, args.package)
    alias = 'qa-apk-' + uuid.uuid4().hex[:12]
    route = '/api/v1/extensions/commands/' + alias
    created = None
    paths = []
    try:
        client.launch()
        cases = []
        for name, data, apk, expected in fixtures():
            path = client.home + '/workspace/' + alias + '-' + name + '.zip'
            client.call('fs.write', dict(path=path, base64=base64.b64encode(data).decode()))
            paths.append(path)
            cases.append(dict(name=name, path=path, apk=apk, expected=expected))
        cases.append(dict(name='installed-signed-apk', path='', apk=True, expected=True))
        meta = dict(id=alias, description='APK codec acceptance', effects=['read'], requires_session=False, timeout_ms=15000)
        source = '/* mdo-command ' + json.dumps(meta) + ' */\n#include "mdo/command.h"\n' + codec_headers() + SOURCE
        manifest = dict(kind='commands', id='command:qa.'+alias, name=alias, alias=alias, entry='command.c', platforms=['any'])
        client.data('PUT', route, dict(manifest=manifest, expected_revision='', edits=[dict(path='command.c', text=source)], trust_code=True))
        created = client.data('GET', route)['revision']
        client.data('POST', route+'/enabled', dict(enabled=True))
        invocation = 'cmd-' + uuid.uuid4().hex
        client.data('POST', '/api/v1/command-invocations?project=default', dict(invocation_id=invocation, command_id=alias, raw_arguments=json.dumps([cases])))
        for _ in range(200):
            result = client.data('GET', '/api/v1/command-invocations/'+invocation)
            if result['terminal']:
                break
            time.sleep(.1)
        args.evidence.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        assert result['state'] == 'succeeded', result
        by_name = {row['name']: row for row in result['result']}
        for case in cases:
            assert by_name[case['name']]['accepted'] == case['expected'], (case, by_name[case['name']])
        print(json.dumps(dict(passed=True, cases=len(cases), package=args.package)))
    finally:
        if created and client.data('GET', route)['revision'] == created:
            client.data('DELETE', route, dict(expected_revision=created))
        for path in paths:
            client.call('fs.remove', dict(path=path))
        client.close()


if __name__ == '__main__':
    main()
