#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ling_toolcall_test.py — Ling 3.0-tiny 工具调用成功率测试弹
直连 GPU 服务端，使用与 xwork 完全一致的工具 schema。
用法: python ling_toolcall_test.py [--base URL] [--key KEY] [--rounds N] [--tag NAME]
"""
import argparse, json, ssl, sys, time, urllib.request, re, os

DEF_BASE = "https://ai.xywhsoft.com:8444/v1"
DEF_KEY = "a59048aa00184acc7a7d9540c95c84f8259ff21fc35110a7"
DEF_MODEL = "ling-3.0-tiny"
CTX = ssl._create_unverified_context()

# ---- 与 xwork 完全一致的工具 schema（摘自 xwork_tools.c） ----
TOOLS = [
  {"type":"function","function":{"name":"read","description":"Read workspace files. Text returns numbered lines with pagination; images (jpg/png/gif/webp/bmp) are attached for viewing. Oversized text output is truncated with the full copy spilled to an artifact.","parameters":{"type":"object","properties":{"path":{"type":"string"},"start_line":{"type":"integer","minimum":1},"max_lines":{"type":"integer","minimum":1,"maximum":10000}},"required":["path"],"additionalProperties":False}}},
  {"type":"function","function":{"name":"write","description":"Create, overwrite, or append a UTF-8 workspace file. Parent directories are created automatically and reported. Prefer edit for small changes.","parameters":{"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"},"mode":{"type":"string","enum":["overwrite","create","append"]}},"required":["path","content"],"additionalProperties":False}}},
  {"type":"function","function":{"name":"edit","description":"Apply exact text edits to one file in a single atomic pass. Each old_text must match the original file uniquely (or set replace_all); 0 or multiple matches return candidate context lines for self-correction.","parameters":{"type":"object","properties":{"path":{"type":"string"},"edits":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"object","properties":{"old_text":{"type":"string"},"new_text":{"type":"string"},"replace_all":{"type":"boolean"}},"required":["old_text","new_text"],"additionalProperties":False}}},"required":["path","edits"],"additionalProperties":False}}},
  {"type":"function","function":{"name":"spawn","description":"Start a background task and return task_id. argv is direct (no shell). Output lands in a bounded tail buffer; completion is announced at the next turn boundary.","parameters":{"type":"object","properties":{"argv":{"type":"array","minItems":1,"maxItems":256,"items":{"type":"string"}},"cwd":{"type":"string"},"env":{"type":"array","maxItems":128,"items":{"type":"string"}},"max_capture_bytes":{"type":"integer","minimum":1024,"maximum":67108864},"merge_stderr":{"type":"boolean"},"notify":{"type":"string","maxLength":500},"remind_after_ms":{"type":"integer","minimum":0,"maximum":3600000}},"required":["argv"],"additionalProperties":False}}},
  {"type":"function","function":{"name":"poll","description":"Read new incremental output from a task (process or subagent) and report its state. Set release=true only after it exits.","parameters":{"type":"object","properties":{"task_id":{"type":"integer","minimum":1},"wait_ms":{"type":"integer","minimum":0,"maximum":30000},"max_bytes":{"type":"integer","minimum":256,"maximum":1048576},"release":{"type":"boolean"}},"required":["task_id"],"additionalProperties":False}}},
  {"type":"function","function":{"name":"wait","description":"Block until any (default) or all of the given tasks exit, returning their new output and exit status. An expired timeout returns early with still-running states.","parameters":{"type":"object","properties":{"task_ids":{"type":"array","minItems":1,"maxItems":32,"items":{"type":"integer","minimum":1}},"mode":{"type":"string","enum":["any","all"]},"timeout_ms":{"type":"integer","minimum":0,"maximum":600000}},"required":["task_ids"],"additionalProperties":False}}},
  {"type":"function","function":{"name":"stdin","description":"Write text to a process task's stdin, optionally appending a newline and/or closing stdin.","parameters":{"type":"object","properties":{"task_id":{"type":"integer","minimum":1},"input":{"type":"string"},"append_newline":{"type":"boolean"},"close_stdin":{"type":"boolean"}},"required":["task_id"],"additionalProperties":False}}},
  {"type":"function","function":{"name":"stop","description":"Stop a task. Modes interrupt/terminate/kill/kill_tree apply to processes; a subagent is cancelled cooperatively. Success means the task actually stopped.","parameters":{"type":"object","properties":{"task_id":{"type":"integer","minimum":1},"mode":{"type":"string","enum":["interrupt","terminate","kill","kill_tree"]},"wait_ms":{"type":"integer","minimum":0,"maximum":30000},"release":{"type":"boolean"}},"required":["task_id"],"additionalProperties":False}}},
  {"type":"function","function":{"name":"exec","description":"Run one command to completion. argv is passed directly with no shell — no pipes or globs; chain work in the command's own tooling or use spawn. Nonzero exit fails unless listed in expected_exit_codes.","parameters":{"type":"object","properties":{"argv":{"type":"array","minItems":1,"maxItems":256,"items":{"type":"string"}},"cwd":{"type":"string"},"env":{"type":"array","maxItems":128,"items":{"type":"string"}},"timeout_ms":{"type":"integer","minimum":1,"maximum":3600000},"merge_stderr":{"type":"boolean"},"expected_exit_codes":{"type":"array","minItems":1,"maxItems":32,"items":{"type":"integer","minimum":-2147483648,"maximum":2147483647}}},"required":["argv"],"additionalProperties":False}}},
]

BASE_SYS = """你是 mdo（墨斗）——运行在原生 C 栈上的 agent 工作台。

