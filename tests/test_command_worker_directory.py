"""Exercise the actual command child process with directory headers/resources.

Only the normal command scratch files are created; the existing XS executable
is reused. No second application Home, model call or copied runtime is used.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    args = parser.parse_args()
    parent = ROOT / '.build/command-worker-directory'; parent.mkdir(parents=True, exist_ok=True)
    fixture = parent / ('task-' + uuid.uuid4().hex[:12]); fixture.mkdir()
    extension = fixture / 'extension'; (extension / 'nested').mkdir(parents=True)
    (extension / 'helper.h').write_text('#define QA_DIRECTORY_VALUE 37\n', encoding='utf-8')
    (extension / 'nested/adjacent.h').write_text('#define QA_ADJACENT_VALUE 11\n', encoding='utf-8')
    (extension / 'resource.txt').write_bytes(b'directory resource')
    source = r'''
#include "mdo/command.h"
#include <xsbase.h>
#include "helper.h"
#include "adjacent.h"
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 (void)A;char* File=xrtPathJoin(C->ExtensionRoot,"resource.txt");size_t N=0;
 char* Text=(char*)xrtFileReadAllLimit(File,1024,&N);xrtFree(File);if(!Text)return MDO_RESULT_ERROR;
 *R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("text"),xrtValueString(xrtStrViewN(Text,N)));
 xrtValueObjectSetNew(*R,XRT_STR_LITERAL("value"),xrtValueInt(QA_DIRECTORY_VALUE+QA_ADJACENT_VALUE));xrtFree(Text);return MDO_RESULT_OK;
}
'''
    (extension / 'nested/command.c').write_text(source, encoding='utf-8')
    results = []
    try:
        for changed in (False, True):
            task = fixture / ('changed' if changed else 'execute'); (task / 'mdo').mkdir(parents=True); (task / 'web').mkdir()
            (task / 'web/index.html').write_text('Command directory test', encoding='utf-8')
            shutil.copyfile(ROOT / 'app/command-worker.c', task / 'worker.c')
            for name in ('command.h', 'module.h', 'sdk.h', 'workbench.h'):
                shutil.copyfile(ROOT / 'include/mdo' / name, task / 'mdo' / name)
            (task / 'command.c').write_text(source, encoding='utf-8')
            data = {'watch_parent': False, 'extension_root': str(extension), 'extension_entry': 'nested/command.c',
                    'directory_id': '0123456789ABCDEFGHJKMNPQRSTVWXYZ', 'command_id': 'qa-alias',
                    'revision': 'a' * 64, 'arguments': [], 'raw_arguments': '', 'workspace': str(ROOT)}
            (task / 'input.json').write_text(json.dumps(data), encoding='utf-8')
            config = {'engine': {'workers': 1}, 'services': [{'enabled': True, 'class': 'custom', 'name': 'command-worker',
                      'host_default': {'enabled': True, 'path': 'web', 'devlang': 'c', 'devfile': 'worker.c'}}]}
            (task / 'xs.json').write_text(json.dumps(config), encoding='utf-8')
            process = subprocess.Popen([str(args.host), str(task / 'xs.json'), '--no-vfs'], cwd=task,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            handled = set(); deadline = time.monotonic() + 30
            try:
                while time.monotonic() < deadline and not (task / 'final.json').exists():
                    for request in task.glob('request-*.json'):
                        if request.name in handled:
                            continue
                        message = json.loads(request.read_text(encoding='utf-8'))
                        assert message['method'] == 'sdk.invoke' and message['params']['method'] == 'extension.fingerprint', message
                        response = {'ok': True, 'error': '', 'result': {'code': 0, 'result': {'sha256': ('b' if changed else 'a') * 64}, 'error': {}}}
                        dest = task / request.name.replace('request-', 'response-')
                        temp = dest.with_suffix('.tmp'); temp.write_text(json.dumps(response), encoding='utf-8'); temp.replace(dest)
                        handled.add(request.name)
                    if process.poll() is not None and not (task / 'final.json').exists():
                        raise AssertionError(('Command worker exited without result', process.returncode))
                    time.sleep(.02)
                assert (task / 'final.json').exists(), 'Command worker timed out'
                result = json.loads((task / 'final.json').read_text(encoding='utf-8'))
                if changed:
                    assert not result['ok'] and 'changed during compilation' in result['error'], result
                else:
                    assert result['ok'] and result['result'] == {'text': 'directory resource', 'value': 48}, result
                assert len(handled) == 1, handled
                results.append({'changed': changed, 'passed': True})
                process.wait(timeout=10)
            finally:
                if process.poll() is None:
                    process.kill(); process.wait(timeout=10)
        (parent / 'result.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
        print(json.dumps(results))
    finally:
        if fixture.resolve().parent == parent.resolve() and fixture.name.startswith('task-'):
            shutil.rmtree(fixture)


if __name__ == '__main__':
    main()
