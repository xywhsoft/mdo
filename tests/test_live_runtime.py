"""Bounded WebSocket integration probe through real xs/TCC, with a local model.

No external provider, stress test, or optional Python WebSocket dependency.
Can run against the development host or a freshly packed executable.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_packed_home_lease import free_port, request, stop, wait_bootstrap, release_packed_copies

ROOT = Path(__file__).resolve().parents[1]


class Model(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_POST(self):
        try:
            payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        except OSError:
            return  # Shutdown can cancel an already accepted model request.
        assert self.path == "/v1/chat/completions", self.path
        messages = payload.get("messages", [])
        prompt = json.dumps(messages)
        output = []
        if "live ask" in prompt and not any(item.get("role") == "tool" for item in messages):
            output = [{"tool_calls": [{"index": 0, "id": "ask-live", "type": "function",
                "function": {"name": "ask_user", "arguments": json.dumps({
                    "question": "Live question?", "options": ["Yes", "No"]})}}]}]
            finish = "tool_calls"
        elif "live approval" in prompt and not any(item.get("role") == "tool" for item in messages):
            output = [{"tool_calls": [{"index": 0, "id": "write-live", "type": "function",
                "function": {"name": "write", "arguments": json.dumps({
                    "path": "approval.txt", "content": "permission probe", "mode": "create"})}}]}]
            finish = "tool_calls"
        else:
            output = [{"reasoning_content": "Inspecting live delivery."}]
            output += [{"content": f"part-{index} "} for index in range(12)]
            finish = "stop"
        chunks = [{"role": "assistant"}] + output
        frames = []
        for delta in chunks:
            frames.append(("data: " + json.dumps({"id": "chatcmpl-live", "model": "ornith-1.5-35b",
                "object": "chat.completion.chunk", "choices": [{"index": 0,
                "delta": delta, "finish_reason": None}]}) + "\n\n").encode())
        frames.append(("data: " + json.dumps({"id": "chatcmpl-live", "model": "ornith-1.5-35b",
            "object": "chat.completion.chunk", "choices": [{"index": 0,
            "delta": {}, "finish_reason": finish}],
            "usage": {"prompt_tokens": 100, "completion_tokens": 20, "total_tokens": 120}}) + "\n\n").encode())
        frames.append(b"data: [DONE]\n\n")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(sum(map(len, frames))))
        self.end_headers()
        try:
            for frame in frames:
                self.wfile.write(frame); self.wfile.flush()
                time.sleep(0.08)
        except OSError:
            pass  # Cancellation is an expected transport termination.


class WebSocket:
    def __init__(self, port, *, origin=None, token=None, version="13"):
        self.port = port
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.socket.settimeout(3)
        self.buffer = b""
        if token is None:
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
            connection.request("GET", "/api/v1/project-purge-intent")
            response = connection.getresponse()
            token = response.getheader("X-Mdo-Write-Token")
            response.read(); connection.close()
        key = base64.b64encode(os.urandom(16)).decode()
        self.socket.sendall((f"GET /api/v1/live HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
            f"Origin: {origin or f'http://127.0.0.1:{port}'}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Version: {version}\r\nSec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Protocol: mdo.live.v1, mdo.token.{token}\r\n\r\n").encode())
        while b"\r\n\r\n" not in self.buffer:
            self.buffer += self.socket.recv(4096)
        head, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        self.status = int(head.split(b" ")[1])
        if self.status == 101:
            accept = base64.b64encode(hashlib.sha1((key +
                "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
            assert b"Sec-WebSocket-Accept: " + accept in head, head
            assert b"Sec-WebSocket-Protocol: mdo.live.v1" in head
            assert self.json()["type"] == "ready"

    def read(self, count):
        while len(self.buffer) < count:
            data = self.socket.recv(65536)
            assert data, "WebSocket closed unexpectedly"
            self.buffer += data
        result, self.buffer = self.buffer[:count], self.buffer[count:]
        return result

    def frame(self):
        first, second = self.read(2)
        assert first & 128 and not second & 128, (first, second)
        length = second & 127
        if length == 126: length = struct.unpack("!H", self.read(2))[0]
        if length == 127: length = struct.unpack("!Q", self.read(8))[0]
        assert length <= 256 * 1024
        return first & 15, self.read(length)

    def json(self):
        while True:
            opcode, payload = self.frame()
            assert opcode == 1, (opcode, payload)
            value = json.loads(payload)
            if value["type"] == "ping": self.send({"type": "pong"}); continue
            return value

    def send_frame(self, payload, opcode=1, final=True, masked=True):
        if len(payload) < 126: length = bytes([len(payload) | (128 if masked else 0)])
        else: length = bytes([126 | (128 if masked else 0)]) + struct.pack("!H", len(payload))
        mask = os.urandom(4) if masked else b""
        body = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload)) if masked else payload
        self.socket.sendall(bytes([opcode | (128 if final else 0)]) + length + mask + body)

    def send(self, value):
        self.send_frame(json.dumps(value).encode())

    def select(self, session, after=0, selection=1):
        self.send({"type": "subscribe", "project_id": "default", "session_id": session,
            "after": after, "selection": selection})

    def close(self):
        self.socket.close()


def probe(host: Path | None, packed: Path | None):
    server = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    with tempfile.TemporaryDirectory(prefix="live-runtime-", dir=ROOT / ".build") as temp:
        base = Path(temp); site = base / "site"; home = base / "home"
        port = free_port(); sockets = []; process = None
        if packed:
            site.mkdir(); shutil.copy2(packed, site / packed.name)
            command = [str(site / packed.name), "--", "--home", str(home)]
        else:
            shutil.copytree(ROOT / "app", site)
            # Lower only fixture capacities/timeouts to exercise eviction and
            # heartbeat deterministically with an ordinary twelve-chunk reply.
            live = site / "src/api/live.c"
            source = live.read_text(encoding="utf-8").replace("#define MDO_LIVE_RECORDS 512u", "#define MDO_LIVE_RECORDS 1u")
            source = source.replace("#define MDO_LIVE_PING_US 20000000u", "#define MDO_LIVE_PING_US 200000u")
            source = source.replace("#define MDO_LIVE_IDLE_US 60000000u", "#define MDO_LIVE_IDLE_US 800000u")
            source = source.replace("Client->ReplayFirst = true; Reply = true;", "Client->ReplayFirst = true; Reply = true; printf(\"[qa-live] cache replay\\n\"); fflush(stdout);")
            live.write_text(source, encoding="utf-8")
            command = [str(host), str(site / "xs.json"), "--", "--home", str(home)]
        (site / "xs.json").write_text(json.dumps({"services": [{"class": "http", "name": "mdo",
            "ip": "127.0.0.1", "port": port, "enabled": True, "recv_limit": 8454144,
            "host_default": {"name": "mdo", "enabled": True, "path": "web",
            "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}), encoding="utf-8")
        env = os.environ.copy()
        endpoint = f"http://127.0.0.1:{server.server_port}/v1"
        env.update(MDO_ORNITH_API_KEY="live-test-key", MDO_ORNITH_CHAT_COMPLETIONS_URL=endpoint,
            MDO_ORNITH_RESPONSES_URL=endpoint, MDO_ORNITH_ANTHROPIC_URL="https://example.invalid")
        def connect(**kwargs):
            client = WebSocket(port, **kwargs); sockets.append(client); return client
        try:
            with (site / "native.log").open("wb") as log:
                process = subprocess.Popen(command, cwd=site, env=env, stdout=log, stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            status, document = wait_bootstrap(process, port, site / "native.log")
            assert status == 200 and document["data"]["ready"], document
            assert connect(origin="https://example.invalid").status == 403
            assert connect(token="incorrect").status == 403
            assert connect(version="12").status == 400
            for client in sockets: client.close()
            sockets.clear()
            assert not home.exists(), "read-only WebSocket check created portable Home"
            status, document = request(port, "POST", "/api/v1/sessions", {
                "project_id": "default", "title": "Live probe", "agent_id": "mdo.default",
                "model_id": "ornith-1.5-35b", "protocol": "openai-chat-completions",
                "reasoning_effort": "medium", "permission_profile": "read-only", "max_output_tokens": 1024})
            assert status == 201, document
            session = document["data"]["id"]; path = f"/api/v1/projects/default/sessions/{session}"
            client = connect(); assert client.status == 101
            client.select(session)
            initial = client.json(); assert initial["type"] == "events" and initial["next_cursor"] == 0
            # Interleaved ping inside a fragmented subscription exercises xrt's
            # message state machine rather than a home-grown frame parser.
            message = json.dumps({"type": "subscribe", "project_id": "default", "session_id": session,
                "after": 0, "selection": 2}).encode()
            client.send_frame(message[:20], final=False)
            client.send_frame(b"control-probe", opcode=9)
            client.send_frame(message[20:], opcode=0)
            assert client.frame() == (10, b"control-probe")
            assert client.json()["selection"] == 2
            status, document = request(port, "POST", path + "/runs", {"prompt": "live stream", "timeout_ms": 10000})
            assert status == 202, document
            run_id = document["data"]["id"]
            delivered = []; first_partial = None; changed = False
            deadline = time.monotonic() + 6
            while time.monotonic() < deadline:
                packet = client.json()
                if packet["type"] == "changed": changed = True; continue
                assert packet["type"] == "events", packet
                delivered += packet["items"]
                if any(item["kind"] == "model_text_delta" for item in packet["items"]) and first_partial is None:
                    first_partial = time.monotonic()
                    # Disconnect in the middle of a reply and resume from the
                    # last displayed ID. The same run keeps executing.
                    cursor = packet["next_cursor"]; client.close()
                    time.sleep(0.18)
                    client = connect(); client.select(session, cursor, selection=3)
                if any(item["kind"] == "agent_done" for item in packet["items"]): break
            else: raise AssertionError("live stream did not complete")
            assert first_partial is not None and time.monotonic() - first_partial > 0.4, "no partial delivery before completion"
            assert changed, "run/resource notification missing"
            ids = [item["event_id"] for item in delivered]
            assert ids == sorted(set(ids)), ids
            text = "".join(item["text"] for item in delivered if item["kind"] == "model_text_delta")
            assert text == "".join(f"part-{index} " for index in range(12)), text
            assert any(item["kind"] == "model_reasoning_delta" for item in delivered)
            _, document = request(port, "GET", path + "/events?after=0&limit=32")
            assert document["data"]["items"] == delivered, "WebSocket and HTTP schemas diverged"
            # Completion is announced independently of the final token callback.
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                packet = client.json()
                if packet["type"] != "changed": continue
                _, document = request(port, "GET", f"/api/v1/runs/{run_id}")
                if document["data"]["terminal"]: break
            else: raise AssertionError("terminal run change was not announced")
            # Ask appears through invalidation even while the model/tool is
            # waiting and no new text event can wake a legacy polling loop.
            status, document = request(port, "POST", path + "/runs", {"prompt": "live ask", "timeout_ms": 10000})
            assert status == 202, document
            deadline = time.monotonic() + 4; question = None
            while time.monotonic() < deadline:
                if client.json()["type"] != "changed": continue
                _, document = request(port, "GET", path + "/asks")
                if document["data"]["items"]:
                    question = document["data"]["items"][0]; break
            assert question and question["question"] == "Live question?", question
            status, document = request(port, "PUT", path + f"/asks/{question['id']}", {"answer": "Yes"})
            assert status in (200, 202), document
            status, document = request(port, "POST", "/api/v1/sessions", {
                "project_id": "default", "title": "Live permission probe", "agent_id": "mdo.default",
                "model_id": "ornith-1.5-35b", "protocol": "openai-chat-completions",
                "reasoning_effort": "medium", "permission_profile": "balanced", "max_output_tokens": 1024})
            assert status == 201, document
            approval_session = document["data"]["id"]
            client.select(approval_session, selection=4)
            status, document = request(port, "POST",
                f"/api/v1/projects/default/sessions/{approval_session}/runs",
                {"prompt": "live approval", "timeout_ms": 10000})
            assert status == 202, document
            deadline = time.monotonic() + 4; approval = None
            while time.monotonic() < deadline:
                if client.json()["type"] != "changed": continue
                _, document = request(port, "GET", "/api/v1/approvals")
                if document["data"]["items"]:
                    approval = document["data"]["items"][0]; break
            assert approval and approval["tool"] == "write", approval
            status, document = request(port, "PUT", f"/api/v1/approvals/{approval['id']}", {"decision": "deny"})
            assert status == 200, document
            assert not (home / "workspace/approval.txt").exists()
            # Bounded malformed-frame rejection and orderly connection close.
            bad = connect(); bad.send_frame(b"{}", masked=False)
            opcode, payload = bad.frame(); assert opcode == 8 and struct.unpack("!H", payload[:2])[0] == 1002
            closing = connect(); closing.send_frame(struct.pack("!H", 1000), opcode=8)
            assert closing.frame() == (8, struct.pack("!H", 1000))
            if not packed:
                idle = connect()
                deadline = time.monotonic() + 2
                saw_ping = False
                while time.monotonic() < deadline:
                    opcode, payload = idle.frame()
                    if opcode == 8:
                        assert struct.unpack("!H", payload[:2])[0] == 1001
                        break
                    saw_ping |= json.loads(payload)["type"] == "ping"
                else: raise AssertionError("idle heartbeat connection was not released")
                assert saw_ping
                assert "[qa-live] cache replay" in (site / "native.log").read_text(errors="replace")
            print("PASS WebSocket: auth, fragmentation/ping, partial stream, cursor reconnect, HTTP parity, run completion, ask, approval, close")
        except BaseException:
            print((site / "native.log").read_text(encoding="utf-8", errors="replace")[-6000:])
            raise
        finally:
            for client in sockets: client.close()
            if process is not None: stop(process)
            if packed: release_packed_copies(site / packed.name)
            server.shutdown(); server.server_close(); thread.join(timeout=3)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--host", type=Path)
    group.add_argument("--packed", type=Path)
    args = parser.parse_args()
    probe(args.host.resolve() if args.host else None, args.packed.resolve() if args.packed else None)
