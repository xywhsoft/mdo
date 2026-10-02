"""Isolated packed startup UI fixture; release tiny read gates via localhost.

No model requests are made. The initial saved session has no messages. A proxy
can hold one captured response for draft, pane or workspace selection until
POST /__qa/release?gate=NAME. POST /__qa/reset?gates=NAME,NAME arms the next reads.
GET /__qa/control reports arrivals and request counts. The fixture-only refresh
button imports the exact packed navigation module to exercise a data refresh.
"""
from __future__ import annotations

import argparse
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
from urllib.parse import parse_qs, urlsplit

from test_api_runtime import ROOT, free_port, request, wait_ready
from test_interrupt_runtime import stop_host

PATHS = {"draft":"/api/v1/draft", "pane":"/api/v1/pane-layout",
         "selection":"/api/v1/workspace-state"}
REFRESH = b'''<button id="qa-refresh" type="button" style="position:fixed;right:12px;top:48px;z-index:999;min-height:40px">Refresh current data (QA)</button>
<script type="module">import {navigation} from "/js/state/navigation.js";
document.querySelector("#qa-refresh").addEventListener("click",()=>navigation.revalidate());</script>'''


class Proxy(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args): pass

    def do_GET(self): self.forward()
    def do_POST(self): self.forward()
    def do_PUT(self): self.forward()
    def do_DELETE(self): self.forward()

    def reply(self, data):
        payload=json.dumps(data).encode()
        self.send_response(200)
        self.send_header("Content-Type","application/json")
        self.send_header("Content-Length",str(len(payload)))
        self.end_headers(); self.wfile.write(payload)

    def forward(self):
        target=urlsplit(self.path); query=parse_qs(target.query)
        if target.path == "/__qa/control":
            with self.server.lock:
                return self.reply({"gates":{name:{"armed":item["armed"],"arrived":item["arrived"],
                    "released":item["event"].is_set()} for name,item in self.server.gates.items()},
                    "counts":dict(self.server.counts)})
        if target.path == "/__qa/release" and self.command == "POST":
            name=query.get("gate",[""])[0]
            with self.server.lock:
                if name in self.server.gates:self.server.gates[name]["event"].set()
            return self.reply({"released":name})
        if target.path == "/__qa/reset" and self.command == "POST":
            names=query.get("gates",[""])[0].split(",")
            with self.server.lock:
                for name,item in self.server.gates.items():
                    item["event"].set()
                    self.server.gates[name]={"armed":name in names,"arrived":False,"event":threading.Event()}
            return self.reply({"armed":names})
        length=int(self.headers.get("Content-Length","0"))
        body=self.rfile.read(length) if length else None
        headers={key:value for key,value in self.headers.items() if key.lower() not in
                 ("host","connection","content-length")}
        connection=http.client.HTTPConnection("127.0.0.1",self.server.upstream,timeout=8)
        try:
            connection.request(self.command,self.path,body=body,headers=headers)
            response=connection.getresponse(); payload=response.read()
            status=response.status; result_headers=response.getheaders()
        finally: connection.close()
        gate=None
        with self.server.lock:
            key=f"{self.command} {target.path}"
            self.server.counts[key]=self.server.counts.get(key,0)+1
            for name,path in PATHS.items():
                item=self.server.gates[name]
                if self.command == "GET" and target.path == path and item["armed"] and not item["arrived"]:
                    item["arrived"]=True;gate=item["event"];break
        if gate is not None and not gate.wait(60):
            print("QA gate reached its bounded timeout",flush=True)
        if self.command == "GET" and target.path == "/" and status == 200:
            payload=payload.replace(b"</body>",REFRESH+b"</body>",1)
        self.send_response(status)
        for key,value in result_headers:
            if key.lower() not in ("content-length","connection","transfer-encoding"):
                self.send_header(key,value)
        self.send_header("Content-Length",str(len(payload)))
        self.end_headers()
        try:self.wfile.write(payload)
        except (BrokenPipeError,ConnectionResetError):pass


def post(port,path,body):
    status,_,payload=request(port,"POST",path,body=json.dumps(body).encode(),
                             headers={"Content-Type":"application/json"})
    assert status in (200,201),(status,payload)
    return json.loads(payload)["data"]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed-path",type=Path,required=True)
    args=parser.parse_args()
    base=Path(tempfile.mkdtemp(prefix="qa-startup-live-",dir=ROOT/".build"))
    exe=base/("mdo.exe" if os.name=="nt" else "mdo")
    shutil.copy2(args.packed_path,exe)
    workspace=base/"workspace";workspace.mkdir()
    (workspace/"notes.txt").write_text("Startup fixture only",encoding="utf-8")
    port=free_port()
    (base/"xs.json").write_text(json.dumps({"services":[{"enabled":True,"class":"http",
        "name":"mdo","ip":"127.0.0.1","port":port,"host_default":{"enabled":True,
        "name":"mdo","path":"web","devlang":"c","devfile":"generated/mdo_unity.c"}}]}),encoding="utf-8")
    environment=dict(os.environ,USERPROFILE=str(base),HOME=str(base),
        MDO_ORNITH_API_KEY="bounded-startup-fixture-key",MDO_ORNITH_RESPONSES_URL="http://127.0.0.1:1/v1")
    process=None;proxy=None
    try:
        with (base/"packed.log").open("wb") as log:
            process=subprocess.Popen([str(exe),"--","--home",str(base/"mdo-home")],cwd=base,env=environment,
                stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
        wait_ready(port,process)
        session=post(port,"/api/v1/sessions",{"project_id":"default","title":"Saved startup task",
            "agent_id":"mdo.default","model_id":"ornith-1.5-35b","protocol":"openai-responses",
            "reasoning_effort":"medium","max_output_tokens":1024})
        for endpoint,body in [("/workspace-state",{"project_id":"default","session_id":session["id"]}),
            ("/pane-layout",{"sidebar_width":348,"inspector_width":388,"sidebar_open":True,"inspector_open":False})]:
            status,_,payload=request(port,"PUT","/api/v1"+endpoint,body=json.dumps(body).encode(),
                                     headers={"Content-Type":"application/json"})
            assert status==200,(status,payload)
        proxy=ThreadingHTTPServer(("127.0.0.1",0),Proxy);proxy.daemon_threads=True
        proxy.upstream=port;proxy.lock=threading.Lock();proxy.counts={}
        proxy.gates={name:{"armed":name in ("draft","pane"),"arrived":False,"event":threading.Event()} for name in PATHS}
        threading.Thread(target=proxy.serve_forever,daemon=True).start()
        metadata={"base":str(base),"port":proxy.server_address[1],"upstream":port,
                  "session":session["id"],"workspace":str(workspace)}
        (ROOT/".build/qa-startup-live.json").write_text(json.dumps(metadata),encoding="utf-8")
        print("READY "+json.dumps(metadata),flush=True)
        input("Press Enter to stop isolated startup fixture.\n")
    finally:
        if proxy:
            for item in proxy.gates.values():item["event"].set()
            proxy.shutdown();proxy.server_close()
        stop_host(process)


if __name__ == "__main__":main()
