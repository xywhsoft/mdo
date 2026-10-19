"""Suspend only a disposable mdo child; check direct recovery or lease expiry.

This is real process suspension, not OS sleep/Android Doze acceptance. The
website keeps running. No public requests, user Home or external PID is used.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import http.client
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import uuid


class SuspendedChild:
    def __init__(self, process: subprocess.Popen):
        self.process = process
        self.handle = None
        self.suspended = False

    def __enter__(self):
        assert self.process.poll() is None, 'owned child already exited'
        if os.name == 'nt':
            from ctypes import wintypes
            self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            self.nt = ctypes.WinDLL('ntdll')
            self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            self.kernel.OpenProcess.restype = wintypes.HANDLE
            self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            self.kernel.CloseHandle.restype = wintypes.BOOL
            for name in ('NtSuspendProcess', 'NtResumeProcess'):
                function = getattr(self.nt, name)
                function.argtypes = [wintypes.HANDLE]
                function.restype = ctypes.c_long
            self.handle = self.kernel.OpenProcess(0x0800, False, self.process.pid)
            if not self.handle:
                raise ctypes.WinError(ctypes.get_last_error())
            status = self.nt.NtSuspendProcess(self.handle)
            if status != 0:
                self.kernel.CloseHandle(self.handle)
                self.handle = None
                raise RuntimeError(f'NtSuspendProcess status={status}')
        else:
            # A pidfd binds the signal to this process even if it exits. Never
            # send CONT to a recycled numeric PID during error cleanup.
            self.handle = os.pidfd_open(self.process.pid)
            try:
                signal.pidfd_send_signal(self.handle, signal.SIGSTOP)
                end = time.monotonic() + 2
                while time.monotonic() < end:
                    _, status = os.waitpid(self.process.pid, os.WUNTRACED | os.WNOHANG)
                    if os.WIFSTOPPED(status):
                        break
                    time.sleep(.01)
                else:
                    raise AssertionError('owned child did not enter stopped state')
            except BaseException:
                try:
                    signal.pidfd_send_signal(self.handle, signal.SIGCONT)
                finally:
                    os.close(self.handle)
                    self.handle = None
                raise
        self.suspended = True
        self.started = time.monotonic()
        return self

    def __exit__(self, *_):
        try:
            if self.suspended:
                if os.name == 'nt':
                    assert self.nt.NtResumeProcess(self.handle) == 0, 'resume failed'
                else:
                    signal.pidfd_send_signal(self.handle, signal.SIGCONT)
                self.suspended = False
        finally:
            if self.handle is not None:
                if os.name == 'nt':
                    self.kernel.CloseHandle(self.handle)
                else:
                    os.close(self.handle)
                self.handle = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True,
                        help='committed mdo source whose existing fixtures are imported')
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--website-host', type=Path, required=True)
    parser.add_argument('--family', type=int, choices=(4, 6), default=4)
    parser.add_argument('--mode', choices=('short', 'expired'), default='short')
    parser.add_argument('--receipt', type=Path, required=True)
    args = parser.parse_args()
    source, host = args.source.resolve(), args.host.resolve()
    test_digest = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    assert not subprocess.check_output(['git', 'status', '--porcelain'], cwd=source).strip(), 'source must be a clean committed snapshot'
    sys.path.insert(0, str(source / 'tests'))
    import test_remote_rudp as scenario
    from test_remote_live import LiveRpc
    from test_remote_manager import run, WebSocket
    from test_remote_bridge import Rpc
    from test_account_runtime import request

    assert scenario.ROOT.resolve() == source
    assert host.is_file() and args.website_host.is_file()
    scenario.FAMILY = args.family
    child = None
    result = {'phase': 'preparing', 'mode': args.mode, 'family': args.family,
              'duration_requested': 3 if args.mode == 'short' else 65,
              'os_sleep_acceptance': False, 'acceptance_72h': False}

    def launched(process, path):
        nonlocal child
        if path.resolve() == scenario.SITE.resolve():
            assert child is None
            assert path.resolve().is_relative_to((source / '.build').resolve())
            assert path.name.startswith('remote-manager-')
            assert Path(process.args[0]).resolve() == host
            child = process

    def exercise(*, client, hello, app, device, call, bearer, website_port, clients):
        assert child is not None and child.poll() is None
        site = scenario.SITE
        port = json.loads((site / 'xs.json').read_text())['services'][0]['port']
        _, headers, _ = request(port, 'GET', '/api/v1/bootstrap')
        token = headers['X-Mdo-Write-Token']

        def connect_direct(channel, metadata):
            direct = scenario.gateway(port, token, metadata['rudp'])
            clients.append(direct)
            scenario.negotiate(direct, channel, metadata['rudp'])
            rpc = scenario.BufferedRpc(direct, metadata)
            assert rpc.json('GET', '/api/v1/projects')[2] == 200
            return direct, rpc

        direct, rpc = connect_direct(client, hello)
        relay = Rpc(client, hello, client_id=rpc.client)
        assert relay.json('GET', '/api/v1/projects')[2] == 200
        live = LiveRpc(direct, hello)
        live.live = uuid.uuid4().hex
        live.offset = live.event_sequence = 0
        live.send({'type': 'live_open', 'id': live.live,
                   'runtime_id': hello['runtime_id'], 'token': token})
        assert Rpc.receive(live)['type'] == 'live_opened'
        assert live.next_event()['type'] == 'ready'

        result.update(phase='suspended', pid=child.pid)
        print(json.dumps(result), flush=True)
        with SuspendedChild(child) as stopped:
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=.25)
            try:
                connection.request('GET', '/api/v1/bootstrap')
                try:
                    connection.getresponse()
                except TimeoutError:
                    result['http_unresponsive_while_suspended'] = True
                else:
                    raise AssertionError('suspended child served a request')
            finally:
                connection.close()
            end = stopped.started + result['duration_requested']
            while time.monotonic() < end:
                assert child.poll() is None, 'owned paused child exited'
                time.sleep(max(0, min(.2, end - time.monotonic())))
            result['actual_pause_seconds'] = time.monotonic() - stopped.started
        assert child.poll() is None
        result['phase'] = 'resumed'
        print(json.dumps(result), flush=True)
        assert request(port, 'GET', '/api/v1/bootstrap')[0] == 200

        if args.mode == 'short':
            assert rpc.json('GET', '/api/v1/projects')[2] == 200
            assert relay.json('GET', '/api/v1/projects')[2] == 200
            live.close_live()
            result['same_direct_and_relay_recovered'] = True
        else:
            # Preserve the configured 60s transport idle timeout. Expired
            # sockets must close, rather than masquerading as a live route.
            direct.socket.settimeout(10)
            try:
                end = time.monotonic() + 10
                while True:
                    opcode, payload = direct.recv()
                    if opcode == 8:
                        break
                    assert opcode in (9, 10), (opcode, payload[:128])
                    if opcode == 9:
                        direct.send(10, payload)
                    assert time.monotonic() < end, 'expired direct route stayed open'
            except EOFError:
                pass
            result['expired_direct_closed'] = True
            direct.close()
            clients.remove(direct)
            client.close()
            clients.remove(client)
            until = time.monotonic() + 25
            while app()['stage'] != 'online':
                assert time.monotonic() < until, 'relay did not reconnect'
                time.sleep(.1)
            snapshot = app()
            assert snapshot['allow_remote'] and snapshot['device_id'] == device
            ticket, _ = call(website_port, 'POST', '/api/v1/devices/ticket',
                             {'device_id': device, 'role': 'controller', 'mode': 'control'}, bearer)
            ticket = ticket['data']
            client = WebSocket(website_port, {'Origin': f'http://127.0.0.1:{port}'},
                path=ticket['path'], protocol=ticket['protocol'] + ', xadmin.ticket.' + ticket['ticket'])
            clients.append(client)
            assert json.loads(client.recv()[1])['type'] == 'ready'
            hello = json.loads(client.recv()[1])
            relay = Rpc(client, hello)
            assert relay.json('GET', '/api/v1/projects')[2] == 200
            direct, rpc = connect_direct(client, hello)
            result['same_device_relay_and_fresh_direct_recovered'] = True

        client.send(1, json.dumps({'type': 'rudp_cancel', 'nonce': hello['rudp']['nonce'],
                                  'runtime_id': hello['runtime_id']}).encode())
        direct.close()
        clients.remove(direct)
        # A cancelled direct route leaves its relay operational.
        assert relay.json('GET', '/api/v1/projects')[2] == 200
        return client

    run(host, args.website_host.resolve(), exercise, site_setup=scenario.setup, on_launch=launched)
    assert child is not None and child.poll() is not None, 'fixture child not reaped'
    assert test_digest == hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'test source changed while running'
    assert not subprocess.check_output(['git', 'status', '--porcelain'], cwd=source).strip()
    result.update(phase='passed', child_reaped=True,
                  source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip(),
                  host_sha256=hashlib.sha256(host.read_bytes()).hexdigest(),
                  test_sha256=test_digest,
                  fixture_sha256={name: hashlib.sha256((source / 'tests' / name).read_bytes()).hexdigest()
                      for name in ('test_account_runtime.py', 'test_remote_manager.py', 'test_remote_rudp.py',
                                   'test_remote_live.py', 'test_remote_bridge.py', 'remote_website_fixture.py')},
                  same_device_unit_revoke_account_switch_logout=True)
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
