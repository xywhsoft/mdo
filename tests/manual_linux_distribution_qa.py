"""Bounded Linux package/update integration using an isolated Home and publisher.

Exercises the real packed xs/TCC host. No real sessions or public publications
are changed. The replacement differs only in an unused ELF identification byte.
"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import threading
import time
from manual_linux_products_qa import request

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary",type=Path);parser.add_argument("--libc",choices=("glibc","musl"),required=True)
    parser.add_argument("--output",type=Path)
    parser.add_argument("--catalog",type=Path,default=ROOT/".build/releases/linux-toolpacks/catalog.json")
    args=parser.parse_args()
    binary=args.binary.resolve();original=binary.read_bytes();changed=bytearray(original);changed[15]^=1;replacement=bytes(changed)
    build_id=json.loads((ROOT/"app/release.json").read_text(encoding="utf-8"))["linux_"+args.libc+"_server_build_id"]
    expected=hashlib.sha256(replacement).hexdigest()
    rows=json.loads(args.catalog.read_text(encoding="utf-8"))["toolpacks"]
    state={"corrupt":False,"mismatch":False}
    class Publisher(BaseHTTPRequestHandler):
        def log_message(self,*_):pass
        def do_GET(self):
            if self.path.startswith("/update/version?"):
                body=dict(platform="linux-x86_64-"+args.libc,edition="desktop" if state["mismatch"] else "server",
                          sha256=expected,size=len(replacement),url="/mdo/blob/"+expected,notes="Linux fixture",build_id=39999999,required=False)
                payload=json.dumps(body).encode()
            elif self.path.startswith("/mdo/catalog"):
                payload=json.dumps(dict(revision=1,notices=[],toolpacks=[{k:v for k,v in r.items() if k!="file"} for r in rows])).encode()
            elif self.path=="/mdo/blob/"+expected:
                payload=bytes(len(replacement)) if state["corrupt"] else replacement
            elif self.path.startswith("/mdo/blob/"):
                row=next((r for r in rows if self.path.endswith(r["sha256"])),None)
                if not row:self.send_error(404);return
                payload=(args.catalog.parent/row["file"]).read_bytes()
            else:self.send_error(404);return
            self.send_response(200);self.send_header("Content-Length",str(len(payload)));self.end_headers()
            self.wfile.write(payload)
    publisher=ThreadingHTTPServer(("127.0.0.1",0),Publisher)
    threading.Thread(target=publisher.serve_forever,daemon=True).start()
    folder=Path(tempfile.mkdtemp(prefix="mdo-linux-distribution-"));home=folder/"home"
    shutil.copytree(ROOT/"app",folder,dirs_exist_ok=True)
    local=folder/"mdo-server";local.write_bytes(original);local.chmod(0o755)
    with socket.socket() as sock:sock.bind(("127.0.0.1",0));port=sock.getsockname()[1]
    source=folder/"generated/mdo_unity.c";origin=f"http://127.0.0.1:{publisher.server_port}"
    source.write_text(f'#define MDO_BUILD_ID {build_id}u\n#define MDO_SERVER_BUILD 1\n#define MDO_PRODUCT_EDITION "server"\n#define MDO_PRODUCT_LIBC "{args.libc}"\n#define MDO_UPDATE_ORIGIN "{origin}"\n#define MDO_DISTRIBUTION_ORIGIN "{origin}"\n#define MDO_DISTRIBUTION_HOST "127.0.0.1"\n#define MDO_DISTRIBUTION_PORT {publisher.server_port}\n#define MDO_DISTRIBUTION_SECURE false\n'+source.read_text(encoding="utf-8"),encoding="utf-8")
    config=json.loads((folder/"xs.json").read_text(encoding="utf-8"))
    config["services"][0].pop("window",None);(folder/"xs.json").write_text(json.dumps(config),encoding="utf-8")
    log_path=folder/"run.log";log=log_path.open("ab");process=None;token=""
    def wait(fn,timeout=90):
        deadline=time.monotonic()+timeout;last=None
        while time.monotonic()<deadline:
            if process and process.poll() is not None:raise AssertionError(log_path.read_text(encoding="utf-8",errors="replace"))
            try:
                last=fn()
                if last:return last
            except (OSError,ValueError):pass
            time.sleep(.15)
        raise AssertionError((last,log_path.read_text(encoding="utf-8",errors="replace")[-3000:]))
    def bootstrap():
        nonlocal token
        status,headers,data=request(port,"GET","/api/v1/bootstrap")
        if status!=200:return False
        token=next((v for k,v in headers.items() if k.lower()=="x-mdo-write-token"),"")
        return data
    def call(path,body=None):
        status,_,value=request(port,"GET" if body is None else "POST","/api/v1/"+path,body,token)
        assert status in (200,202),(path,status,value)
        return value["data"]
    def start():
        nonlocal process
        process=subprocess.Popen([str(local),"--port",str(port),"--","--home",str(home)],cwd=folder,
            env={**os.environ,"MDO_HOME":str(home)},stdout=log,stderr=log)
        wait(bootstrap)
    def update_done():
        value=call("update");return value if not value["busy"] else False
    def dist_done():
        value=call("distribution");return value if not value["busy"] else False
    try:
        start();wait(lambda:call("distribution").get("toolpacks"))
        for package in ("core","python"):
            call("distribution",dict(action="install",id=package));value=wait(dist_done)
            assert value["stage"]=="complete",value
        tools=call("distribution")["tools"]
        assert {t["id"] for t in tools}=={"busybox","curl","jq","ssh","scp","sftp","aria2c","rg","7zip","python"},tools
        assert all(os.access(t["path"],os.X_OK) for t in tools)
        state["mismatch"]=True;call("update",{});value=wait(update_done)
        assert value["status"]=="error",value
        state["mismatch"]=False;call("update",{});value=wait(update_done);assert value["available"],value
        state["corrupt"]=True;call("update/download",{});value=wait(update_done)
        assert not value["ready"] and local.read_bytes()==original,value
        state["corrupt"]=False;call("update/download",{});value=wait(update_done);assert value["ready"],value
        pid=process.pid;call("update/install",{})
        wait(lambda: local.read_bytes()==replacement)
        wait(lambda: bootstrap() and call("update")["local_sha256"]==expected)
        assert process.pid==pid and process.poll() is None,"exec must preserve MainPID"
        assert (home/"data/update/previous.bin").read_bytes()==original
        assert len(call("distribution")["tools"])==10,"installed tools survive update/restart"
        process.send_signal(signal.SIGTERM);process.wait(30);assert process.returncode==0;process=None
        receipt=dict(passed=True,libc=args.libc,binary=str(binary),binary_sha256=hashlib.sha256(original).hexdigest(),home=str(home),log=str(log_path),
            checks=["core+Python SHA256 extraction", "POSIX modes", "10 functional probes", "edition mismatch rejected",
                    "damaged update rejected", "atomic executable replacement", "previous binary retained", "PID-preserving restart", "offline tool persistence", "SIGTERM"])
        if args.output:args.output.write_text(json.dumps(receipt,indent=2)+"\n",encoding="utf-8")
        print(json.dumps(receipt,indent=2))
    finally:
        if process and process.poll() is None:process.terminate();process.wait(30)
        log.close();publisher.shutdown();publisher.server_close()

if __name__=="__main__":main()
