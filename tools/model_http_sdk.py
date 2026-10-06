"""Refresh the public xhttp declarations absent from the current xs TCC SDK.

Only declarations are bundled, never a second implementation. The source must
be the exact xs checkout recorded in deps.lock; regenerate on SDK upgrades.
"""
from pathlib import Path
import argparse
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
HEADERS = {'http_headers.h', 'http_body.h', 'http_client.h', 'cookie.h',
           'cookie_jar.h', 'http_client_runtime.h', 'http_client_future.h',
           'http_client_easy.h', 'http_cache_store.h', 'url.h', 'mime.h',
           'http_exchange.h', 'http_client_stream.h'}


def generate(sdk: Path) -> str:
    revision = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
    assert revision == json.loads((ROOT / 'deps.lock').read_text())['xserver']['commit'], 'xs revision mismatch'
    source = (sdk / 'lib/xhttp/single/xhttp.h').read_text(encoding='utf-8')
    sections = re.split(r'/\* ={10,} \*/\n(/\* [^\n]+ \*/)\n/\* ={10,} \*/', source)
    pairs = list(zip(sections[1::2], sections[2::2]))
    features = next(body for name, body in pairs if 'feature selection: extlibs/xhttp/' in name)
    public = ''.join(name + '\n' + body for name, body in pairs
                     if 'public: extlibs/xhttp/' in name and name.removesuffix(' */').rsplit('/', 1)[-1] in HEADERS)
    # xrt_decl provides the full base declarations, then restores feature flags.
    # Restore only declaration dependency guards needed by these public headers.
    dependencies = sorted(set(re.findall(r'!defined\((XRT_FEATURE_\w+)\)', public)))
    modules = '\n'.join(line for line in (sdk / 'lib/xwork/xwork-xrt-roots.h').read_text().splitlines()
                        if line.startswith('#define XHTTP_MODULE_'))
    license = source[:source.index('/* 此文件')]
    return (license + '/* Generated public SDK bridge. Source: xs ' + revision + '.\n'
            ' * Regenerate with tools/model_http_sdk.py; contains no implementation. */\n'
            '#ifndef MDO_MODEL_HTTP_SDK_H\n#define MDO_MODEL_HTTP_SDK_H\n#include <xsbase.h>\n' +
            modules + '\n#define XHTTP_SINGLE_HEADER 1\n' +
            ''.join('#ifndef ' + flag + '\n#define ' + flag + '\n#endif\n' for flag in dependencies) +
            features + public + '\n#endif\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    target = ROOT / 'app/include/mdo/model_http_sdk.h'
    text = generate(args.sdk)
    if args.check:
        assert target.read_text(encoding='utf-8') == text, 'Regenerate the xhttp SDK bridge'
    else:
        target.write_text(text, encoding='utf-8', newline='\n')
