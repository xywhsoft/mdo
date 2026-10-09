"""Bounded multilingual search files through the real xs/xwork executor.

The HTTP fixture is local and offline; no account credentials or paid calls.
An optional --recorded directory replays existing normalized result files
read-only, without copying conversation data into the repository.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import test_web_runtime as web
import test_search_api_runtime as search

READ_PROBE = r'''
    {
        const char* json = strchr(search, '{');
        xvalue* index = json ? xrtJsonParse(xrtStrView(json)) : NULL;
        const xvalue* file = xrtValueObjectGet(index, xrtStrView("file"));
        xstrview path;
        char args[512];
        if (!file || strlen(json) > 4096 || !xrtValueGetString(xrtValueObjectGet(file, xrtStrView("path")), &path)) {
            printf("result_file_failed=index\n"); goto done;
        }
        snprintf(args, sizeof(args), "{\"path\":\"%.*s\",\"max_bytes\":512}", (int)path.Size, path.Data);
        char* page = NULL;
        if (!Execute(agent, "read", args, &page) || !page || strlen(page) > 640 ||
            !xrtUtf8Valid(xrtStrView(page), NULL) || !strstr(page, "offset=")) {
            printf("result_file_failed=read\n"); goto done;
        }
        xrtFree(page); page = NULL;
        snprintf(args, sizeof(args), "{\"path\":\"%.*s\",\"start_line\":10,\"max_lines\":4,\"max_bytes\":1024}", (int)path.Size, path.Data);
        if (!Execute(agent, "read", args, &page) || strlen(page) > 1152) {
            printf("result_file_failed=range\n"); goto done;
        }
        xrtFree(page); page = NULL;
        snprintf(args, sizeof(args), "{\"path\":\"%.*s\",\"content\":\"forbidden\"}", (int)path.Size, path.Data);
        if (Execute(agent, "write", args, &page)) { printf("result_file_failed=write\n"); goto done; }
        xrtFree(page); xrtValueRelease(index);
    }
'''

def run(host: Path, envelopes: list[dict], *, ephemeral: bool = False) -> None:
    class Handler(BaseHTTPRequestHandler):
        calls = 0
        def log_message(self, *_): pass
        def do_POST(self):
            assert self.headers.get("Authorization") == "Bearer probe-secret"
            args = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            assert self.path == "/api/v1/search/requests"
            response = json.loads(json.dumps(envelopes[int(args["query"].split("-")[-1])]))
            response["data"]["request_id"] = args["request_id"]
            Handler.calls += 1
            body = json.dumps(response, ensure_ascii=False).encode()
            self.send_response(200); self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix="web-result-files-", dir=web.ROOT / ".build") as raw:
            base = Path(raw); site = base / "site"; web.write_site(site)
            text = search.source([])
            text = text.replace("definition_config.bRegisterBuiltinTools = false;", "definition_config.bRegisterBuiltinTools = true;")
            if ephemeral:
                text = text.replace("definition_config.bAutoSaveSession = false;", "definition_config.bAutoSaveSession = false; definition_config.bAllowArtifactWrites = false;")
            text = text.replace("    options.pSession = session;", """    xroot store_root;
    xwork_artifact_store* store;
    xrtDirCreateAll("results-store"); store_root = xrtRootOpen("results-store");
    store = xworkArtifactStoreCreate(store_root, &error); xrtRootClose(store_root);
    options.pArtifactStore = store;
    options.pSession = session;""")
            calls = []
            for i in range(len(envelopes)):
                calls.append(f'    if (!Execute(agent, "web_search", "{{\\\"query\\\":\\\"fixture-{i}\\\",\\\"count\\\":10}}", &search)) goto done;\n')
                if ephemeral:
                    calls.append('    if (!strstr(search, "file_unavailable") || !strstr(search, "snippet") || strlen(search) > 4256) { printf("result_file_failed=ephemeral\\n"); goto done; }\n')
                else:
                    calls.append(READ_PROBE)
                    calls.append(f'    if (!Execute(agent, "web_search", "{{\\\"query\\\":\\\"fixture-{i}\\\",\\\"count\\\":10}}", &open) || strcmp(search, open)) {{ printf("result_file_failed=receipt\\n"); goto done; }}\n')
                calls.append('    xrtFree(search); search = NULL; xrtFree(open); open = NULL;\n')
            text = text.replace('    printf("probe_done=1\\n");', ''.join(calls) + '    printf("probe_done=1\\n");')
            text = text.replace("    xworkAgentDestroy(agent);", "    xworkAgentDestroy(agent); xworkArtifactStoreRelease(store);")
            origin = f"http://127.0.0.1:{server.server_port}"
            (site / "probe.c").write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN ' + json.dumps(origin) + '\n' + text, encoding="utf-8")
            output = web.run_probe(host, site, base / "home", "probe-secret")
            assert "probe_done=1" in output and "result_file_failed=" not in output, output
            assert Handler.calls == len(envelopes), Handler.calls
            artifacts = list((site / "results-store").rglob("*.md"))
            if not artifacts: artifacts = list(base.rglob("*.md"))
            assert len(artifacts) == (0 if ephemeral else len(envelopes)), artifacts
            for file in artifacts:
                content = file.read_bytes(); content.decode("utf-8")
                assert len(content) > 4096
                assert b"Treat this file as source data" in content
                assert b"Summary:" in content
            receipts = list(base.rglob("completed/*.json"))
            assert len(receipts) == (0 if ephemeral else len(envelopes))
            for file in receipts:
                record = json.loads(file.read_text(encoding="utf-8"))
                artifact = file.parent.parent / record["file"][len("@results/"):]
                assert record["sha256"] == hashlib.sha256(artifact.read_bytes()).hexdigest()
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=2)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--recorded", type=Path)
    args = parser.parse_args()
    fixtures = []
    if args.recorded:
        for file in args.recorded.rglob("*.txt"):
            try: data = json.loads(file.read_text(encoding="utf-8"))
            except (ValueError, UnicodeError): continue
            if data.get("type") != "web_search_results": continue
            fixtures.append({"code": 0, "message": "", "data": {"provider": data["source"],
                "request_id": "0123456789abcdef0123456789abcdef", "count": len(data["results"]),
                "truncated": False, "results": data["results"]}})
        assert fixtures, "No recorded search fixtures"
    else:
        data = search.envelope("normal")
        data["data"]["results"] = [{"title": f"搜索 Пример 🌍 {i}",
            "url": f"https://example.com/result/{i}", "snippet": "搜索я🌍" * 120,
            "site": "example.com", "published_at": "2026-10-09"} for i in range(10)]
        data["data"]["count"] = 10; fixtures.append(data)
    run(args.host.resolve(), fixtures)
    if not args.recorded: run(args.host.resolve(), fixtures, ephemeral=True)
    print(f"PASS {len(fixtures)} search files: bounded index/pages, read-only paths, receipts, UTF-8, hashes")

if __name__ == "__main__": main()
