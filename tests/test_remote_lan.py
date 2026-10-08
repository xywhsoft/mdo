"""One local authorized peer: real relay, trusted LAN TLS and native WebView gateway.
No public endpoint, model request, stress or high-load traffic.
"""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import tempfile
import time
import uuid
from test_remote_manager import run, WebSocket, PASSWORD
from test_remote_bridge import Rpc
from test_remote_live import LiveRpc
from test_account_runtime import request, free_port, ROOT
from remote_website_fixture import WEBSITE_HOST
SITE = None
HOST = None
def setup(site, home):
    global SITE
    SITE = site

def controller_logout_probe(offer, relay_hello, relay):
    """A separate controller logs out while the target's account stays live."""
    with tempfile.TemporaryDirectory(prefix="lan-controller-",dir=ROOT/".build") as raw:
        directory=Path(raw); source=directory/"site"
        shutil.copytree(SITE,source,ignore=shutil.ignore_patterns("mdo-home","test.log",".cache"))
        port=free_port(); config=json.loads((source/"xs.json").read_text())
        config["services"][0]["port"]=port; (source/"xs.json").write_text(json.dumps(config))
        with (directory/"controller.log").open("wb") as log:
            process=subprocess.Popen([str(HOST),str(source/"xs.json")],cwd=source,
                env={**os.environ,"MDO_HOME":str(directory/"home")},stdout=log,stderr=log)
            gateway=None
            try:
                end=time.monotonic()+12
                while time.monotonic()<end:
                    assert process.poll() is None,"controller host stopped"
                    try:
                        status,headers,_=request(port,"GET","/api/v1/bootstrap")
                        if status==200: break
                    except OSError: pass
                    time.sleep(.05)
                else: raise AssertionError("controller startup deadline")
                token=headers["X-Mdo-Write-Token"]
                assert request(port,"POST","/api/v1/account/login",{
                    "identifier":"remote_manager_one","password":PASSWORD,"remember":False},
                    {"X-Mdo-Write-Token":token})[0]==200
                end=time.monotonic()+12
                while time.monotonic()<end:
                    account=json.loads(request(port,"GET","/api/v1/account")[2])["data"]
                    if account["state"]=="signed_in" and not account["busy"]: break
                    time.sleep(.05)
                else: raise AssertionError("controller login deadline")
                assert not json.loads(request(port,"GET","/api/v1/remote")[2])["data"]["allow_remote"]
                gateway=WebSocket(port,{"Origin":f"http://127.0.0.1:{port}"},path="/api/v1/connector/direct",
                    protocol=f"mdo.direct.v1, mdo.token.{token}")
                gateway.send(1,json.dumps({**offer,"addresses":["127.0.0.1"]}).encode())
                assert json.loads(gateway.recv()[1])["runtime_id"]==relay_hello["runtime_id"]
                assert request(port,"POST","/api/v1/account/logout",{}, {"X-Mdo-Write-Token":token})[0]==200
                try:
                    opcode,_=gateway.recv(); assert opcode==8
                except EOFError: pass
                assert relay.json("GET","/api/v1/projects")[2]==200,"controller logout affected target"
                print("PASS controller-local logout closes its LAN gateway while target and relay remain available")
            finally:
                if gateway: gateway.close()
                process.terminate(); process.wait(timeout=5)
