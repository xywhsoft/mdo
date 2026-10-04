"""Bounded search API integration: real xs POST, xwork executor and local HTTP.

No upstream API keys or public search calls. Also supports a disposable xadmin
server fixture with a real member JWT via --endpoint and --token-file.
"""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
from urllib.parse import urlsplit

import test_web_runtime as web


def envelope(query: str) -> dict:
    results = [] if query == "empty" else [{
        "title": "墨斗检索", "url": "https://example.com/search-result",
        "snippet": "UTF-8 search result", "site": "example.com", "published_at": "2026-10-04",
    }]
    return {"code": 0, "message": "", "data": {
        "provider": "zai" if query == "zai" else "bocha",
        "request_id": "0123456789abcdef0123456789abcdef", "count": len(results),
        "truncated": query == "truncated", "results": results,
    }}


class Handler(BaseHTTPRequestHandler):
    calls: list[dict] = []

    def log_message(self, *_args: object) -> None:
        pass

    def do_POST(self) -> None:
        assert self.path == "/api/v1/search", self.path
        assert self.headers.get("Authorization") == "Bearer probe-secret"
        assert self.headers.get("Content-Type") == "application/json"
        assert self.headers.get("Accept") == "application/json"
        args = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert set(args) <= {"query", "count"}, args
        self.calls.append(args)
        query = args["query"]
        status = int(query[6:]) if query.startswith("status") else 200
        response = envelope(query)
        if query == "business403":
            response = {"code": 403, "message": "probe-secret must never echo"}
        if query == "badcount":
            response["data"]["count"] = True
        if query == "badurl":
            response["data"]["results"][0]["url"] = "javascript:alert(1)"
        if query == "longtitle":
            response["data"]["results"][0]["title"] = "x" * 513
        if query == "duplicate":
            response["data"]["results"] *= 2
            response["data"]["count"] = 2
        body = (b"<html>login required probe-secret</html>" if query == "html"
                else json.dumps(response, ensure_ascii=False).encode())
        if status != 200:
            body = b'{"message":"probe-secret must never echo"}'
        self.send_response(status)
        if status == 302:
            self.send_header("Location", "/credential-leak")
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def source(cases: list[tuple[str, bool]], *, external: bool = False) -> str:
    text = web.PROBE_SOURCE
    a = text.index("static bool Fetch(")
    b = text.index("static void ResponseUnit(", a)
    text = text[:a] + r'''
static bool Fetch(void* data, const XS_FetchRequest* request, XS_FetchResponse* response) {
    Probe* probe = (Probe*)data;
    ++probe->Fetches;
    if (request->Method == NULL || strcmp(request->Method, "POST") != 0 ||
        request->Body == NULL || request->BodySize == 0u ||
        request->MaxRedirects != 0u || (request->Flags & XS_FETCH_FOLLOW_REDIRECTS) != 0u ||
        (request->Flags & XS_FETCH_PUBLIC_ADDRESSES_ONLY) != 0u)
        printf("request_contract_failed=1\n");
    return xsFetch(request, response);
}
''' + text[b:]
    # The old page extractor fixture is not needed; xs owns native responses.
    a = text.index('    if (!Execute(agent, "web_search"')
    b = text.index("done:\n", a)
    calls = []
    for index, (arguments, success) in enumerate(cases):
        literal = json.dumps(arguments, ensure_ascii=False)
        calls.append(f'    if (Execute(agent, "web_search", {literal}, &search) != {str(success).lower()}) '
                     f'{{ printf("case_failed={index}\\n"); goto done; }}\n'
                     '    xrtFree(search); search = NULL;\n')
    # Obsolete standalone search options are ignored during import.
    if not external:
        calls.append(r'''
    {
        const char* legacy = "{\"schema_version\":1,\"patch\":{\"web\":{\"search\":{\"endpoint\":\"https://other.example/api/v1/search\",\"provider\":\"bing\"}}}}";
        if (!MdoConfigImport(MDO_CONFIG_SETTINGS, xrtStrView(legacy))) {
            printf("legacy_search_migration_failed=1\n"); goto done;
        }
        char* effective = MdoConfigEffectiveJson(NULL);
        xvalue* root = effective ? xrtJsonParse(xrtStrView(effective)) : NULL;
        xvalue* settings = xrtValueObjectGet(root, xrtStrView("settings"));
        xvalue* web = xrtValueObjectGet(settings, xrtStrView("web"));
        bool clean = web && !xrtValueObjectHas(web, xrtStrView("search"));
        xrtValueRelease(root); xrtFree(effective);
        if (!clean) { printf("legacy_search_retained=1\n"); goto done; }
    }
''')
    text = text[:a] + ''.join(calls) + '    printf("probe_done=1\\n");\n' + text[b:]
    # Avoid an unused helper in strict source checks.
    a = text.index('static unsigned char *Copy(')
    b = text.index('static bool Fetch(', a)
    return text[:a] + text[b:]


