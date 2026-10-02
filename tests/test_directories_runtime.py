#!/usr/bin/env python3
"""Read-only host directory picker through real xs/TCC, with a tiny fixture."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from urllib.parse import quote

from test_api_runtime import ROOT, free_port, request, wait_ready, assert_common
from test_interrupt_runtime import stop_host


def probe(port: int, workspace: Path) -> None:
    endpoint = "/api/v1/workspace/directories"
    target = endpoint + "?path=" + quote(str(workspace), safe="")
    status, headers, body = request(port, "GET", target)
    document = json.loads(body)
    assert status == 200, (status, body)
    assert_common(headers, document)
    data = document["data"]
    assert Path(data["path"]) == workspace.resolve(), data
    assert Path(data["parent"]) == workspace.parent.resolve(), data
    assert data["separator"] == os.sep, data
    assert data["truncated"] is False, data
    assert {"one", "two words", "中文 & # %", ".hidden"}.issubset(data["directories"]), data
    assert "file.txt" not in data["directories"], data
    if (workspace / "linked").is_symlink():
        assert "linked" in data["directories"], data
    assert all(Path(item["path"]).is_absolute() for item in data["shortcuts"]), data
    assert {item["kind"] for item in data["shortcuts"]} == {"cwd", "home"}, data

    for path in (str(workspace / "中文 & # %"), str(workspace / "one"),
                 str(workspace / "one" / ".."), ""):
        status, _, body = request(port, "GET", endpoint + "?path=" + quote(path, safe=""))
        assert status == 200, (path, status, body)
        assert Path(json.loads(body)["data"]["path"]).is_absolute()
    status, _, body = request(port, "GET", endpoint + "?path=" + quote(workspace.anchor, safe=""))
    assert status == 200 and json.loads(body)["data"]["parent"] == "", (status, body)
    for query in ("path=%00", "path=%0A", "path=%FF", "path=%GG", "path=a&x=b", "other=a", "path=" + "x" * 2049):
        status, _, body = request(port, "GET", endpoint + "?" + query)
        assert status == 400, (query[:30], status, body)
        # Malformed wire escapes may be rejected by xs before the API route.
        if body.startswith(b"{"):
            assert json.loads(body)["error"]["code"] == "invalid_directory_path"
    for path in (workspace / "file.txt", workspace / "missing"):
        status, _, body = request(port, "GET", endpoint + "?path=" + quote(str(path), safe=""))
        assert status == 422 and json.loads(body)["error"]["code"] == "directory_unavailable", (status, body)
    status, _, body = request(port, "HEAD", target)
    assert status == 200 and body == b"", (status, body)
    status, headers, _ = request(port, "OPTIONS", target)
    assert status in (200, 204) and headers["allow"] == "GET, HEAD, OPTIONS", (status, headers)
    status, _, _ = request(port, "POST", target, body=b"{}", headers={"Content-Type":"application/json"})
    assert status == 405, status
    assert (workspace / "file.txt").read_text(encoding="utf-8") == "must stay unchanged"


def run_probe(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="directories-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw).resolve()
        site = base / "site"
        shutil.copytree(ROOT / "app", site)
        port = free_port()
        config = {"engine":{"workers":1}, "services":[{"enabled":True,"class":"http",
            "name":"mdo-directories", "ip":"127.0.0.1", "port":port,
            "host_default":{"enabled":True,"name":"mdo","path":"web","devlang":"c",
                            "devfile":"generated/mdo_unity.c"}}]}
        (site / "xs.json").write_text(json.dumps(config), encoding="utf-8")
        workspace = base / "workspace"
        workspace.mkdir()
        for name in ("one", "two words", "中文 & # %", ".hidden"):
            (workspace / name).mkdir()
        (workspace / "file.txt").write_text("must stay unchanged", encoding="utf-8")
        try: (workspace / "linked").symlink_to(workspace / "one", target_is_directory=True)
        except OSError: pass
        environment = dict(os.environ, USERPROFILE=str(base), HOME=str(base),
                           MDO_ORNITH_API_KEY="bounded-directory-test-key")
        process = None
        try:
            with (base / "runtime.log").open("wb") as log:
                process = subprocess.Popen([str(host), str(site / "xs.json"), "--", "--home", str(base / "home")],
                    cwd=base, env=environment, stdout=log, stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                wait_ready(port, process)
                probe(port, workspace)
        finally: stop_host(process)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    args = parser.parse_args()
    run_probe(args.host.resolve())
    print("Directory picker runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
