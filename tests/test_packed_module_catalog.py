"""The real packed default Home publishes useful modules, not ABI test tools."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import tempfile
import threading
import time
from http.server import ThreadingHTTPServer

from test_packed_home_lease import (
    ROOT, release_packed_copies, request, site, start, stop, wait_bootstrap,
)
from test_api_runtime import request as raw_request, ModelHandler
from test_packed_queue_start_recovery import configure_model


class ExampleModel(ModelHandler):
    protocol_version = "HTTP/1.0"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path,
                        default=ROOT / ("mdo.exe" if os.name == "nt" else "mdo"))
    args = parser.parse_args()
    packed = args.packed.resolve()
    with tempfile.TemporaryDirectory(prefix="module-packed-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        directory, port = site(base, "site", packed)
        process = start(directory, packed, base / "home", dict(os.environ, USE_WEBVIEW="0",MDO_QUEUE_FIXTURE_KEY="fixture-only"))
        model = None
        try:
            bootstrap = wait_bootstrap(process, port, directory / "packed.log")[1]
            assert bootstrap["data"]["ready"], bootstrap
            status, envelope = request(port, "GET", "/api/v1/modules")
            assert status == 200, envelope
            data = envelope["data"]
            tools = {tool["id"] for tool in data["tools"]}
            assert "mdo.echo" not in tools, "test module leaked into the production catalog"
            assert "mdo.todo" not in tools, data
            status, envelope = request(port, "GET", "/api/v1/tools")
            assert status == 200, envelope
            builtins = {tool["id"] for tool in envelope["data"]["items"]}
            assert "mdo.todo" not in builtins, envelope
            assert {"read", "ls", "glob", "grep", "write", "edit", "exec", "spawn", "poll", "wait", "stdin", "stop", "ask_user", "skill", "agent"} <= builtins, envelope
            status, envelope = request(port, "GET", "/api/v1/agents")
            assert status == 200, envelope
            agents = {agent["id"] for agent in envelope["data"]["items"]}
            assert "mdo.default" in agents, envelope
            print("PASS packed default Agent and built-ins; no Todo/Echo example bundled")
            resource = "/api/v1/extensions/tools/todo-example"
            source = (ROOT / "examples/tools/todo.c").read_text(encoding="utf-8")
            status, _, raw = raw_request(port, "PUT", resource,
                body=json.dumps({"content":source}).encode(),
                headers={"Content-Type":"application/json", "If-Match":'"new"'})
            installed = json.loads(raw)
            assert status == 200 and installed["data"]["valid"] and installed["data"]["external"], installed
            revision = installed["data"]["revision"]
            assert [tool["id"] for tool in installed["data"]["tools"]] == ["mdo.todo"], installed
            status, envelope = request(port, "GET", "/api/v1/tools")
            todo = next(tool for tool in envelope["data"]["items"] if tool["id"]=="mdo.todo")
            assert status==200 and todo["source"]=="c", envelope
            def call(method, params):
                status, result = request(port,"POST","/api/v1/application/call",{"method":method,"params":params,"context":{"project":"default"}})
                assert status==200,result
                return result["data"]
            ModelHandler.calls=10;ModelHandler.todo_sent=False
            model=ThreadingHTTPServer(('127.0.0.1',0),ExampleModel)
            model_thread=threading.Thread(target=model.serve_forever,daemon=True);model_thread.start()
            configure_model(port,f'http://127.0.0.1:{model.server_port}/v1/responses','openai-responses')
            session = call("session.create",{"body":{"project_id":"default","title":"Optional Todo example","model_id":"queue-fixture","protocol":"openai-responses"}})["data"]["id"]
            status, result=request(port,"POST",f"/api/v1/projects/default/sessions/{session}/runs",{"prompt":"TODO probe","timeout_ms":10000})
            assert status==202,result
            run=result['data'];deadline=time.monotonic()+15
            while not run['terminal'] and time.monotonic()<deadline:
                time.sleep(.05);status,result=request(port,"GET","/api/v1/runs/"+run['id']);run=result['data']
            assert status==200 and run['state']=='succeeded' and ModelHandler.todo_sent,result
            items = [{"text":"Inspect repository","done":True},{"text":"Verify result","done":False}]
            todo_path = f"/api/v1/projects/default/sessions/{session}/todo"
            status, plan = request(port,"GET",todo_path)
            assert status==200 and plan["data"]["items"]==items,plan
            status, _, raw = raw_request(port,"DELETE",resource,headers={"If-Match":'"'+revision+'"'})
            assert status==200,raw
            status, envelope = request(port,"GET","/api/v1/tools")
            assert status==200 and all(tool["id"]!="mdo.todo" for tool in envelope["data"]["items"]),envelope
            status, preserved = request(port,"GET",todo_path)
            assert status==200 and preserved["data"]==plan["data"],preserved
            print("PASS optional Todo import, model tool execution, plan projection, deletion and preserved history")
        finally:
            stop(process)
            if model is not None:model.shutdown();model.server_close();model_thread.join(timeout=5)
            release_packed_copies(directory / packed.name)


if __name__ == "__main__":
    main()
