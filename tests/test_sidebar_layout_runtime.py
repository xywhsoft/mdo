"""Sidebar persistence, CAS and real pointer/menu flows in an isolated Home."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from test_api_runtime import ROOT, free_port, request, wait_ready, write_site
from test_packed_home_lease import site as packed_site


def check(host, browser, width, packed=None):
    with tempfile.TemporaryDirectory(prefix="sidebar-layout-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        if packed: runtime, port = packed_site(base, "packed", packed); config = None
        else: runtime = base; port = free_port(); config = write_site(base, port)
        home = base / "home"; workspace = base / "workspace"; workspace.mkdir()
        env = {**os.environ, "USERPROFILE": str(base), "HOME": str(base), "USE_WEBVIEW": "0"}
        log = (base / "runtime.log").open("wb"); process = None
        def start():
            nonlocal process
            command = [str(runtime / packed.name)] if packed else [str(host), str(config)]
            process = subprocess.Popen([*command, "--", "--home", str(home)], cwd=runtime, env=env,
                stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            wait_ready(port, process)
        def stop():
            nonlocal process
            if process: process.terminate(); process.wait(timeout=12); process = None
        def call(method, path, body=None, expected=200):
            status, _, raw = request(port, method, "/api/v1" + path,
                body=json.dumps(body).encode() if body is not None else None,
                headers={"Content-Type": "application/json"} if body is not None else None)
            assert status == expected, (status, raw)
            return json.loads(raw).get("data") if raw else None
        try:
            start()
            initial = call("GET", "/sidebar-layout"); assert initial["groups"] == [] and initial["revision"] == 0
            assert not (home / "data/sidebar-layout.json").exists()
            call("HEAD", "/sidebar-layout")
            first = {**initial, "groups": [{"id": "a" * 32, "name": "工作 / Work"}]}
            saved = call("PUT", "/sidebar-layout", first); assert saved["revision"] == 1
            call("PUT", "/sidebar-layout", first, 409)
            for invalid in ({**saved, "placements": [{"key": "s:../escape", "group": "a" * 32}]},
                {**saved, "groups": saved["groups"] * 2}, {**saved, "groups": [{"id": "a" * 32, "name": "\n"}]},
                {**saved, "placements": [{"key": "p:alpha", "group": "b" * 32}]}):
                call("PUT", "/sidebar-layout", invalid, 422)
            assert call("GET", "/sidebar-layout") == saved
            saved = call("PUT", "/sidebar-layout", {**saved,
                "placements": [{"key": "p:row" + str(index), "group": ""} for index in range(2048)]})
            assert call("GET", "/sidebar-layout") == saved
            too_large = {**saved, "placements": [{"key": "s:" + "p" * 100 + "/" + str(index).zfill(80), "group": "a" * 32} for index in range(900)]}
            call("PUT", "/sidebar-layout", too_large, 422)
            stop(); start(); assert call("GET", "/sidebar-layout") == saved
            call("PUT", "/sidebar-layout", {**initial, "revision": saved["revision"]})
            for name in ("alpha", "beta"):
                call("POST", "/projects", {"id": name, "name": name.title(), "workspace_root": str(workspace)}, 201)
            ids = []
            for project, title in (("alpha", "First alpha"), ("alpha", "Second alpha"), ("beta", "Beta task"), ("default", "Free task")):
                session = call("POST", "/sessions", {"project_id": project, "title": title, "agent_id": "mdo.default",
                    "model_id": "ornith-1.5-35b", "protocol": "openai-responses", "reasoning_effort": "none"}, 201)
                ids.append(session["id"])
            shot = ROOT / f".build/sidebar-ui/{'packed-' if packed else ''}{width}.png"; shot.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="sidebar-browser-") as profile:
                url = f"http://127.0.0.1:{port}/?one={ids[0]}&two={ids[1]}&free={ids[3]}#/projects/alpha/new"
                result = subprocess.run(["node", str(ROOT / "tests/queue_browser_driver.mjs"), str(browser), profile, url,
                    str(width), str(shot), str(ROOT / "tests/fixtures/sidebar-layout-runtime.js")],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=55,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                assert result.returncode == 0, result.stderr.decode(errors="replace")[-2500:]
                report = json.loads(result.stdout.decode("utf-8")); print(json.dumps(report, ensure_ascii=False))
                assert report["passed"], report
            assert call("GET", f"/projects/alpha/sessions/{ids[0]}")["project_id"] == "alpha"
            assert call("GET", f"/projects/default/sessions/{ids[3]}")["project_id"] == "default"
            before = call("GET", "/sidebar-layout"); stop(); start(); assert call("GET", "/sidebar-layout") == before
            stop(); layout = home / "data/sidebar-layout.json"; layout.write_text("{broken", encoding="utf-8"); start()
            call("GET", "/sidebar-layout", expected=503); call("PUT", "/sidebar-layout", before, 503)
            assert layout.read_text(encoding="utf-8") == "{broken"
        except BaseException:
            log.flush(); print((base / "runtime.log").read_text(encoding="utf-8", errors="replace")[-4000:]); raise
        finally: stop(); log.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    parser.add_argument("--browser", type=Path, default=Path("C:/Program Files/Google/Chrome/Application/chrome.exe"))
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--packed", type=Path)
    args = parser.parse_args(); check(args.host.resolve(), args.browser.resolve(), args.width, args.packed.resolve() if args.packed else None)
