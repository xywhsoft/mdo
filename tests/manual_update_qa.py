"""Disposable real workbench for updater UI review. Never publishes real packages.

GET the publisher's /__qa/optional|required|offline|revoke|current to change its
manifest, then use the client's Check button or POST /api/v1/update. Install
requests cannot stop this headless host. All files stay in the fixture directory.
"""
import argparse
import json
import shutil
import subprocess
import tempfile
import threading
from pathlib import Path
from http.server import ThreadingHTTPServer
from test_update_runtime import Published, SOURCE, NEXT, ROOT
from test_api_runtime import wait_ready, free_port

class Publisher(Published):
    def do_GET(self):
        if self.path.startswith("/__qa/"):
            state = self.path.rsplit("/", 1)[-1]
            type(self).package = SOURCE if state == "current" else NEXT
            type(self).mode = "offline" if state == "offline" else "normal"
            type(self).required = state in ("required", "offline", "current")
            body = b"ok"
            self.send_response(200); self.send_header("Content-Length", "2"); self.end_headers()
            self.wfile.write(body); return
        super().do_GET()

def run():
    parser = argparse.ArgumentParser(); parser.add_argument("--port", type=int, default=63158)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", free_port()), Publisher)
    Publisher.package = NEXT; Publisher.required = False
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="update-ui-", dir=ROOT / ".build") as raw:
        base = Path(raw); site = base / "site"; home = base / "mdo-home"
        shutil.copytree(ROOT / "app", site)
        source = site / "fixture.exe"; source.write_bytes(SOURCE)
        (site / "probe.c").write_text(
            '#define MDO_UPDATE_ORIGIN "http://127.0.0.1:' + str(server.server_port) + '"\n'
            '#define ServiceInit ProductServiceInit\n#include "generated/mdo_unity.c"\n#undef ServiceInit\n'
            'void ServiceInit(XS_HostInfo* host){ProductServiceInit(host);MdoUpdateUnit();'
            'g_MdoUpdate.Lock=xrtMutexCreate();g_MdoUpdate.Source=xrtStrDup("' + source.as_posix() + '");'
            'g_MdoUpdate.Status.Enabled=true;strcpy(g_MdoUpdate.Status.Platform,"windows-x86_64");'
            'MdoUpdatePolicyLoad(&g_MdoUpdate.Status);g_MdoUpdate.Thread=xrtThreadCreate(MdoUpdateThread,NULL,0);MdoUpdateCheck();}', encoding="utf-8")
        config = dict(engine=dict(workers=2), services=[dict(enabled=True,name="update-ui",ip="127.0.0.1",
            port=args.port, **{"class":"http"}, host_default=dict(enabled=True,name="mdo",path="web",devlang="c",devfile="probe.c"))])
        path = site / "xs.json"; path.write_text(json.dumps(config))
        with (base / "host.log").open("wb") as log:
            proc = subprocess.Popen([str(ROOT / ".build/host/xs.exe"),str(path),"--","--home",str(home)],
                cwd=site,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                wait_ready(args.port,proc)
                print(json.dumps(dict(url=f"http://127.0.0.1:{args.port}",publisher=f"http://127.0.0.1:{server.server_port}",fixture=raw)),flush=True)
                threading.Event().wait()
            except KeyboardInterrupt: pass
            finally:
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired: proc.kill();proc.wait(timeout=5)
    server.shutdown();server.server_close()

if __name__ == "__main__": run()
