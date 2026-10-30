"""Current warnings and retired diagnostics/migration UI against a packed app."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import tempfile
import urllib.error
import urllib.request

from test_packed_home_lease import ROOT, site, start, stop, wait_bootstrap, request, release_packed_copies


def check(packed,record=None):
    results=[]
    for mode in ("normal","invalid-files"):
        with tempfile.TemporaryDirectory(prefix="diagnostic-retirement-",dir=ROOT/".build") as raw:
            base=Path(raw);runtime,port=site(base,"runtime",packed);home=base/"home"
            if mode=="invalid-files":
                home.mkdir()
            env={**os.environ,"USE_WEBVIEW":"0","USERPROFILE":str(base),"HOME":str(base)}
            process=start(runtime,packed,home,env)
            try:
                _,boot=wait_bootstrap(process,port,runtime/"packed.log");assert boot["data"]["ready"],boot
                if mode=="invalid-files":
                    knowledge=home/"knowledge"
                    if knowledge.is_dir(): knowledge.rmdir()
                    knowledge.write_bytes(b"retain-conflicting-file")
                    broken=home/"sessions/default/broken";broken.mkdir(parents=True)
                    (broken/"meta.json").write_bytes(b"{invalid")
                status,reply=request(port,"GET","/api/v1/diagnostics");assert status==200,reply
                data=reply["data"];assert isinstance(data["items"],list) and data["total"]>=len(data["items"]),data
                if mode=="invalid-files":
                    assert {"knowledge","sessions"}.issubset({row["domain"] for row in data["items"]}),data
                    assert (home/"knowledge").read_bytes()==b"retain-conflicting-file"
                    assert (home/"sessions/default/broken/meta.json").read_bytes()==b"{invalid"
                for endpoint in ("storage","migrations/legacy"):
                    for method in ("GET","HEAD","OPTIONS","POST"):
                        req=urllib.request.Request(f"http://127.0.0.1:{port}/api/v1/{endpoint}",method=method)
                        try:urllib.request.urlopen(req,timeout=3);raise AssertionError((method,endpoint,"not retired"))
                        except urllib.error.HTTPError as error:
                            assert error.code==404 and not error.headers.get("Allow"),(method,endpoint,error.code)
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/",timeout=3) as r:html=r.read().decode()
                assert 'data-settings-section="diagnostics"' not in html
                assert 'data-settings-panel="diagnostics"' not in html
                assert 'data-notifications-open' in html
                results.append({"mode":mode,"domains":sorted({row["domain"] for row in data["items"]}),"retired_routes":True})
                print(mode+": PASS",flush=True)
            finally:stop(process);release_packed_copies(runtime/packed.name)
    if record:record.write_text(json.dumps(results,indent=2)+"\n",encoding="utf-8")


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--packed",type=Path,required=True)
    parser.add_argument("--record",type=Path);args=parser.parse_args();check(args.packed.resolve(),args.record)
