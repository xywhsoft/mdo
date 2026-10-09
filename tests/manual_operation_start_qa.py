"""Bounded real packed operation acknowledgements in a disposable Home.

fault.json selects one path and drop/hold/delay/corrupt. Each rule is consumed
once. The proxy never repeats a native POST; holds stop after 30 seconds.
"""
import http.client
import json
import socket
import time

from manual_extension_form_qa import FormProxy
import manual_timeline_history_qa as fixture


class OperationProxy(FormProxy):
    def forward(self):
        operation = self.path.startswith('/api/v1/operations/')
        start = self.command == 'POST' and (self.path == '/api/v1/modules/reload' or
            self.path.startswith('/api/v1/mcp/') and self.path.endswith('/refresh'))
        if not start and not operation:
            return super().forward()
        started = time.monotonic()
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        fault = ''
        with self.lock:
            control = self.root / 'fault.json'
            if start and control.exists():
                rule = json.loads(control.read_text(encoding='utf-8'))
                if rule['path'] == self.path:
                    fault = rule['mode']; control.unlink()
        if fault == 'delay':
            time.sleep(18)  # Request body is already owned; submit only once.
        connection = http.client.HTTPConnection('127.0.0.1', self.upstream, timeout=15)
        headers = {name: value for name, value in self.headers.items()
                   if name.lower() not in ('connection', 'transfer-encoding')}
        headers['Connection'] = 'close'
        try:
            connection.request(self.command, self.path, body, headers)
            response = connection.getresponse(); raw = response.read()
            record = dict(method=self.command, path=self.path, status=response.status,
                          mode=fault or 'normal', started=started, received=time.monotonic())
            if start:
                record['requested_id'] = json.loads(body or '{}').get('operation_id')
                record['operation'] = json.loads(raw).get('data')
            with self.lock:
                with (self.root / 'operation-requests.jsonl').open('a', encoding='utf-8') as output:
                    output.write(json.dumps(record) + '\n')
            if fault == 'drop':
                self.close_connection = True
                self.connection.shutdown(socket.SHUT_RDWR)
                return
            if fault == 'corrupt': raw = b'{incomplete acknowledgement'
            self.send_response(response.status)
            for name, value in response.getheaders():
                if name.lower() not in ('content-length', 'transfer-encoding', 'connection'):
                    self.send_header(name, value)
            self.send_header('Content-Length', str(len(raw))); self.end_headers()
            if fault == 'hold':
                (self.root / 'ack-held.json').write_text(json.dumps(record) + '\n', encoding='utf-8')
                deadline = time.monotonic() + 30
                while not self.quitting.is_set() and not (self.root / 'release-ack').exists() and time.monotonic() < deadline:
                    time.sleep(.05)
            self.wfile.write(raw)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        finally:
            connection.close()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = forward


if __name__ == '__main__':
    fixture.Proxy = OperationProxy
    fixture.main()
