"""Bounded local TLS requests through both model proxy transports.

The checked-in CA, server certificate, and private key belong only to this
localhost fixture. No request leaves the loopback interface.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import select
import shutil
import socket
import socketserver
import ssl
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from test_model_runtime import ROOT, run_probe, write_site


PROBE_SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"

void ServiceInit(XS_HostInfo* host)
{
    MdoModelCatalog* catalog = NULL;
    MdoModelClientOptions options;
    xllm_client* client = NULL;
    xllm_request request;
    xllm_response* response = NULL;
    xllm_error error;
    xllm_result result;
    (void)host;
    if ( !MdoHomeInit() || !MdoConfigInit() || !MdoModelManagerInit() ) {
        printf("proxy_init=0\nprobe_done=1\n");
        return;
    }
    catalog = MdoModelCatalogSnapshot();
    MdoModelClientOptionsInit(&options);
    options.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    xllmErrorInit(&error);
    client = MdoModelClientCreate(catalog, &options, NULL, &error);
    if ( client == NULL ) {
        printf("proxy_client=0 error=%s\nprobe_done=1\n", error.sMessage);
        goto done;
    }
    xllmRequestInit(&request);
    if ( !xllmRequestAddTextMessage(&request, XLLM_ROLE_USER, "proxy probe") ) {
        printf("proxy_request=0\nprobe_done=1\n");
        xllmRequestUnit(&request);
        goto done;
    }
    result = xllmClientComplete(client, &request, NULL, &response, &error);
    printf("proxy_result=%d status=%u error=%s\nprobe_done=1\n",
        (int)result, response != NULL ? (unsigned)response->uHttpStatus : 0u,
        error.sMessage[0] != '\0' ? error.sMessage : "none");
    if ( response != NULL ) xllmResponseDestroy(response);
    xllmRequestUnit(&request);
done:
    if ( client != NULL ) xllmClientDestroy(client);
    MdoModelCatalogRelease(catalog);
}

void ServiceUnit(XS_HostInfo* host)
{
    (void)host;
    MdoModelManagerUnit();
    MdoConfigUnit();
    MdoHomeUnit();
}
'''


class ModelHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args: object) -> None:
        pass

    def do_POST(self) -> None:
        size = int(self.headers.get("Content-Length", "0"))
        if self.path != "/v1/responses" or size > 65536:
            self.send_error(400)
            return
        payload = json.loads(self.rfile.read(size))
        assert payload.get("model") == "ornith-1.5-35b", payload
        self.server.hits += 1  # type: ignore[attr-defined]
        body = json.dumps({
            "id": "proxy-local-fixture", "model": "ornith-1.5-35b",
            "status": "completed", "output": [{"type": "message", "content": [
                {"type": "output_text", "text": "Local proxy request completed."},
            ]}], "usage": {"input_tokens": 2, "output_tokens": 3,
                           "total_tokens": 5},
        }).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def read_exact(connection: socket.socket, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        part = connection.recv(size - len(result))
        if not part:
            raise ConnectionError("proxy peer closed early")
        result.extend(part)
    return bytes(result)


def tunnel(client: socket.socket, upstream: socket.socket) -> None:
    sockets = (client, upstream)
    while True:
        readable, _, _ = select.select(sockets, [], [], 5.0)
        if not readable:
            raise TimeoutError("bounded proxy tunnel timed out")
        for source in readable:
            try:
                data = source.recv(65536)
            except ConnectionResetError:
                return
            if not data:
                return
            destination = upstream if source is client else client
            destination.sendall(data)


class ProxyHandler(socketserver.BaseRequestHandler):
    def handle(self) -> None:
        server = self.server
        client = self.request
        client.settimeout(5.0)
        try:
            if server.kind == "http-connect":
                header = bytearray()
                while not header.endswith(b"\r\n\r\n") and len(header) < 8192:
                    header.extend(read_exact(client, 1))
                lines = header.decode("ascii").split("\r\n")
                target = f"127.0.0.1:{server.target_port}"
                token = base64.b64encode(b"probe:bounded-password").decode()
                assert lines[0] == f"CONNECT {target} HTTP/1.1", lines[0]
                assert any(line.lower() == f"proxy-authorization: basic {token}".lower()
                           for line in lines), lines
                upstream = socket.create_connection(("127.0.0.1", server.target_port), 5)
                client.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
            else:
                version, count = read_exact(client, 2)
                methods = read_exact(client, count)
                assert version == 5 and 2 in methods, methods
                client.sendall(b"\x05\x02")
                assert read_exact(client, 1) == b"\x01"
                username = read_exact(client, read_exact(client, 1)[0])
                password = read_exact(client, read_exact(client, 1)[0])
                assert (username, password) == (b"probe", b"bounded-password")
                client.sendall(b"\x01\x00")
                version, command, _, address_type = read_exact(client, 4)
                assert version == 5 and command == 1
                if address_type == 1:
                    address = socket.inet_ntoa(read_exact(client, 4))
                elif address_type == 3:
                    address = read_exact(client, read_exact(client, 1)[0]).decode()
                else:
                    raise AssertionError(f"unsupported SOCKS address type {address_type}")
                port = int.from_bytes(read_exact(client, 2), "big")
                assert address == "127.0.0.1" and port == server.target_port
                upstream = socket.create_connection(("127.0.0.1", port), 5)
                client.sendall(b"\x05\x00\x00\x01\x7f\x00\x00\x01" + port.to_bytes(2, "big"))
            server.connections += 1
            with upstream:
                client.settimeout(None)
                upstream.settimeout(None)
                tunnel(client, upstream)
        except Exception as error:
            server.errors.append(str(error))


class LocalProxy(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host" /
                        ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="model-proxy-runtime-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        write_site(site)
        (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
        model = ThreadingHTTPServer(("127.0.0.1", 0), ModelHandler)
        model.daemon_threads = True
        model.hits = 0
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(ROOT / "tests/fixtures/proxy-localhost-cert.pem",
                            ROOT / "tests/fixtures/proxy-localhost-key.pem")
        model.socket = tls.wrap_socket(model.socket, server_side=True)
        model_thread = threading.Thread(target=model.serve_forever, daemon=True)
        model_thread.start()
        try:
            for kind, bypass in (("http-connect", ""), ("socks5", ""),
                                 ("socks5", "127.0.0.1")):
                proxy = LocalProxy(("127.0.0.1", 0), ProxyHandler)
                proxy.kind = kind
                proxy.target_port = model.server_port
                proxy.connections = 0
                proxy.errors = []
                thread = threading.Thread(target=proxy.serve_forever, daemon=True)
                thread.start()
                try:
                    home = base / f"{kind}-{bypass or 'routed'}"
                    (home / "config").mkdir(parents=True)
                    (home / "certs").mkdir()
                    shutil.copy2(ROOT / "tests/fixtures/proxy-localhost-ca.pem",
                                 home / "certs/local.pem")
                    (home / "config/settings.json").write_text(json.dumps({
                        "schema_version": 1, "patch": {"transport": {
                            "ca_pem_path": "certs/local.pem",
                            "proxy": {"kind": kind, "host": "127.0.0.1",
                                      "port": proxy.server_address[1],
                                      "user": "probe", "bypass": bypass,
                                      "credential": {"secret_ref":
                                                     "env:MDO_TEST_PROXY_PASSWORD"}},
                        }},
                    }), encoding="utf-8")
                    before = model.hits
                    output = run_probe(host, site, home, {
                        "MDO_ORNITH_RESPONSES_URL": f"https://127.0.0.1:{model.server_port}/v1",
                        "MDO_ORNITH_API_KEY": "bounded-local-model-key",
                        "MDO_TEST_PROXY_PASSWORD": "bounded-password",
                    })
                    assert "proxy_result=0 status=200" in output, (
                        kind, bypass, proxy.connections, proxy.errors,
                        model.hits, output)
                    assert model.hits == before + 1, (model.hits, before, output)
                    assert proxy.connections == (0 if bypass else 1), (
                        kind, bypass, proxy.connections, proxy.errors, output)
                    assert not proxy.errors, proxy.errors
                finally:
                    proxy.shutdown()
                    proxy.server_close()
                    thread.join(timeout=3)
        finally:
            model.shutdown()
            model.server_close()
            model_thread.join(timeout=3)
    print("model proxy TLS runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