def invoke(host: Path, endpoint: str, cases: list[tuple[str, bool]],
           token: str | None = "probe-secret", *, external: bool = False) -> str:
    with tempfile.TemporaryDirectory(prefix="search-api-", dir=web.ROOT / ".build") as raw:
        base = Path(raw)
        web.write_site(base / "site")
        url = urlsplit(endpoint)
        assert url.scheme in ('http', 'https') and url.path == '/api/v1/search' and not url.query and not url.fragment and not url.username
        origin = url.scheme+'://'+url.netloc
        (base / "site/probe.c").write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(origin)+'\n'+source(cases, external=external), encoding="utf-8")
        output = web.run_probe(host.resolve(), base / "site", base / "home", token)
        assert "probe_done=1" in output, output
        assert "case_failed=" not in output and "init_error=" not in output, output
        assert "request_contract_failed=" not in output and "legacy_search_migration_failed=" not in output and "legacy_search_retained=" not in output, output
        # Tokens and hostile upstream error bodies never reach the model.
        if token:
            assert token not in output, output
        return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    import os
    parser.add_argument("--host", type=Path, default=web.ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    parser.add_argument("--endpoint")
    parser.add_argument("--token-file", type=Path)
    args = parser.parse_args()
    if args.endpoint:
        assert args.token_file
        token = args.token_file.read_text(encoding="utf-8").strip()
        output = invoke(args.host, args.endpoint, [('{"query":"hello","count":2}', True)], token, external=True)
        assert '"type":"web_search_results"' in output, output
        print("PASS real xadmin member JWT search integration")
        return
    Handler.calls = []
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        endpoint = f"http://127.0.0.1:{server.server_port}/api/v1/search"
        queries = ["墨斗", "zai", "empty", "truncated", "duplicate"]
        failures = [f"status{status}" for status in (400, 401, 403, 404, 405, 429, 502, 503, 504, 302)]
        failures += ["business403", "html", "badcount", "badurl", "longtitle"]
        cases = [(json.dumps({"query": query}, ensure_ascii=False), query in queries)
                 for query in queries + failures]
        # Argument failures are rejected before any network call.
        cases += [(arguments, False) for arguments in (
            '{"query":"   "}', '{"query":"line\\nbreak"}', '{"query":"ok","count":11}',
            '{"query":"ok","provider":"bocha"}', '{"query":"ok","count":true}')]
        output = invoke(args.host, endpoint, cases)
        assert len(Handler.calls) == len(queries + failures), Handler.calls
        for marker in ('"source":"zai"', '"results":[]', '"truncated":true', '"count":1',
                       'valid account login', 'phone/email verification', 'quota or concurrency',
                       'configure its API key', 'invalid xadmin response'):
            assert marker in output, output
        before = len(Handler.calls)
        missing = invoke(args.host, endpoint, [('{"query":"hello"}', False)], None)
        assert "Search needs an account login" in missing
        invalid = invoke(args.host, endpoint, [('{"query":"hello"}', False)], "bad\r\ntoken")
        assert "Search needs an account login" in invalid
        assert len(Handler.calls) == before
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    print("PASS search API POST/auth/UTF-8/results/empty/errors/no-redirect/no-retry/limits")


if __name__ == "__main__":
    main()
