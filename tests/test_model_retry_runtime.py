"""Bounded real HTTP/TCC model recovery; no external model, key or load loop."""
from __future__ import annotations

import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import tempfile
import threading

from test_model_runtime import ROOT, run_probe, write_site


PROBE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>
#define MDO_MODEL_RETRY_BASE_MS 10u
#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"

typedef struct Observer { unsigned Retries, Deltas; xcancel* Cancel; bool Stop; } Observer;
static bool Retry(xllm_client* Client, const xllm_diagnostics* Diagnostics,
    uint32 Next, void* Data) {
    Observer* O=Data; (void)Client;
    if (Next != Diagnostics->uAttemptCount + 1u ||
        Diagnostics->uMaxAttempts != 6u) return false;
    ++O->Retries;
    if (O->Cancel) xrtCancelRequest(O->Cancel);
    return !O->Stop;
}
static bool Event(void* Data,const xllm_event* E) {
    Observer* O=Data;
    if (E->eKind==XLLM_EVENT_TEXT_DELTA || E->eKind==XLLM_EVENT_REASONING_DELTA)
        ++O->Deltas;
    return !O->Stop;
}
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;
    if (!MdoHomeInit() || !MdoConfigInit() || !MdoModelManagerInit()) {
        printf("init_failed=1\nprobe_done=1\n"); return;
    }
    MdoModelCatalog* Catalog=MdoModelCatalogSnapshot();
    MdoModelClientOptions Options; MdoModelClientOptionsInit(&Options);
    Options.Protocol=MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
    Options.MaxOutputTokens=128;
    xllm_error Error; xllmErrorInit(&Error);
    xllm_client* Client=MdoModelClientCreate(Catalog,&Options,NULL,&Error);
    if (!Client) {printf("client_failed=%s\nprobe_done=1\n",Error.sMessage); return;}
    const char* Names[]={"transient","network","daily","balance","quota",
        "membership","authentication","invalid","exhausted","retry-after",
        "cancel","deadline","partial","hook-stop","partial-recover",
        "eof-protected","eof-recover","eof-exhausted",
        "output-protected","output-recover","output-exhausted","malformed-recover"};
    for (unsigned i=0;i<sizeof(Names)/sizeof(Names[0]);++i) {
        Observer O={0}; xllm_request Request; xllmRequestInit(&Request);
        xllmRequestAddTextMessage(&Request,XLLM_ROLE_USER,Names[i]);
        Request.uMaxOutputTokens=128u;
        Request.bStream=!strncmp(Names[i],"partial",7) || !strncmp(Names[i],"eof",3) ||
            !strncmp(Names[i],"output",6) || !strncmp(Names[i],"malformed",9);
        xllm_stream_callbacks Stream={&O,Event};
        xllm_hooks Hooks={0}; Hooks.pOnRetry=Retry; Hooks.pUserData=&O;
        Request.pHooks=&Hooks;
        if (!strcmp(Names[i],"cancel")) {
            O.Cancel=xrtCancelCreate(); xllmRequestSetCancel(&Request,O.Cancel);
        }
        if (!strcmp(Names[i],"deadline")) Request.uDeadline=xrtClock()+200000u;
        if (!strcmp(Names[i],"hook-stop")) O.Stop=true;
        xllm_response* Response=NULL;
        uint64 Started=xrtClock();
        bool Restartable=strstr(Names[i],"recover") || strstr(Names[i],"exhausted");
        xllm_result Result=Restartable?
            MdoModelCompleteRestartable(Client,&Request,&Stream,&Response,&Error):
            MdoModelComplete(Client,&Request,&Stream,&Response,&Error);
        printf("case=%s result=%d kind=%s attempts=%u retries=%u deltas=%u exhausted=%u ms=%llu\n",
            Names[i],Result,MdoModelErrorKind(&Error),Error.tDiagnostics.uAttemptCount,
            O.Retries,O.Deltas,Error.tDiagnostics.bRetryExhausted?1:0,
            (unsigned long long)((xrtClock()-Started)/1000u));
        if (Request.iMessageCount!=1u || Request.uMaxOutputTokens!=128u)
            printf("request_mutated=1\n");
        if (!strcmp(Names[i],"daily") && strstr(Error.sMessage,"00:00 Asia/Shanghai"))
            printf("daily_message=accurate\n");
        if (Result==XLLM_RESULT_OK && Response &&
            Response->tDiagnostics.uAttemptCount!=Error.tDiagnostics.uAttemptCount)
            printf("response_attempts_mismatch=1\n");
        xllmResponseDestroy(Response); xllmRequestUnit(&Request);
        if (O.Cancel) xrtCancelDestroy(O.Cancel);
    }
    xllmClientDestroy(Client); MdoModelCatalogRelease(Catalog);
    MdoModelManagerUnit(); MdoConfigUnit(); MdoHomeUnit();
    printf("probe_done=1\n");
}
'''


class Model(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    calls: Counter[str] = Counter()
    lock = threading.Lock()

    def log_message(self, *_args):
        pass

    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError, BrokenPipeError):
            # Cancellation deliberately closes an idle/active fixture socket.
            pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        name = next(m['content'] for m in body['messages'] if m['role'] == 'user')
        with self.lock:
            self.calls[name] += 1
            attempt = self.calls[name]
        if name.startswith('output'):
            assert body.get('max_tokens', body.get('max_completion_tokens')) == 128, body
            if attempt > 1:
                assert len(body['messages']) == 2 and body['messages'][-1]['role'] == 'system', body
                assert 'only one small, complete tool call' in body['messages'][-1]['content']
        if name in ('output-protected', 'output-exhausted') or (
            name in ('output-recover', 'malformed-recover') and attempt == 1):
            chunk = {'choices': [{'delta': {'tool_calls': [{'index': 0, 'id': 'draft',
                'function': {'name': 'write', 'arguments': '{"content":"unfinished'}}]},
                'finish_reason': 'tool_calls' if name == 'malformed-recover' else 'length'}]}
            raw = ('data: ' + json.dumps(chunk) + '\n\n').encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        if name == 'network' and attempt == 1:
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        if name == 'partial' or (name == 'partial-recover' and attempt == 1):
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Transfer-Encoding', 'chunked')
            self.send_header('Connection', 'close')
            self.end_headers()
            chunk = {'id': 'partial', 'model': 'ornith-1.5-35b', 'choices':
                     [{'index': 0, 'delta': {'content': 'partial output'}}]}
            raw = ('data: ' + json.dumps(chunk) + '\n\n').encode()
            self.wfile.write(f'{len(raw):x}\r\n'.encode() + raw + b'\r\n')
            self.wfile.flush()
            self.close_connection = True
            return
        if name in ('eof-protected', 'eof-exhausted') or (name == 'eof-recover' and attempt == 1):
            # Valid HTTP framing and valid JSON are insufficient: the model
            # never declared this generation complete.
            chunk = {'choices': [{'delta': {'content': 'incomplete draft'}, 'finish_reason': None}]}
            raw = ('data: ' + json.dumps(chunk) + '\n\n').encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Content-Length', str(len(raw)))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(raw)
            self.close_connection = True
            return
        failure = {
            'daily': (429, 'daily_token_limit'),
            'balance': (402, 'insufficient_balance'),
            'quota': (429, 'insufficient_quota'),
            'membership': (403, 'membership_required'),
            'authentication': (401, 'invalid_api_key'),
            'invalid': (400, 'invalid_request'),
            'exhausted': (503, 'service_unavailable'),
            'cancel': (429, 'rate_limit_exceeded'),
            'deadline': (429, 'rate_limit_exceeded'),
            'hook-stop': (429, 'rate_limit_exceeded'),
        }.get(name)
        if name == 'transient' and attempt <= 2:
            failure = (429 if attempt == 1 else 503, 'rate_limit_exceeded')
        if name == 'retry-after' and attempt == 1:
            failure = (429, 'rate_limit_exceeded')
        if failure:
            status, code = failure
            payload = {'error': {'code': code, 'type': code, 'message': 'fixture-only'}}
        else:
            status = 200
            payload = {'id': 'complete', 'model': 'ornith-1.5-35b', 'choices':
                       [{'index': 0, 'message': {'role': 'assistant', 'content': 'recovered'},
                         'finish_reason': 'stop'}],
                       'usage': {'prompt_tokens': 10, 'completion_tokens': 2, 'total_tokens': 12}}
        raw = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        if name == 'retry-after' and attempt == 1:
            self.send_header('Retry-After-Ms', '80')
        if name in ('cancel', 'deadline'):
            self.send_header('Retry-After', '60')
        self.end_headers()
        self.wfile.write(raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    host = parser.parse_args().host.resolve()
    server = ThreadingHTTPServer(('127.0.0.1', 0), Model)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    Model.calls = Counter()
    try:
        with tempfile.TemporaryDirectory(prefix='model-retry-', dir=ROOT / '.build') as raw:
            base = Path(raw)
            site = base / 'site'
            write_site(site)
            (site / 'probe.c').write_text(PROBE, encoding='utf-8')
            endpoint = f'http://127.0.0.1:{server.server_port}/v1/chat/completions'
            output = run_probe(host, site, base / 'home', {
                'MDO_ORNITH_CHAT_COMPLETIONS_URL': endpoint,
                'MDO_ORNITH_API_KEY': 'fixture-only-key',
            })
            assert 'init_failed' not in output and 'client_failed' not in output, output
            expected = {'transient': (0, 'unknown', 3), 'network': (0, 'unknown', 2),
                        'daily': (-1, 'daily_token_limit', 1), 'balance': (-1, 'insufficient_balance', 1),
                        'quota': (-1, 'quota_exceeded', 1), 'membership': (-1, 'membership_required', 1),
                        'authentication': (-1, 'authentication_failed', 1), 'invalid': (-1, 'invalid_request', 1),
                        'exhausted': (-1, 'service_unavailable', 6), 'retry-after': (0, 'unknown', 2),
                        'cancel': (-3, 'cancelled', 1), 'deadline': (-2, 'timeout', 1),
                        'partial': (-1, 'network', 1), 'hook-stop': (-3, 'cancelled', 1),
                        'partial-recover': (0, 'unknown', 2),
                        'eof-protected': (-1, 'invalid_response', 1),
                        'eof-recover': (0, 'unknown', 2),
                        'eof-exhausted': (-1, 'invalid_response', 6),
                        'output-protected': (-1, 'output_limit', 1),
                        'output-recover': (0, 'unknown', 2),
                        'output-exhausted': (-1, 'output_limit', 6),
                        'malformed-recover': (0, 'unknown', 2)}
            for name, (result, kind, count) in expected.items():
                assert f'case={name} result={result} kind={kind} attempts={count}' in output, output
                assert Model.calls[name] == count, (name, Model.calls, output)
            assert 'case=exhausted result=-1 kind=service_unavailable attempts=6 retries=5' in output, output
            assert 'daily_message=accurate' in output, output
            assert 'response_attempts_mismatch' not in output, output
            assert 'request_mutated' not in output, output
            after = next(line for line in output.splitlines() if line.startswith('case=retry-after '))
            assert int(after.split('ms=')[1]) >= 80, output
            cancelled = next(line for line in output.splitlines() if line.startswith('case=cancel '))
            assert int(cancelled.split('ms=')[1]) < 1000 and 'exhausted=0' in cancelled, output
            print(output[output.find('case='):output.find('probe_done=')])
        print('Model retry, business classification, deadline, cancellation and partial-output protection: PASS')
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=3)


if __name__ == '__main__':
    main()
