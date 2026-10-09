import argparse
import json
from pathlib import Path
import sys
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument("--directory", type=Path, required=True)
args = parser.parse_args()
base = args.directory.resolve()
state = json.loads((base / "state.json").read_text(encoding="utf-8"))
root = Path.cwd()
url = f"http://127.0.0.1:{state['port']}/api/v1"
startup = urllib.request.urlopen(url + "/project-purge-intent", timeout=5)
token = startup.headers["X-Mdo-Write-Token"]
startup.read()
for identifier, program, arguments in (
        ("local", sys.executable, [str(root / "tests/fixtures/mcp-2025.py"), str(base / "mcp-methods.jsonl")]),
        ("missing", "__mdo_missing_mcp_fixture__", [])):
    doc = dict(schema_version=1, id=identifier, name=identifier, description="Disposable local MCP acceptance",
        enabled=True, transport=dict(type="stdio", program=program, arguments=arguments,
            working_directory=str(root), inherit_environment=True, environment=[]),
        protocol_version="2025-11-25", startup_timeout_ms=1000, request_timeout_ms=3000,
        limits=dict(message_bytes=1048576, tools=128), tools=dict(allow=[], deny=[]),
        security=dict(default_effects=["read"], permission_profile="balanced", trust_read_only_annotations=False), auto_reconnect=True)
    request = urllib.request.Request(url + "/extensions/mcp/" + identifier,
        data=json.dumps(dict(content=json.dumps(doc))).encode(), method="PUT",
        headers={"Content-Type": "application/json", "If-Match": '"new"', "X-Mdo-Write-Token": token})
    raw = urllib.request.urlopen(request, timeout=5).read()
    (base / ("setup-" + identifier + ".json")).write_bytes(raw)
    assert json.loads(raw)["data"]["valid"]
print("PASS isolated local and missing-program MCP definitions")
