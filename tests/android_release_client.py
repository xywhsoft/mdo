"""ADB/HTTP fixture restricted to an explicitly selected independent QA APK."""
from __future__ import annotations

import http.client
import json
from pathlib import Path
import re
import shlex
import subprocess
import time


class AndroidReleaseClient:
    def __init__(self, adb: Path, serial: str, package: str):
        if not re.fullmatch(r'org\.xleaves\.mdo\.qa\.[a-z][a-z0-9_]*', package):
            raise ValueError('Select an independent org.xleaves.mdo.qa.* package')
        self.command = [str(adb.resolve()), '-s', serial]
        self.package = package
        self.local_port = self.device_port = None
        self.project = 'default'
        self.home = None

    def adb(self, *parts, check=True, timeout=30):
        result = subprocess.run([*self.command, *parts], capture_output=True, timeout=timeout)
        if check and result.returncode:
            raise AssertionError(result.stderr.decode('utf-8', errors='replace')[-1500:])
        return result.stdout

    def shell(self, *parts, check=True):
        return self.adb('shell', shlex.join(map(str, parts)), check=check).decode('utf-8', errors='replace').strip()

    def private_file(self, path, check=True):
        return self.adb('exec-out', shlex.join(['run-as', self.package, 'cat', str(path)]), check=check)

    def launch(self):
        before = self.private_file('files/mdo-home/native.log', check=False)
        self.shell('am', 'force-stop', self.package)
        self.shell('am', 'start', '-n', self.package + '/org.xserver.android.XsActivity')
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            log = self.private_file('files/mdo-home/native.log', check=False)
            delta = log[len(before):] if log.startswith(before) else log
            ports = re.findall(rb'http bound on 127\.0\.0\.1:(\d+)', delta)
            if ports and self.shell('pidof', self.package, check=False):
                self.connect(int(ports[-1]))
                break
            time.sleep(.25)
        else:
            raise AssertionError('QA APK has no native listener: ' + log[-3000:].decode('utf-8', errors='replace'))
        while time.monotonic() < deadline:
            status, _, value = self.request('GET', '/api/v1/bootstrap')
            if status == 200 and value['data'].get('ready'):
                break
            if value.get('data', {}).get('stage') == 'failed':
                raise AssertionError(value)
            time.sleep(.1)
        else:
            raise AssertionError('QA bootstrap did not become ready')
        data_root = self.shell('run-as', self.package, 'pwd')
        assert data_root.startswith('/data/') and data_root.endswith('/' + self.package), data_root
        self.home = data_root + '/files/mdo-home'
        return self.device_port

    def connect(self, port):
        self.disconnect()
        self.device_port = port
        self.local_port = int(self.adb('forward', 'tcp:0', 'tcp:' + str(port)).decode().strip())

    def request(self, method, path, body=None, extra=None):
        if self.local_port is None:
            raise RuntimeError('Connect to the QA listener first')
        authority = '127.0.0.1:' + str(self.device_port)
        headers = {'Host': authority, 'Origin': 'http://' + authority, **(extra or {})}
        if method not in ('GET', 'HEAD', 'OPTIONS'):
            _, admission, _ = self.request('GET', '/api/v1/bootstrap')
            headers['X-Mdo-Write-Token'] = admission['x-mdo-write-token']
        if body is not None:
            headers['Content-Type'] = 'application/json'
        connection = http.client.HTTPConnection('127.0.0.1', self.local_port, timeout=60)
        try:
            connection.request(method, path, json.dumps(body).encode() if body is not None else None, headers)
            response = connection.getresponse()
            return response.status, {k.lower(): v for k, v in response.getheaders()}, json.loads(response.read())
        finally:
            connection.close()

    def data(self, method, path, body=None, *, expected=(200, 201, 202), extra=None):
        status, _, value = self.request(method, path, body, extra)
        assert status in expected, (method, path, status, value)
        return value['data']

    def call(self, method, params=None, *, expected=200):
        status, _, value = self.request('POST', '/api/v1/application/call', {
            'method': method, 'params': params or {},
            'context': {'project': self.project, 'extension': 'qa.android-release'},
        })
        assert status == expected, (method, status, value)
        return value['data'] if expected == 200 else value['error']

    def disconnect(self):
        if self.local_port is not None:
            self.adb('forward', '--remove', 'tcp:' + str(self.local_port), check=False)
        self.local_port = None

    def close(self):
        self.shell('am', 'force-stop', self.package, check=False)
        self.disconnect()
