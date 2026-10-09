"""Bounded lost/held schedule acknowledgements around a disposable packed app.

fault.json selects one method/path and drop or hold; its rule is consumed once.
Only the browser/client can send another write. The proxy never replays one.
"""
import http.client
import json
import socket
import time

import manual_timeline_history_qa as fixture
from manual_extension_form_qa import FormProxy


class ScheduleProxy(FormProxy):
    def forward(self):
        if not self.path.startswith('/api/v1/schedules'):
            return super().forward()
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        rule = None
        with self.lock:
            control = self.root / 'fault.json'
            if control.exists():
                candidate = json.loads(control.read_text(encoding='utf-8'))
                matches = candidate.get('path') == self.path or (candidate.get('path_prefix') and
                    self.path.startswith(candidate['path_prefix']))
                if matches and candidate['method'] == self.command:
                    rule = candidate
                    if candidate.get('remaining', 1) > 1:
                        control.write_text(json.dumps(dict(candidate, remaining=candidate['remaining'] - 1)), encoding='utf-8')
                    else:
                        control.unlink()
        if rule and rule['mode'] == 'read-error':
            assert self.command == 'GET', 'synthetic faults only replace reads'
            raw = json.dumps(dict(ok=False, error=dict(code='schedule_service_unavailable',
                message='Temporary QA read failure'))).encode()
            record = dict(method='GET', path=self.path, started=time.monotonic(),
                status=503, response=json.loads(raw), mode='read-error', phase=rule['phase'])
            with self.lock:
                with (self.root / 'schedule-requests.jsonl').open('a', encoding='utf-8') as output:
                    output.write(json.dumps(record) + '\n')
            self.send_response(503)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers(); self.wfile.write(raw)
            return
        connection = http.client.HTTPConnection('127.0.0.1', self.upstream, timeout=10)
        headers = {name: value for name, value in self.headers.items()
                   if name.lower() not in ('connection', 'transfer-encoding')}
        headers['Connection'] = 'close'
        try:
            started = time.monotonic()
            connection.request(self.command, self.path, body, headers)
            response = connection.getresponse()
            raw = response.read()
            record = dict(method=self.command, path=self.path, started=started,
                status=response.status, received=time.monotonic(),
                request=json.loads(body or '{}'), response=json.loads(raw),
                mode=rule['mode'] if rule else 'normal', phase=rule['phase'] if rule else '')
            with self.lock:
                with (self.root / 'schedule-requests.jsonl').open('a', encoding='utf-8') as output:
                    output.write(json.dumps(record) + '\n')
            if rule and rule['mode'] == 'drop':
                self.close_connection = True
                self.connection.shutdown(socket.SHUT_RDWR)
                return
            self.send_response(response.status)
            for name, value in response.getheaders():
                if name.lower() not in ('content-length', 'transfer-encoding', 'connection'):
                    self.send_header(name, value)
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            if rule and rule['mode'] == 'hold':
                (self.root / ('held-' + rule['phase'] + '.json')).write_text(
                    json.dumps(record, indent=2) + '\n', encoding='utf-8')
                deadline = time.monotonic() + 30
                while not self.quitting.is_set() and not (self.root / ('release-' +
                        rule['phase'])).exists() and time.monotonic() < deadline:
                    time.sleep(.05)
            self.wfile.write(raw)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        finally:
            connection.close()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = forward


if __name__ == '__main__':
    fixture.Proxy = ScheduleProxy
    fixture.main()