环境: Windows (win32)
Shell: 无 shell——exec 工具为 argv 直生; rg/git/python 在 PATH
编码: 原生命令输出 GBK, 工具层已转 UTF-8
工作目录: D:/GIT/xrt
"""

# 优化的系统提示词（对照实验用）
OPT_SYS_EXTRA = """

工具调用纪律（务必遵守）:
1. exec/spawn 的 argv 是字符串数组，不是字符串: {"argv":["git","status"]} 正确; {"argv":"git status"} 错误。
2. argv 不经 shell: 管道、重定向、通配符都不可用。列文件用 ["python","-c","import os;print('\\n'.join(os.listdir('.')))"] 或 ["git","ls-files"]；搜索用 ["rg","pattern","-l"]。
3. 每个工具调用前确认: 工具名在列表中、必填参数齐全、参数类型与 schema 一致。
4. 需要多步时一次只调一个工具，等结果再决定下一步。
5. 只在需要工具时调用；闲聊/解释类问题直接回答，不调用工具。
"""

def chat(base, key, messages, model=DEF_MODEL, temp=0.2):
    body = json.dumps({
        "model": model, "messages": messages, "tools": TOOLS,
        "temperature": temp, "max_tokens": 2048,
    }).encode("utf-8")
    req = urllib.request.Request(base.rstrip("/") + "/chat/completions", data=body,
        headers={"Content-Type": "application/json", "Authorization": "Bearer " + key})
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=120, context=CTX) as r:
        d = json.loads(r.read().decode("utf-8"))
    d["_latency"] = time.time() - t0
    return d

# ---- 校验器：判定一次工具调用是否合规（直接从 TOOLS schema 推导） ----
VALID_TOOLS = {t["function"]["name"] for t in TOOLS}
SCHEMA_OF = {t["function"]["name"]: t["function"]["parameters"] for t in TOOLS}
REQ = {n: p.get("required", []) for n, p in SCHEMA_OF.items()}

def check_call(tc):
    """返回 (ok, 问题列表)。检查工具名、JSON 参数、必填、类型、未知参数。"""
    fn = tc.get("function", {})
    name = fn.get("name", "")
    args_raw = fn.get("arguments", "")
    issues = []
    if name not in VALID_TOOLS:
        issues.append("幻觉工具:%s" % name)
        return False, issues
    try:
        args = json.loads(args_raw if isinstance(args_raw, str) else json.dumps(args_raw))
        if not isinstance(args, dict):
            issues.append("参数非对象")
            return False, issues
    except Exception:
        issues.append("JSON 解析失败")
        return False, issues
    schema = SCHEMA_OF.get(name, {})
    props = schema.get("properties", {})
    for r in REQ.get(name, []):
        if r not in args:
            issues.append("缺必填:%s" % r)
    def type_ok(v, spec):
        ty = spec.get("type")
        if ty == "string": return isinstance(v, str)
        if ty == "integer": return isinstance(v, int) and not isinstance(v, bool)
        if ty == "number": return isinstance(v, (int, float)) and not isinstance(v, bool)
        if ty == "boolean": return isinstance(v, bool)
        if ty == "array": return isinstance(v, list)
        if ty == "object": return isinstance(v, dict)
        return True
    for k, v in args.items():
        spec = props.get(k)
        if spec is None:
            issues.append("未知参数:%s" % k)
            continue
        if not type_ok(v, spec):
            issues.append("类型错误:%s 应为 %s 实为 %s" % (k, spec.get("type"), type(v).__name__))
        if spec.get("type") == "array":
            items = spec.get("items", {})
            if items.get("type") == "string":
                for i, x in enumerate(v):
                    if not isinstance(x, str):
                        issues.append("%s[%d] 非字符串" % (k, i))
            if items.get("type") == "integer":
                for i, x in enumerate(v):
                    if not isinstance(x, int) or isinstance(x, bool):
                        issues.append("%s[%d] 非整数" % (k, i))
    return not issues, issues

# ---- 测试场景 ----
SCENARIOS = [
  # 高难场景：引号嵌套/类型陷阱/中文路径/精确编辑/后台任务链
  ("exec-quotes", "用 exec 运行 python 打印 hello world（python -c 方式）。",
   lambda fin, turns: has_tool(turns, "exec") and first_call_ok(turns)),
  ("type-trap-params", "读取 xllm.h 从第 5 行开始最多 8 行。",
   lambda fin, turns: has_tool(turns, "read") and first_call_ok(turns)),
  ("chinese-path", "读取 文档/说明.md 这个文件。",  # 不存在的中文路径→看首调合规性
   lambda fin, turns: has_tool(turns, "read") and first_call_ok(turns)),
  ("edit-precise", "创建 t.txt 内容为三行：alpha/beta/gamma（每行一个词）。然后把 beta 这一行替换为 BETA。",
   lambda fin, turns: has_tool(turns, "edit") and first_edit_ok(turns)),
  ("spawn-chain", "用 spawn 启动一个 sleep 3 的后台任务，然后用 poll 查看它的状态。",
   lambda fin, turns: has_tool(turns, "spawn") and has_tool(turns, "poll") and first_call_ok(turns)),
  ("no-tool-math", "计算 17*23 等于多少？直接给答案。",  # 不应调工具
   lambda fin, turns: tool_turns(turns) == 0),
  ("exec-pipe-trap", "统计当前目录下有多少个 .h 文件。",  # 无 shell：模型须拆步或用 python -c
   lambda fin, turns: has_tool(turns, "exec") and first_call_ok(turns)),
  ("unknown-q", "帮我查看天气。",  # 无天气工具：理想行为=解释无此能力，不乱调工具
   lambda fin, turns: not has_tool(turns, "read") and not has_tool(turns, "exec") or True),  # 宽松：只记录不判定
]
def tool_turns(turns):
    return sum(len(t) for t in turns)

def iter_calls(turns):
    for t in turns:
        if isinstance(t, dict):
            yield t
        elif isinstance(t, list):
            for tc in t:
                if isinstance(tc, dict):
                    yield tc

def first_call(turns):
    for tc in iter_calls(turns):
        return tc
    return None

def first_call_ok(turns):
    tc = first_call(turns)
    return tc is not None and check_call(tc)[0]

def first_edit_ok(turns):
    for tc in iter_calls(turns):
        if tc.get("function", {}).get("name") == "edit":
            ok, _ = check_call(tc)
            if not ok: return False
            try:
                args = json.loads(tc["function"]["arguments"])
                for e in args.get("edits", []):
                    if e.get("old_text", "").strip() == "" or e.get("new_text", "").strip() == "":
                        return False
            except Exception:
                return False
            return True
    return False

def has_tool(turns, name):
    return any(tc.get("function", {}).get("name") == name for tc in iter_calls(turns))

def all_calls_ok(turns):
    return all(check_call(tc)[0] for tc in iter_calls(turns))

def run_scenario(base, key, sid, prompt, judge, sys_extra, rounds, temp, nudge=False):
    sys_prompt = BASE_SYS + (sys_extra or "")
    msgs = [{"role": "system", "content": sys_prompt}, {"role": "user", "content": prompt}]
    tool_calls_hist = []
    errors = []
    final_text = ""
    last_hash = None
    repeat_count = 0
    nudged = False
    for rnd in range(rounds):
        try:
            d = chat(base, key, msgs, temp=temp)
        except Exception as e:
            errors.append("http:%s" % str(e)[:80]); break
        msg = d["choices"][0]["message"]
        tcs = msg.get("tool_calls") or []
        if isinstance(tcs, str):
            try: tcs = json.loads(tcs)
            except Exception: tcs = []
        if not isinstance(tcs, list): tcs = []
        if tcs:
            for tc in tcs:
                if isinstance(tc, dict):
                    tc = dict(tc)
                    tc["_round"] = rnd
                    tool_calls_hist.append(tc)
            msgs.append({"role": "assistant", "content": msg.get("content") or "",
                         "tool_calls": [{"id": c.get("id", "t%d" % i), "type": "function",
                                          "function": {"name": c["function"]["name"],
                                                       "arguments": c["function"]["arguments"]}}
                                         for i, c in enumerate(tcs)]})
            # 重复检测（xwork loop-guard 同款 hash 语义）
            batch_sig = tuple((c["function"]["name"], c["function"]["arguments"]) for c in tcs)
            batch_hash = hash(batch_sig)
            if batch_hash == last_hash: repeat_count += 1
            else: repeat_count = 0
            last_hash = batch_hash
            # 模拟工具结果
            for i, c in enumerate(tcs):
                name = c["function"]["name"]
                try: cargs = json.loads(c["function"]["arguments"])
                except Exception: cargs = {}
                content = fake_result(name, cargs)
                if nudge:
                    if repeat_count >= 2:
                        content = "run aborted: repeated identical tool-call batch too many times"
                    elif repeat_count == 1 and not nudged:
                        content = ("[loop-guard] This call is identical to your previous one. "
                                   "Repeating it again will abort the run: change arguments, "
                                   "switch tools, or finish with what you already have.] " + content)
                        nudged = True
                msgs.append({"role": "tool", "tool_call_id": c.get("id", "t%d" % i), "content": content})
            continue
        final_text = msg.get("content") or ""
        break
    ok = judge(final_text, tool_calls_hist)
    all_issues = []
    for tc in iter_calls(tool_calls_hist):
        _, iss = check_call(tc)
        all_issues += iss
    return {"id": sid, "ok": ok, "calls": [tc["function"]["name"] for tc in iter_calls(tool_calls_hist)],
            "issues": all_issues, "errors": errors, "final": final_text[:120]}

def fake_exec_output(args):
    """argv 关键词 -> 语义匹配输出"""
    argv = args.get("argv", []) if isinstance(args, dict) else []
    joined = " ".join(str(x) for x in argv)
    if "python" in joined and "-c" in argv:
        try:
            code = argv[argv.index("-c") + 1]
        except Exception:
            code = ""
        if "hello" in code.lower():
            return json.dumps({"exit_code": 0, "stdout": "hello world", "stderr": ""})
        if ".h" in code or "listdir" in code or "glob" in code:
            return json.dumps({"exit_code": 0, "stdout": "42", "stderr": ""})
        return json.dumps({"exit_code": 0, "stdout": "ok", "stderr": ""})
    if "git" in joined:
        return json.dumps({"exit_code": 0, "stdout": "on branch master", "stderr": ""})
    if "rg" in joined:
        return json.dumps({"exit_code": 0, "stdout": "src/a.c\nsrc/b.c", "stderr": ""})
    return json.dumps({"exit_code": 0, "stdout": "done", "stderr": ""})

FAKE_RESULTS = {
    "read": "# xrt\nA runtime library.\n",
    "write": "ok: wrote 1 file",
    "edit": "ok: 1 edit applied",
    "spawn": "task_id: 7 started",
    "poll": "state: running, output: (empty)",
    "stop": "task 999 not found",
}

def fake_result(name, args):
    if name == "exec":
        try:
            return fake_exec_output(args if isinstance(args, dict) else {})
        except Exception:
            pass
    return FAKE_RESULTS.get(name, "ok")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default=DEF_BASE)
    ap.add_argument("--key", default=DEF_KEY)
    ap.add_argument("--rounds", type=int, default=4)
    ap.add_argument("--temp", type=float, default=0.2)
    ap.add_argument("--tag", default="base")
    ap.add_argument("--only", default="")
    ap.add_argument("--reps", type=int, default=1)
    ap.add_argument("--nudge", action="store_true")
    ap.add_argument("--sys-extra-file", default="")
    args = ap.parse_args()
    sys_extra = ""
    if args.sys_extra_file and os.path.exists(args.sys_extra_file):
        sys_extra = open(args.sys_extra_file, encoding="utf-8").read()
    only = set(args.only.split(",")) if args.only else None
    results = []
    for sid, prompt, judge in SCENARIOS:
        if only and sid not in only: continue
        for rep in range(args.reps):
            r = run_scenario(args.base, args.key, sid, prompt, judge, sys_extra, args.rounds, args.temp, nudge=args.nudge)
            r["tag"] = args.tag; r["rep"] = rep
            results.append(r)
        # 聚合显示
        rs = [x for x in results if x["id"] == sid]
        n_ok = sum(1 for x in rs if x["ok"])
        all_calls = [c for x in rs for c in x["calls"]]
        mark = "PASS" if n_ok == len(rs) else (" MIX" if n_ok else "FAIL")
        print("[%s] %-18s %d/%d  calls=%-28s %s" % (mark, sid, n_ok, len(rs),
              ",".join(all_calls[:8]) or "-",
              "; ".join((rs[0]["issues"] or rs[0]["errors"])[:2])))
        time.sleep(0.4)
    n_ok = sum(1 for r in results if r["ok"])
    maxed = sum(1 for r in results if len(r["calls"]) >= 3)
    print("loops-maxed(>=3 calls): %d/%d" % (maxed, len(results)))
    print("\n=== %s: %d/%d passed (%.0f%%) ===" % (args.tag, n_ok, len(results), 100.0 * n_ok / max(1, len(results))))
    # 失败明细
    for r in results:
        if not r["ok"]:
            print("---- %s final: %s" % (r["id"], r["final"][:100]))
    out = "ling_test_%s.json" % args.tag
    json.dump(results, open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("saved:", out)

if __name__ == "__main__":
    main()
