"""Offline stdio peer for admission, capabilities and malformed-tool tests."""
import json
import sys
import time

mode = sys.argv[1]
for line in sys.stdin:
    request = json.loads(line)
    method = request.get("method")
    if method == "initialize":
        capabilities = {"resources": {}} if mode == "resources" else {"tools": {"listChanged": True}}
        result = {"protocolVersion": "2025-11-25", "capabilities": capabilities,
                  "instructions": "Use the valid echo tool for this server.",
                  "serverInfo": {"name": "reliability", "version": "1"}}
    elif method == "tools/list":
        if mode == "resources":
            raise RuntimeError("resources-only peer received tools/list")
        result = {"tools": [{"name": "echo", "description": "echo",
                             "inputSchema": {"type": "object"}},
                            {"name": "bad", "inputSchema": "invalid"},
                            {"name": "echo", "inputSchema": {"type": "object"}},
                            {"name": "badroot", "inputSchema": {"type": "array"}}]}
    elif method == "tools/call":
        if mode!='image': time.sleep(0.4)
        result = {"content": [{"type": "text", "text": "SERIAL_OK"}]}
        if mode=='image':
            result['content'] += [{'type':'image','mimeType':'image/png','data':'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aV9kAAAAASUVORK5CYII='},
                {'type':'resource','resource':{'uri':'fixture:///binary','blob':'BINARY_MUST_NOT_ENTER_CONTEXT'}}]
            result['structuredContent']={'ok':True}
    else:
        continue
    print(json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": result}), flush=True)
