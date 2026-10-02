"""Bounded HTTP/TLS segmented upload and immutable reader ownership probe.

One ordinary 2 MiB file is downloaded with the production v2 route, then uploaded
in <=256 KiB chunks. No model, shell, queue, restore or user Home is executed.
Expiry/reader controls are injected only into copied sources; no load test.
"""
from __future__ import annotations

import argparse
from contextlib import closing
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import tempfile
import time

from test_api_runtime import ROOT, free_port

PREFIX = "/api/v1/session-backups/uploads"
CONTROL = "/__fixture/backup-upload/"
CHUNK = 256 * 1024


class Probe:
    def __init__(self, host, base, secure):
        self.host, self.base, self.secure = host, base, secure
        self.site, self.home = base / "site", base / "home"
        self.port, self.process = free_port(), None
        shutil.copytree(ROOT / "app", self.site)
        shutil.copy2(ROOT / "tests/fixtures/backup-upload.c", self.site / "src/bootstrap/backup-upload.c")
        service = self.site / "src/bootstrap/service.c"
        text = service.read_text(encoding="utf-8")
        for old, new in (
            ("void ServiceInit(XS_HostInfo* pHost)", '#include "backup-upload.c"\nvoid ServiceInit(XS_HostInfo* pHost)'),
            ("    MdoApiUnit();", "    BackupUploadFixtureUnit();\n    MdoApiUnit();"),
            ("    return MdoApiRequest(pRequest);", "    if (BackupUploadFixtureControl(pRequest)) return XS_OK;\n    return MdoApiRequest(pRequest);"),
        ):
            assert text.count(old) == 1, old
            text = text.replace(old, new, 1)
        service.write_text(text, encoding="utf-8", newline="\n")
        host_config = {"enabled": True, "name": "mdo", "path": "web", "devlang": "c", "devfile": "generated/mdo_unity.c"}
        if secure:
            for kind in ("cert", "key"):
                shutil.copy2(ROOT / f"tests/fixtures/proxy-localhost-{kind}.pem", self.site / f"fixture-{kind}.pem")
            host_config.update(tls_cert="fixture-cert.pem", tls_key="fixture-key.pem")
        plain_port = free_port() if secure else self.port
        while secure and plain_port == self.port:
            plain_port = free_port()
        self.config = self.site / "xs.json"
        self.config.write_text(json.dumps({"engine": {"workers": 2}, "services": [{
            "enabled": True, "class": "http", "tls": secure, "name": "upload-probe", "ip": "127.0.0.1",
            "ip_tls": "127.0.0.1", "port": plain_port, "port_tls": self.port if secure else 0,
            "host_default": host_config, "recv_limit": 8454144, "body_limit": 8388608,
        }]}), encoding="utf-8")

    def call(self, method, path, body=None, headers=None, *, chunked=False, token=True):
        headers = dict(headers or {})
        if token and method not in ("GET", "HEAD", "OPTIONS") and path.startswith("/api/v1/"):
            headers["X-Mdo-Write-Token"] = self.call("GET", "/api/v1/bootstrap")[1]["x-mdo-write-token"]
        if isinstance(body, dict):
            body = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
        if self.secure:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            context.check_hostname, context.verify_mode = False, ssl.CERT_NONE  # loopback fixture only
            conn = http.client.HTTPSConnection("127.0.0.1", self.port, timeout=4, context=context)
        else:
            conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=4)
        with closing(conn):
            conn.request(method, path, body=body, headers=headers, encode_chunked=chunked)
            response = conn.getresponse()
            return response.status, {k.lower(): v for k, v in response.getheaders()}, response.read()

    def api(self, method, path, body=None, headers=None, **kwargs):
        status, _, data = self.call(method, path, body, headers, **kwargs)
        return status, json.loads(data)

    def control(self, name):
        status, value = self.api("GET", CONTROL + name)
        assert status == 200, value
        return value["data"]

    def start(self):
        self.log = (self.base / "xs.log").open("wb")
        self.process = subprocess.Popen([str(self.host), str(self.config)], cwd=self.site,
            env=dict(os.environ, MDO_HOME=str(self.home), MDO_ORNITH_RESPONSES_URL="https://example.invalid/v1",
                     MDO_ORNITH_API_KEY="bounded-upload-fixture"), stdout=self.log, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise AssertionError((self.base / "xs.log").read_text(errors="replace"))
            try:
                status, body = self.api("GET", "/api/v1/bootstrap")
                if status == 200 and body["data"]["ready"]:
                    return
            except (OSError, ValueError):
                pass
            time.sleep(0.05)
        raise AssertionError((self.base / "xs.log").read_text(errors="replace"))

    def stop(self):
        if self.process is not None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.log.close()

    def check(self):
        status, body = self.api("POST", "/api/v1/sessions", {
            "project_id": "default", "title": "Upload source", "agent_id": "mdo.default",
            "model_id": "ornith-1.5-35b", "protocol": "openai-responses", "reasoning_effort": "medium", "max_output_tokens": 1024,
        })
        assert status == 201, body
        session = body["data"]["id"]
        artifact = self.home / "sessions/default" / session / "artifacts/run-00000000000000000001/00000000000000000001-upload.txt"
        artifact.parent.mkdir(parents=True)
        artifact.write_bytes(bytes(range(256)) * 8192)
        status, _, data = self.call("GET", f"/api/v1/projects/default/sessions/{session}/backup")
        assert status == 200 and len(data) > CHUNK, data[:1024]
        digest = hashlib.sha256(data).hexdigest()
        def inventory():
            # The process lease is exclusively held on Windows; it is neither
            # uploaded content nor a writable session/config sidecar.
            return {str(p.relative_to(self.home)): p.read_bytes() for p in self.home.rglob("*")
                    if p.is_file() and p != self.home / ".mdo.lock"}
        before = inventory()
        upload_id = "a" * 32
        path = PREFIX + "/" + upload_id
        create = {"id": upload_id, "bytes": len(data), "sha256": digest}
        assert self.api("GET", PREFIX)[1]["data"]["upload"] is None
        assert self.api("POST", PREFIX, create, token=False)[0] == 428
        for invalid in ({"id": "bad", "bytes": 1}, {"id": upload_id, "bytes": 0},
                        {"id": upload_id, "bytes": 1, "sha256": "bad"}, {**create, "extra": 1}):
            assert self.api("POST", PREFIX, invalid)[0] == 422, invalid
        assert self.api("POST", PREFIX, {"id": upload_id, "bytes": 96 * 1024 * 1024 + 1})[0] == 413
        assert self.api("POST", PREFIX, create)[0] == 201
        assert self.api("GET", PREFIX)[1]["data"]["upload"]["id"] == upload_id
        status, body = self.api("POST", PREFIX, create)
        assert status == 200 and body["data"]["received_bytes"] == 0
        assert self.api("POST", PREFIX, {**create, "bytes": len(data) - 1})[0] == 409
        assert self.api("POST", PREFIX, {**create, "id": "b" * 32})[0] == 503
        assert self.control("pin/" + upload_id)["access"] == 2  # incomplete
        assert self.api("POST", path + "/seal")[0] == 409
        assert self.call("HEAD", path)[0] == 200 and self.call("HEAD", path)[2] == b""
        assert self.call("OPTIONS", path + "/chunks/0")[0] == 200
        assert self.call("PATCH", path)[0] == 405
        binary = {"Content-Type": "application/octet-stream"}
        assert self.api("PUT", path + "/chunks/0", b"x")[0] == 415
        assert self.api("PUT", path + "/chunks/01", b"x", binary)[0] == 400
        assert self.api("PUT", path + "/chunks/1", b"x", binary)[0] == 409
        assert self.api("PUT", path + "/chunks/0", b"x" * (CHUNK + 1), binary)[0] == 413
        # Transport framing does not change the decoded chunk byte count.
        for offset in range(0, len(data), CHUNK):
            chunk = data[offset:offset + CHUNK]
            if offset == 0:
                content = [chunk[:1234], chunk[1234:]]
                status, body = self.api("PUT", path + "/chunks/0", content, binary, chunked=True)
            else:
                status, body = self.api("PUT", path + f"/chunks/{offset}", chunk, binary)
            assert status == 200 and body["data"]["received_bytes"] == offset + len(chunk), (status, body)
            if offset == 0:
                assert self.api("PUT", path + "/chunks/0", b"!" + chunk[1:], binary)[0] == 409
                status, body = self.api("PUT", path + "/chunks/0", chunk, binary)
                assert status == 200 and body["data"]["received_bytes"] == len(chunk)
                # A disconnected, incomplete HTTP request never enters the
                # upload handler or advances accepted bytes (HTTP and TLS).
                write_token = self.call("GET", "/api/v1/bootstrap")[1]["x-mdo-write-token"]
                if self.secure:
                    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
                    context.check_hostname, context.verify_mode = False, ssl.CERT_NONE
                    partial = http.client.HTTPSConnection("127.0.0.1", self.port, timeout=4, context=context)
                else:
                    partial = http.client.HTTPConnection("127.0.0.1", self.port, timeout=4)
                with closing(partial):
                    partial.putrequest("PUT", path + f"/chunks/{CHUNK}")
                    partial.putheader("Content-Type", "application/octet-stream")
                    partial.putheader("Content-Length", "1024")
                    partial.putheader("X-Mdo-Write-Token", write_token)
                    partial.endheaders(b"partial")
                assert self.api("GET", path)[1]["data"]["received_bytes"] == CHUNK
        assert self.api("PUT", path + f"/chunks/{len(data)}", b"!", binary)[0] == 409
        status, body = self.api("POST", path + "/seal")
        assert status == 200 and body["data"]["sha256"] == digest and body["data"]["state"] == "sealed"
        assert body["data"]["restore_ready"] is False and body["data"]["validation"] == "transport-sha256"
        assert self.api("POST", path + "/seal")[1]["data"]["sha256"] == digest
        assert self.api("PUT", path + "/chunks/0", data[:CHUNK], binary)[0] == 409
        assert self.control("pin/" + upload_id)["sha256"] == digest
        assert self.api("DELETE", path)[0] == 200
        assert self.api("GET", path)[0] == 404
        assert self.api("GET", PREFIX)[1]["data"]["upload"] is None
        assert self.control("state")["sha256"] == digest  # DELETE cannot free a reader
        assert self.api("POST", PREFIX, {"id": "b" * 32, "bytes": 1})[0] == 503
        self.control("release")
        # Optional sender checksum: the server still computes a whole-file SHA.
        small = "b" * 32
        small_path = PREFIX + "/" + small
        assert self.api("POST", PREFIX, {"id": small, "bytes": 3})[0] == 201
        assert self.api("PUT", small_path + "/chunks/0", b"\0\xffx", binary)[0] == 200
        assert self.api("POST", small_path + "/seal")[1]["data"]["sha256"] == hashlib.sha256(b"\0\xffx").hexdigest()
        assert self.control("pin/" + small)["bytes"] == 3
        # Retired store remains alive for its pin; releasing it after Init must
        # not remove/unlock the new generation's slot.
        self.control("reset")
        assert self.api("POST", PREFIX, {"id": "c" * 32, "bytes": 1})[0] == 201
        self.control("release")
        assert self.api("GET", PREFIX + "/" + "c" * 32)[0] == 200
        self.control("expire")
        assert self.api("GET", PREFIX + "/" + "c" * 32)[0] == 404
        assert self.api("POST", PREFIX, {"id": small, "bytes": 1, "sha256": "0" * 64})[0] == 201
        assert self.api("PUT", small_path + "/chunks/0", b"x", binary)[0] == 200
        status, body = self.api("POST", small_path + "/seal")
        assert status == 422 and body["error"]["code"] == "backup_upload_checksum"
        assert self.api("GET", small_path)[0] == 404
        assert self.api("POST", PREFIX, {"id": small, "bytes": 1})[0] == 201
        assert self.api("DELETE", small_path)[0] == 200
        assert before == inventory()
        print(f"backup upload {'TLS' if self.secure else 'HTTP'}: exact document/hash, framing, retry/conflict, budget, token, pins, expiry/generation and zero Home writes PASS", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    host = parser.parse_args().host.resolve()
    (ROOT / ".build").mkdir(exist_ok=True)
    for secure in (False, True):
        with tempfile.TemporaryDirectory(prefix="backup-upload-", dir=ROOT / ".build") as raw:
            probe = Probe(host, Path(raw), secure)
            try:
                probe.start()
                probe.check()
            finally:
                probe.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
