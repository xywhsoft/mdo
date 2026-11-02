"""Manual Windows host acceptance; opens only local fixture pages, no model calls."""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    host = args.host.resolve()
    results = {}
    for name in ("missing", "create-failure", "headless", "assembly-failure", "native"):
        site = args.evidence / name
        (site / "web").mkdir(parents=True, exist_ok=True)
        (site / "web/index.html").write_text(
            "<!doctype html><title>xs fallback QA</title><p>browser fallback ready</p>", encoding="utf-8")
        (site / "main.c").write_text("invalid C !" if name == "assembly-failure" else
            "#include <xsbase.h>\nvoid ServiceInit(XS_HostInfo* p){(void)p;}\n"
            "void ServiceUnit(XS_HostInfo* p){(void)p;}\n", encoding="utf-8")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        config = {"services": [{"enabled": True, "class": "app", "name": "fallback-qa",
            "ip": "127.0.0.1", "port": port if name == "headless" else 0,
            "host_default": {"enabled": True, "name": "fallback-qa", "path": "web",
                             "devlang": "c", "devfile": "main.c"},
            "window": {"title": "xs fallback QA", "url": "/", "close": "stop",
                       "profile_dir": "profile", "install_fallback": "headless" if name == "headless" else "browser"}}]}
        path = site / "xs.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        env = {k: v for k, v in os.environ.items() if k not in
               {"XS_WV2_FORCE_MISSING", "XS_WV2_FORCE_CREATE_FAILURE", "XS_APP_AUTOCLOSE_MS"}}
        if name in {"missing", "headless", "assembly-failure"}:
            env["XS_WV2_FORCE_MISSING"] = "1"
        elif name == "create-failure":
            env["XS_WV2_FORCE_CREATE_FAILURE"] = "1"
        else:
            env["XS_APP_AUTOCLOSE_MS"] = "2000"
        command = [str(host), str(path.resolve())]
        if name == "create-failure":
            command += ["--port", str(port)]
        log_path = site / "launch.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen(command, cwd=site, env=env, stdout=log, stderr=log,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                def output():
                    return log_path.read_text(encoding="utf-8", errors="replace")
                if name in {"assembly-failure", "native"}:
                    process.wait(timeout=25)
                    text = output()
                    assert "browser/service mode ->" not in text, text
                    assert (process.returncode == 0) == (name == "native"), text
                    if name == "native":
                        assert "frontend ready ->" in text, text
                    results[name] = {"passed": True, "exit_code": process.returncode, "browser_opens": 0}
                else:
                    url = f"http://127.0.0.1:{port}/" if name == "headless" else None
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        assert process.poll() is None, output()
                        if url is None:
                            match = re.search(r"browser/service mode -> (http://127\.0\.0\.1:\d+/)", output())
                            if match:
                                url = match[1]
                        if url:
                            try:
                                with urlopen(url, timeout=1) as response:
                                    if b"browser fallback ready" in response.read():
                                        break
                            except OSError:
                                pass
                        time.sleep(.05)
                    else:
                        raise AssertionError(output())
                    time.sleep(.3)
                    text = output()
                    opens = text.count("browser/service mode ->")
                    assert opens == (0 if name == "headless" else 1), text
                    if name == "create-failure":
                        assert url == f"http://127.0.0.1:{port}/" and "webview create failed" in text, text
                    if name == "missing":
                        second = subprocess.run(command, cwd=site, env=env, capture_output=True, timeout=10)
                        assert second.returncode == 0, second.stdout
                        deadline = time.monotonic() + 5
                        while output().count("browser/service mode ->") < 2 and time.monotonic() < deadline:
                            time.sleep(.05)
                        assert output().count("browser/service mode ->") == 2, output()
                        assert process.poll() is None
                    results[name] = {"passed": True, "url": url, "initial_browser_opens": opens,
                                     "reopen_verified": name == "missing", "service_survived": process.poll() is None}
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
        print(name, "PASS", flush=True)
    (args.evidence / "result.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
