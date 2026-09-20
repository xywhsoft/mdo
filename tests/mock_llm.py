# mock_llm.py — mdo 全链路冒烟用的本地 OpenAI 兼容 mock
#
# 行为脚本（无状态、按消息形状分派）：
#   请求消息里没有 role:"tool"   → 流式返回一个 exec 工具调用（finish=tool_calls）
#   请求消息里有 role:"tool"      → 流式返回 reasoning + 正文（finish=stop）+ usage
#
# 用法：python mock_llm.py [port]   （默认 8099，仅 127.0.0.1）
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8099

TOOL_CALL_ARGS = json.dumps({"argv": ["cmd", "/c", "echo", "mdo-smoke-ok"]})
FINAL_TEXT = "墨斗全链路冒烟通过：工具执行成功。"


def sse(obj):
    return b"data: " + json.dumps(obj, ensure_ascii=False).encode("utf-8") + b"\n\n"


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def _json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/v1/chat/completions":
            self._json(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length", 0))
        req = json.loads(self.rfile.read(length) or b"{}")
        messages = req.get("messages", [])
        has_tool = any(m.get("role") == "tool" for m in messages)

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()

        def chunk(delta, finish=None, usage=None):
            obj = {
                "id": "chatcmpl-mock1", "object": "chat.completion.chunk",
                "created": 0, "model": "mock-1",
                "choices": [{"index": 0, "delta": delta, "finish_reason": finish}],
            }
            if usage is not None:
                obj["usage"] = usage
            self.wfile.write(sse(obj))
            self.wfile.flush()

        if not has_tool:
            chunk({"role": "assistant", "content": ""})
            chunk({"tool_calls": [{
                "index": 0, "id": "call_1", "type": "function",
                "function": {"name": "exec", "arguments": TOOL_CALL_ARGS},
            }]})
            chunk({}, finish="tool_calls",
                  usage={"prompt_tokens": 111, "completion_tokens": 7, "total_tokens": 118})
        else:
            chunk({"role": "assistant", "content": ""})
            chunk({"reasoning_content": "用户在验证链路，工具已返回，直接收口。"})
            chunk({"content": FINAL_TEXT})
            chunk({}, finish="stop",
                  usage={"prompt_tokens": 222, "completion_tokens": 33, "total_tokens": 255})
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
