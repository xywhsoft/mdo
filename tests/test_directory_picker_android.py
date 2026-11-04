"""Directory API and actual mobile WebView acceptance in an independent QA APK.

Uses the existing ADB and Node runtimes; touches only an owned directory and
project in the selected org.xleaves.mdo.qa.* Home. No accounts or model calls.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time
import uuid
from urllib.parse import quote

from android_release_client import AndroidReleaseClient


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=False)
    client = AndroidReleaseClient(args.adb, args.serial, args.package)
    identity = 'qa-directory-' + uuid.uuid4().hex[:12]
    root = None
    debug_port = None
    report = {'package': args.package, 'paid_model_calls': 0, 'checks': [], 'passed': False}

    def check(value, name):
        assert value, name
        report['checks'].append(name)
        print('PASS', name, flush=True)

    try:
        client.launch()
        root = client.home + '/workspace/' + identity + ' space + 中文'
        client.shell('run-as', client.package, 'mkdir', '-p', root + '/many')
        script = ('for i in $(/system/bin/toybox seq 1 300); do mkdir ' +
                  shlex.quote(root + '/many/node-') + '"$i" || exit; done')
        client.shell('run-as', client.package, 'sh', '-c', script)
        locations = client.data('GET', '/api/v1/workspace/locations')
        check(locations['platform'] == 'android', 'Locations retain the Android target identity')
        home = next(row['path'] for row in locations['locations'] if row['kind'] == 'home')
        check(home == client.home, 'Android home shortcut uses this app private Home')
        check(client.request('GET', '/api/v1/workspace/directories?path=' + quote(home, safe=''))[0] == 200,
              'First-open home shortcut is browsable without shared storage permission')
        endpoint = '/api/v1/workspace/directories'
        url = endpoint + '?path=' + quote(root + '/many', safe='') + '&limit=47'
        cursor = ''
        names = []
        for _ in range(8):
            page = client.data('GET', url + '&after=' + quote(cursor, safe=''))
            check(page['total'] == 300 and not page['truncated'], 'Paged Android directory reports complete total')
            names.extend(page['directories'])
            if not page['has_more']:
                break
            cursor = page['next_cursor']
        check(names == ['node-' + str(i) for i in range(1, 301)],
              'All 300 Android folders retain natural order with no missing or duplicate page entries')
        for fields, expected in [('filter=NODE-299', ['node-299']), ('prefix=node-30', ['node-30', 'node-300'])]:
            check(client.data('GET', url + '&' + fields)['directories'] == expected,
                  'Android server filtering ' + fields)
        created = client.data('POST', endpoint, {'path': root, 'name': 'API + 中文'})
        check(created['path'] == root + '/API + 中文', 'Native creation preserves spaces plus and Unicode')
        check(client.request('POST', endpoint, {'path': root, 'name': 'API + 中文'})[0] == 409,
              'Native creation never overwrites an existing folder')
        check(client.request('POST', endpoint, {'path': root, 'name': '../escape'})[0] == 400,
              'Creation rejects traversal instead of changing another directory')
        for method, params in [('workspace.locations', {}),
                               ('workspace.directories.list', {'query': {'path': root, 'limit': 1}}),
                               ('workspace.directories.create', {'body': {'path': root, 'name': 'SDK folder'}})]:
            value = client.call(method, params)
            check(value['status'] in (200, 201), 'ARM64 SDK route ' + method)

        pid = client.shell('pidof', client.package)
        socket_name = 'webview_devtools_remote_' + pid
        deadline = time.monotonic() + 8
        while socket_name not in client.shell('cat', '/proc/net/unix') and time.monotonic() < deadline:
            time.sleep(.15)
        assert socket_name in client.shell('cat', '/proc/net/unix'), 'QA WebView debugging socket missing'
        debug_port = int(client.adb('forward', 'tcp:0', 'localabstract:' + socket_name).decode().strip())
        fixture = Path(__file__).parent / 'fixtures/directory-picker-device-browser.js'
        injected = fixture.read_text(encoding='utf-8').replace('__DIRECTORY_TARGET__', json.dumps(root))
        injected = injected.replace('__DIRECTORY_ID__', json.dumps(identity))
        source = args.evidence / 'browser-input.js'
        source.write_text(injected, encoding='utf-8')
        result = subprocess.run(['node', str(Path(__file__).with_name('directory_picker_android_driver.mjs')),
                                 str(debug_port), str(client.device_port), str(source.resolve())],
                                capture_output=True, timeout=50)
        raw = result.stdout.decode('utf-8')
        assert raw, result.stderr.decode('utf-8', errors='replace')[-4000:]
        browser = json.loads(raw)
        report['browser'] = browser
        assert result.returncode == 0, browser
        check(browser['passed'], 'Actual Android WebView directory/project interactions: ' + json.dumps(browser))
        (args.evidence / 'mobile.png').write_bytes(client.adb('exec-out', 'screencap', '-p'))
        saved = client.data('GET', '/api/v1/projects/' + identity)
        check(saved['workspace_root'] == root + '/new folder 中文', 'Mobile confirmation saves the actual selected project directory')
        report['passed'] = True
    finally:
        if root:
            # This exact random namespace was created above, never user paths.
            assert root.startswith(client.home + '/workspace/' + identity + ' ')
            client.request('DELETE', '/api/v1/projects/' + identity)
            client.shell('run-as', client.package, 'rm', '-rf', '--', root, check=False)
        if debug_port is not None:
            client.adb('forward', '--remove', 'tcp:' + str(debug_port), check=False)
        client.close()
        (args.evidence / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