def exercise(*, client, hello, app, device, call, bearer, website_port, clients):
    assert "direct" in hello, "target failed to create ephemeral LAN identity/listener"
    offer = {**hello["direct"], "addresses":["127.0.0.1"]}
    assert offer["version"] == 1 and len(offer["certificate"]) < 1536
    port = json.loads((SITE/"xs.json").read_text())["services"][0]["port"]
    _, headers, _ = request(port,"GET","/api/v1/project-purge-intent")
    token = headers["X-Mdo-Write-Token"]
    origin = f"http://127.0.0.1:{port}"
    WebSocket(port,{"Origin":"http://other.invalid"},path="/api/v1/connector/direct",
        protocol=f"mdo.direct.v1, mdo.token.{token}",expect=403)
    # Invalid trust material and a wrong grant must fail before any API call,
    # without consuming the real grant or changing the relay's authority.
    for invalid in ({**offer,"certificate":"AAAA"},{**offer,"token":"0"*64}):
        denied=WebSocket(port,{"Origin":origin},path="/api/v1/connector/direct",
            protocol=f"mdo.direct.v1, mdo.token.{token}")
        denied.send(1,json.dumps(invalid).encode())
        try:
            opcode,_=denied.recv(); assert opcode == 8
        except EOFError: pass
        denied.close()
    gateway = WebSocket(port,{"Origin":origin},path="/api/v1/connector/direct",
        protocol=f"mdo.direct.v1, mdo.token.{token}")
    clients.append(gateway)
    gateway.send(1,json.dumps(offer).encode())
    ready=json.loads(gateway.recv()[1])
    assert ready["type"] == "direct_ready" and ready["runtime_id"] == hello["runtime_id"],ready
    assert ready["mode"] == "control" and ready["peer_id"] == offer["peer_id"]
    direct=LiveRpc(gateway,hello)
    response, _, status, _=direct.json("GET","/api/v1/projects")
    assert status == 200 and response["ok"]
    # Binary live events and receipt queries share the direct response route.
    # Nine sequential subscriptions cross the eight-slot bookkeeping limit.
    # Each explicit close must reclaim its slot without a terminal reply.
    for _ in range(9):
        direct.live=uuid.uuid4().hex; direct.offset=direct.event_sequence=0
        direct.send({"type":"live_open","id":direct.live,"runtime_id":hello["runtime_id"],"token":token})
        assert Rpc.receive(direct)["type"] == "live_opened"
        assert direct.next_event()["type"] == "ready"
        direct.close_live()
    created,_,status,original=direct.json("POST","/api/v1/projects",{"id":"lan-probe","name":"LAN probe"},
        [["X-Mdo-Write-Token",token]])
    assert status == 201 and created["ok"]
    direct.send({"type":"receipt","id":original["id"],"runtime_id":hello["runtime_id"],
        "client_id":direct.client,"sequence":original["sequence"]})
    receipt=direct.receive()
    assert receipt["type"] == "receipt" and receipt["state"] == "done" and receipt["status"] == 201,receipt
    # The same peer's relay remains usable concurrently. Direct and relay
    # responses must stay on the route where each request was admitted.
    relay=Rpc(client,hello)
    assert relay.json("GET","/api/v1/settings")[2] == 200
    reused=WebSocket(port,{"Origin":origin},path="/api/v1/connector/direct",
        protocol=f"mdo.direct.v1, mdo.token.{token}")
    clients.append(reused); reused.send(1,json.dumps(offer).encode())
    try:
        opcode, _=reused.recv(); assert opcode == 8
    except EOFError: pass
    # Verify a read-only relay grant stays read-only over LAN.
    ticket,_=call(website_port,"POST","/api/v1/devices/ticket",{"device_id":device,"role":"controller","mode":"view"},bearer)
    viewer=WebSocket(website_port,{"Origin":origin},path=ticket["data"]["path"],
        protocol=ticket["data"]["protocol"]+", xadmin.ticket."+ticket["data"]["ticket"])
    clients.append(viewer); viewer.recv(); read_hello=json.loads(viewer.recv()[1])
    readonly=WebSocket(port,{"Origin":origin},path="/api/v1/connector/direct",
        protocol=f"mdo.direct.v1, mdo.token.{token}")
    clients.append(readonly); readonly.send(1,json.dumps({**read_hello["direct"],"addresses":["127.0.0.1"]}).encode())
    assert json.loads(readonly.recv()[1])["mode"] == "view"
    assert Rpc(readonly,read_hello).call("POST","/api/v1/projects",b"{}")[0]["code"] == "read_only"
    viewer.close(); clients.remove(viewer)
    try:
        opcode,_=readonly.recv(); assert opcode == 8
    except EOFError: pass
    readonly.close(); clients.remove(readonly)
    ticket,_=call(website_port,"POST","/api/v1/devices/ticket",{"device_id":device,"role":"controller","mode":"view"},bearer)
    second=WebSocket(website_port,{"Origin":origin},path=ticket["data"]["path"],
        protocol=ticket["data"]["protocol"]+", xadmin.ticket."+ticket["data"]["ticket"])
    clients.append(second); second.recv(); second_hello=json.loads(second.recv()[1])
    controller_logout_probe(second_hello["direct"],second_hello,relay)
    second.close(); clients.remove(second)
    gateway.close(); clients.remove(gateway)
    assert relay.json("GET","/api/v1/projects")[2] == 200, "LAN failure disconnected relay"
    print("PASS standard-chain LAN TLS, invalid trust/grant, origin/token gate, one-use grant, HTTP/live/receipt route isolation, view scope, revoke and relay fallback")
    return client

if __name__ == "__main__":
    parser=argparse.ArgumentParser(); parser.add_argument("--host",type=Path,default=Path(".build/host/xs.exe"))
    parser.add_argument("--website-host",type=Path,default=WEBSITE_HOST); args=parser.parse_args()
    HOST=args.host.resolve()
    run(args.host.resolve(),args.website_host.resolve(),exercise,site_setup=setup)
