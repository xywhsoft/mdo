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
        time.sleep(0.4)
        result = {"content": [{"type": "text", "text": "SERIAL_OK"}]}
    else:
        continue
    print(json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": result}), flush=True)
