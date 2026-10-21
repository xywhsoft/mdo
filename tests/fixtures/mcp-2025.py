"""Local MCP 2025-11-25 stdio fixture; stdout contains JSON-RPC only."""
import json
from pathlib import Path
import sys


initialized = False
record = Path(sys.argv[1]) if len(sys.argv) > 1 else None
interactive=len(sys.argv)>2 and sys.argv[2]=="interactive"
for line in sys.stdin:
    request = json.loads(line)
    method = request.get("method")
    if record is not None:
        with record.open("a", encoding="utf-8") as output:
            output.write(json.dumps({"method": method,
                "params": request.get("params", {})}) + "\n")
    if method == "initialize":
        assert request["params"]["protocolVersion"] == "2025-11-25"
        result = {"protocolVersion": "2025-11-25", "capabilities": {"tools": {}},
            "serverInfo": {"name": "bounded-echo", "version": "1.0.0"}}
    elif method == "notifications/initialized":
        initialized = True
        continue
    elif method == "tools/list":
        assert initialized
        result = {"tools": [{"name": "echo", "description": "Echo fixture text.",
            "inputSchema": {"type": "object", "properties": {
                "text": {"type": "string"}}, "required": ["text"],
                "additionalProperties": False}, "annotations": {"readOnlyHint": True}}]}
    elif method == "tools/call":
        assert initialized and request["params"]["name"] == "echo"
        if interactive and request['params']['arguments']['text']=='MCP_CALLBACK_RESULT':
            print(json.dumps({'jsonrpc':'2.0','id':'roots-fixture','method':'roots/list','params':{}}),flush=True)
            roots=json.loads(sys.stdin.readline())
            assert len(roots['result']['roots'])==1 and roots['result']['roots'][0]['uri'].startswith('file://'),roots
            print(json.dumps({'jsonrpc':'2.0','id':'form-fixture','method':'elicitation/create','params':{
                'message':'Confirm the fixture name','requestedSchema':{'type':'object','properties':{'name':{'type':'string','enum':['fixture']}},'required':['name']}}}),flush=True)
            answer=json.loads(sys.stdin.readline())
            assert answer['result']=={'action':'accept','content':{'name':'fixture'}},answer
            print(json.dumps({'jsonrpc':'2.0','method':'notifications/progress','params':{
                'progressToken':request['params']['_meta']['progressToken'],'progress':1,'total':1,'message':'Fixture complete'}}),flush=True)
            if record: record.with_suffix('.interaction.json').write_text(json.dumps({'roots':roots['result']['roots'],'accepted':True}))
        result = {"content": [{"type": "text",
            "text": request["params"]["arguments"]["text"]}], "isError": False}
    elif "id" not in request:
        continue
    else:
        print(json.dumps({"jsonrpc": "2.0", "id": request["id"],
            "error": {"code": -32601, "message": "Method not found"}}), flush=True)
        continue
    print(json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": result}), flush=True)
