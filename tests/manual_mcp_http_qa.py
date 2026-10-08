"""Opt-in UI fixture for one public HTTP MCP call, using a loopback model.

Run with the ordinary manual_conversation_retry_qa arguments and balanced
permission. Configure a DeepWiki MCP entry named deepwiki in this disposable
Home, then send mcp-http from the page. Its only external tool request reads
the structure of the public modelcontextprotocol/python-sdk repository.
No model request, workspace content or credentials leave loopback.
"""
import json

import manual_conversation_retry_qa as fixture


class Model(fixture.Model):
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        user = next(row for row in reversed(body["messages"]) if row["role"] == "user")
        assert user["content"] == "mcp-http", "only the explicit MCP acceptance prompt is supported"
        tools = [tool["function"]["name"] for tool in body.get("tools", [])]
        with self.lock:
            step = self.calls["mcp-http"] = self.calls.get("mcp-http", 0) + 1
            assert step <= 4, "no model turn may be replayed"
            self.markers.append({"step": step, "tool_names": tools,
                "tool_results": sum(row["role"] == "tool" for row in body["messages"]),
                "user_messages": sum(row["role"] == "user" for row in body["messages"])})
            for name, data in (("calls.json", self.calls), ("requests.json", self.markers)):
                pending = self.workspace / (name + ".pending")
                pending.write_text(json.dumps(data, indent=2), encoding="utf-8")
                pending.replace(self.workspace / name)
        if step <= 2:
            assert not any(name.startswith("mcp__") for name in tools), "remote schemas stay lazy"
            name = "tool_search" if step == 1 else "tool_load"
            arguments = {"server": "deepwiki", "query": "read_wiki_structure"} if step == 1 else {
                "server": "deepwiki", "tool": "read_wiki_structure"}
        elif step == 3:
            loaded = [name for name in tools if name.startswith("mcp__")]
            assert len(loaded) == 1 and loaded[0].endswith("__read_wiki_structure")
            name, arguments = loaded[0], {"repoName": "modelcontextprotocol/python-sdk"}
        if step <= 3:
            delta = {"role": "assistant", "tool_calls": [{"index": 0,
                "id": f"http-mcp-{step}", "type": "function", "function": {
                    "name": name, "arguments": json.dumps(arguments)}}]}
            reason = "tool_calls"
        else:
            assert self.markers[-1]["tool_results"] == 3
            delta = {"role": "assistant", "content": "HTTP_MCP_ACCEPTANCE_COMPLETED"}
            reason = "stop"
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        for value in ({"id": "http-mcp-fixture", "model": "conversation-fixture",
            "choices": [{"index": 0, "delta": delta}]}, {
            "id": "http-mcp-fixture", "model": "conversation-fixture", "choices": [
                {"index": 0, "delta": {}, "finish_reason": reason}],
            "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15}}):
            self.wfile.write(("data: " + json.dumps(value) + "\n\n").encode())
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()
        self.close_connection = True


if __name__ == "__main__":
    fixture.Model = Model
    fixture.main()
