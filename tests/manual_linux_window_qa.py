"""Exercise the real GTK/WebKit process and private command protocol on X11.

Use xvfb-run + dbus-run-session as an ordinary user. No sandbox override.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("helper", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    folder = Path(tempfile.mkdtemp(prefix="mdo-window-qa-"))
    loaded = threading.Event()
    count = [0]

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            if self.path == "/loaded":
                count[0] += 1; loaded.set(); body = b"ok"
            else:
                body = "<!doctype html><meta charset=utf-8><title>墨斗 QA</title><p>GTK/WebKitGTK</p><script>fetch('/loaded')</script>".encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    parent, child = socket.socketpair()
    parent.settimeout(30)
    log = (folder / "window.log").open("wb")
    os.dup2(child.fileno(), 198, inheritable=True)
    process = subprocess.Popen([str(args.helper.resolve()), str(folder / "profile"), "墨斗窗口 QA",
        "800", "600", "360", "320", "0", "stop"], stdout=log, stderr=subprocess.STDOUT,
        pass_fds=(198,))
    os.close(198)
    child.close()

    def send(command, text=""):
        encoded = text.encode()
        frame = command.encode() + struct.pack("!I", len(encoded)) + encoded
        parent.sendall(frame[:2]); parent.sendall(frame[2:])

    try:
        assert parent.recv(1) == b"R", (folder / "window.log").read_text(errors="replace")
        send("N", f"http://127.0.0.1:{server.server_port}/")
        assert loaded.wait(30), (folder / "window.log").read_text(errors="replace")
        # Find only this helper's renderer, including a sandbox parent chain.
        parents = {}; renderers = []
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                pid = int(entry.name)
                status = (entry / "status").read_text()
                ppid = int(next(line.split()[1] for line in status.splitlines() if line.startswith("PPid:")))
                parents[pid] = ppid
                if b"WebKitWebProcess" in (entry / "cmdline").read_bytes():
                    renderers.append(pid)
            except (OSError, StopIteration):
                continue
        renderer = None
        for pid in renderers:
            current = pid
            for _ in range(10):
                current = parents.get(current, 0)
                if current == process.pid:
                    renderer = pid; break
                if not current:
                    break
            if renderer:
                break
        assert renderer, "no renderer belonging to this helper"
        loaded.clear(); os.kill(renderer, signal.SIGKILL)
        assert parent.recv(1) == b"F", "missing renderer termination notification"
        assert loaded.wait(30) and count[0] >= 2, "page did not recover"
        send("E", "<startup> error & 中文")
        send("Q")
        process.wait(timeout=15)
        assert process.returncode == 0, process.returncode
        result = {"passed": True, "helper": str(args.helper), "sandbox_override": False,
                  "checks": ["native GTK window", "fragmented command", "JavaScript execution", "renderer crash/recovery", "error page", "graceful quit"],
                  "folder": str(folder)}
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        return 0
    finally:
        parent.close()
        if process.poll() is None:
            process.terminate(); process.wait(timeout=15)
        server.shutdown(); server.server_close(); log.close()


if __name__ == "__main__":
    raise SystemExit(main())
