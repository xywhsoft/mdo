"""Bounded HTTP refusal before cache-preserving import payload writes.

Only copied source injects a filesystem failure. Two identical messages with
different xrt kinds prove the API uses structured state rather than text.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_api_runtime import ROOT, free_port, request, wait_ready, write_site
from test_interrupt_runtime import stop_host


def inventory(home: Path) -> dict[str, bytes]:
    return {path.relative_to(home).as_posix(): path.read_bytes()
            for path in home.rglob("*")
            if path.is_file() and path.name != ".mdo.lock"}


def run_probe(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="migration-storage-", dir=ROOT / ".build") as raw:
        for kind, expected_status, expected_code in (
            ("XERR_UNSUPPORTED", 409, "migration_storage_unsupported"),
            ("XERR_IO", 500, "migration_failed"),
        ):
            base = Path(raw) / kind
            port = free_port()
            config = write_site(base, port)
            source = base / "src/storage/home_import.inc.c"
            text = source.read_text(encoding="utf-8")
            entry = "static bool MdoHomeImportPreflight(void)\n{"
            assert text.count(entry) == 1
            text = text.replace(entry, entry +
                f'\n    MdoHomeErrorSet({kind}, MDO_HOME_ERROR_STORAGE, '
                '"synthetic unsupported filesystem message");\n    return false;')
            source.write_text(text, encoding="utf-8", newline="\n")
            home = base / "home"
            cache = home / "data/cache/webview2/cache.bin"
            cache.parent.mkdir(parents=True)
            cache.write_bytes(b"portable-cache")
            legacy = base / ".mdo/config.json"
            legacy.parent.mkdir()
            legacy_bytes = b'{"models":[],"settings":{"theme":"dark"}}'
            legacy.write_bytes(legacy_bytes)
            before = inventory(home)
            env = dict(os.environ, USERPROFILE=str(base), HOME=str(base), MDO_HOME=str(home))
            log_path = base / "probe.log"
            with log_path.open("wb") as log:
                process = subprocess.Popen([str(host), str(config)], cwd=base, env=env,
                    stdout=log, stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                try:
                    wait_ready(port, process)
                    path = "/api/v1/migrations/legacy"
                    status, _, body = request(port, "GET", path)
                    assert status == 200, body
                    preview = next(item for item in json.loads(body)["data"]["items"]
                                   if item["source_id"] == "user-home")
                    assert preview["importable"] and preview["preserve_browser_cache"], preview
                    assert inventory(home) == before
                    assert not (home / ".mdo-import").exists()
                    assert not (home / ".mdo-import-cleanup").exists()
                    payload = json.dumps({"source_id": "user-home",
                        "preview_token": preview["preview_token"]}).encode()
                    for _ in range(2):
                        status, _, body = request(port, "POST", path, body=payload,
                            headers={"Content-Type": "application/json"})
                        result = json.loads(body)
                        assert status == expected_status and result["error"]["code"] == expected_code, result
                        assert inventory(home) == before
                        assert legacy.read_bytes() == legacy_bytes
                        assert not (home / ".mdo-import").exists()
                        assert not (home / ".mdo-import-cleanup").exists()
                        snapshot = json.loads(request(port, "GET", "/api/v1/bootstrap")[2])["data"]["home"]
                        assert not snapshot["restart_required"] and not snapshot["import_in_progress"], snapshot
                    # A failed preparation releases Home and mapped project
                    # reservations; an ordinary owned mutation still succeeds.
                    status, _, body = request(port, "POST", "/api/v1/projects",
                        body=b'{"id":"after-refusal","name":"Still writable"}',
                        headers={"Content-Type": "application/json"})
                    assert status == 201, (status, body)
                except BaseException as error:
                    raise RuntimeError(f"{error}\n{log_path.read_text(encoding='utf-8', errors='replace')[-5000:]}") from error
                finally:
                    stop_host(process)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    run_probe(parser.parse_args().host.resolve())
    print("Migration unsupported-storage refusal runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
