"""Real HTTP timeout/cancellation boundaries in native web tool execution.

Uses a loopback HTTP server and the actual xs/TCC executor. No external model,
account or search service is contacted. Registered search retries keep one ID;
the legacy billed POST is deliberately not replayed after a lost response.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import test_web_runtime as web


class Handler(BaseHTTPRequestHandler):
    calls = {}
    billed = {}
    lock = threading.Lock()

    def log_message(self, *_):
        pass

    def reply(self, status, value):
        body = json.dumps(value).encode()
        try:
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        with self.lock:
            count = self.calls.get(self.path, 0) + 1
            self.calls[self.path] = count
        if count == 1 or self.path in ('/open-cancel', '/open-deadline'):
            time.sleep(.4)
        self.reply(200, {'text': 'page recovered'})

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if self.path == '/search-legacy-total/registered':
            self.reply(404, {'error': 'route absent'})
            return
        key = body.get('request_id', 'legacy')
        with self.lock:
            entries = self.calls.setdefault(self.path, [])
            entries.append(key)
            first = len(entries) == 1
            self.billed.setdefault(self.path, set()).add(key)
        if first:
            time.sleep(.4)
        self.reply(200, {'code': 0, 'message': '', 'data': {
            'provider': 'fixture', 'request_id': key if key != 'legacy' else 'a'*32,
            'count': 1, 'truncated': False, 'results': [{
                'title': 'Recovered search', 'url': 'https://example.test/page',
                'snippet': 'fixture result', 'site': 'example.test', 'published_at': ''}]}})


def probe_source(port):
    source = web.PROBE_SOURCE
    begin, end = source.index('static bool Fetch('), source.index('static void ResponseUnit(')
    transport = r'''
static const char* ProbeCase;
static xcancel* ProbeCancel;
static uint64 ProbeDeadline;
static int32 CancelLater(void* data) { xrtSleep(50u); xrtCancelRequest((xcancel*)data); return 0; }
static bool Fetch(void* data, const XS_FetchRequest* request, XS_FetchResponse* response) {
    Probe* probe = (Probe*)data;
    ++probe->Fetches;
    if (strstr(ProbeCase, "aborted")) { xrtSetErrorKind(XERR_CANCELLED); return false; }
    if (strstr(ProbeCase, "account-cancel")) {
        xrtCancelRequest(request->Cancel); xrtSetErrorKind(XERR_CANCELLED); return false;
    }
    XS_FetchRequest bounded = *request;
    char url[192];
    snprintf(url, sizeof(url), "http://127.0.0.1:PORT/%s%s", ProbeCase,
        request->Body ? (strstr(request->Url, "/requests") ? "/registered" : "/legacy") : "");
    bounded.Url = url;
    uint64 total = strstr(ProbeCase, "idle") ? 800000u : 150000u;
    uint64 idle = strstr(ProbeCase, "idle") ? 150000u : 800000u;
    if (bounded.Timeout > total) bounded.Timeout = total;
    if (bounded.IdleTimeout > idle) bounded.IdleTimeout = idle;
    return xsFetch(&bounded, response);
}
'''.replace('PORT', str(port))
    source = source[:begin] + transport + source[end:]
    source = source.replace('context.uDeadline = MdoDeadlineAfterUs(5000000u);',
                            'context.uDeadline = ProbeDeadline; context.pCancel = ProbeCancel;')
    source = source.replace('printf("execute_%s=infra:%d success:%d text:%s\\n", name,',
                            'printf("execute_%s case:%s=infra:%d success:%d text:%s\\n", name, ProbeCase,')
    begin = source.index('    if (!Execute(agent, "web_search"')
    end = source.index('    printf("probe_done=1', begin)
    cases = ['open-idle', 'open-total', 'open-aborted', 'open-deadline', 'open-cancel',
             'search-idle', 'search-total', 'search-legacy-total', 'search-aborted', 'search-account-cancel', 'search-cancel']
    calls = []
    for case in cases:
        search = case.startswith('search')
        arguments = {'query': case, 'count': 1} if search else {'url': f'http://127.0.0.1:{port}/{case}'}
        calls.append('''
    ProbeCase = CASE; probe.Fetches = 0u; ProbeCancel = xrtCancelCreate();
    ProbeDeadline = MdoDeadlineAfterUs(DEADLINEu);
    xthread* worker_NAME = CANCEL_THREAD;
    bool ok_NAME = Execute(agent, TOOL, ARGUMENTS, &open);
    printf("case=NAME success:%d fetches:%u parent_cancelled:%d\\n",ok_NAME,probe.Fetches,xrtCancelRequested(ProbeCancel));
    if (worker_NAME) { xrtThreadWait(worker_NAME); xrtThreadDestroy(worker_NAME); }
    xrtCancelDestroy(ProbeCancel); ProbeCancel = NULL; xrtFree(open); open = NULL;
'''.replace('CASE', json.dumps(case)).replace('DEADLINE', '80000' if case == 'open-deadline' else '5000000')
            .replace('CANCEL_THREAD', 'xrtThreadCreate(CancelLater, ProbeCancel, 0u)' if case in ('open-cancel','search-cancel') else 'NULL')
            .replace('TOOL', json.dumps('web_search' if search else 'web_open'))
            .replace('ARGUMENTS', json.dumps(json.dumps(arguments)))
            .replace('NAME', case.replace('-', '_')))
    source = source[:begin] + ''.join(calls) + source[end:]
    return source.replace('    MdoHomeUnit();', '    MdoHomeUnit(); fflush(stdout); exit(0);')


def run(host, baseline=None, report=None):
    Handler.calls, Handler.billed = {}, {}
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        with tempfile.TemporaryDirectory(prefix='tool-timeouts-', dir=web.ROOT/'.build') as raw:
            base = Path(raw)
            site = base/'site'
            web.write_site(site)
            if baseline:
                for name in ('manager.c', 'fetch_recovery.inc.c', 'search_api.inc.c'):
                    (site/'src/web'/name).write_bytes((baseline/'app/src/web'/name).read_bytes())
            (site/'probe.c').write_text(probe_source(server.server_port), encoding='utf-8')
            defaults = site/'default-home/config/defaults.json'
            config = json.loads(defaults.read_text(encoding='utf-8'))
            config['settings']['web'].update(allow_http=True, allow_private_networks=True)
            defaults.write_text(json.dumps(config), encoding='utf-8')
            environment = dict(os.environ, MDO_TEST_SEARCH_ACCESS_TOKEN='probe-secret')
            result = subprocess.run([str(host.resolve()), 'xs.json', '--', '--home', str(base/'home')],
                cwd=site, env=environment, capture_output=True, text=True, encoding='utf-8', errors='replace',
                timeout=25, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            output = result.stdout + result.stderr
            if report:
                report.parent.mkdir(parents=True, exist_ok=True)
                report.write_text(json.dumps({'baseline': bool(baseline), 'output': output,
                    'calls': Handler.calls, 'billed': {k: len(v) for k,v in Handler.billed.items()}},indent=2)+'\n',encoding='utf-8')
            assert result.returncode == 0 and 'probe_done=1' in output and 'init_error=' not in output, output
            if baseline:
                assert 'case=open_idle success:0 fetches:1 parent_cancelled:0' in output, output
                assert 'execute_web_open case:open-idle=infra:0' in output, output
                assert 'execute_web_search case:search-idle=infra:0' in output, output
                print('PASS baseline: real HTTP timeouts incorrectly terminate both web tools without parent cancellation')
                return
            for case in ['open-idle', 'open-total', 'search-idle', 'search-total']:
                tool = 'web_search' if case.startswith('search') else 'web_open'
                assert f'case={case.replace("-","_")} success:1 fetches:2 parent_cancelled:0' in output, output
                assert f'execute_{tool} case:{case}=infra:1 success:1' in output, output
            for case in ['open-aborted', 'search-aborted', 'search-account-cancel', 'search-legacy-total']:
                tool = 'web_search' if case.startswith('search') else 'web_open'
                assert f'execute_{tool} case:{case}=infra:1 success:0' in output, output
                assert f'case={case.replace("-","_")} success:0 fetches:{2 if case=="search-legacy-total" else 1} parent_cancelled:0' in output, output
            assert 'execute_web_open case:open-cancel=infra:0 success:0' in output, output
            # This direct executor's deadline belongs to the call, not an
            # active agent-run scope. It must produce a failed tool result.
            assert 'execute_web_open case:open-deadline=infra:1 success:0' in output, output
            assert 'case=open_cancel success:0 fetches:1 parent_cancelled:1' in output, output
            assert 'execute_web_search case:search-cancel=infra:0 success:0' in output, output
            assert 'case=search_cancel success:0 fetches:1 parent_cancelled:1' in output, output
            for case in ['search-idle', 'search-total']:
                path = f'/{case}/registered'
                assert len(Handler.calls[path]) == 2 and len(set(Handler.calls[path])) == 1, Handler.calls
                assert len(Handler.billed[path]) == 1, Handler.billed
            assert len(Handler.calls['/search-legacy-total/legacy']) == 1, Handler.calls
            print('PASS 11 native tool cases: real idle/total HTTP timeouts recover, local/account cancellation stays local, '
                  'legacy POST is not replayed, call deadline is reported and task stop still terminates')
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, help='optional pre-fix source snapshot')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    run(args.host, args.baseline, args.report)
