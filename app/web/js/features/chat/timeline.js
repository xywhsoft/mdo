import { element, clear, formatClock, errorMessage } from "../../utils/dom.js";

function appendOrCreate(items, event, kind, role, key) {
  let item = items.at(-1);
  if (!item || item.key !== key || item.kind !== kind) {
    item = { key, kind, role, text: "", time: event.time, state: "running", meta: "" };
    items.push(item);
  }
  item.text += event.text ?? "";
  return item;
}

export function eventsToTimeline(events, historyLost = false) {
  const items = [];
  const tools = new Map();
  if (historyLost) {
    items.push({ key: "history-gap", kind: "system", role: "记录提示", text: "更早的实时事件已超出保留窗口。持久会话上下文仍然完整。", state: "done", time: 0 });
  }
  for (const event of events) {
    const runKey = String(event.run_id || event.agent_id || event.event_id);
    switch (event.kind) {
      case "agent_start":
        items.push(event.agent_depth > 0
          ? { key: `subagent-${event.event_id}`, kind: "task", role: "子 Agent", text: event.text || "子 Agent 已启动", state: "running", time: event.time, meta: `depth ${event.agent_depth}` }
          : { key: `user-${event.event_id}`, kind: "user", role: "你", text: event.text || "", state: "done", time: event.time });
        break;
      case "model_reasoning_delta":
        appendOrCreate(items, event, "reasoning", "思考", `reasoning-${runKey}`);
        break;
      case "model_text_delta":
        appendOrCreate(items, event, "assistant", event.model || "Agent", `assistant-${runKey}`);
        break;
      case "model_done": {
        const answer = [...items].reverse().find((item) => item.key === `assistant-${runKey}`);
        if (answer) answer.state = event.success ? "done" : "failed";
        break;
      }
      case "tool_start": {
        const item = {
          key: `tool-${event.tool_call_id || event.event_id}`,
          kind: "tool",
          role: event.tool_name || "工具",
          text: event.text || "正在执行…",
          state: "running",
          time: event.time,
          meta: event.tool_call_id || "",
        };
        tools.set(event.tool_call_id, item);
        items.push(item);
        break;
      }
      case "tool_done": {
        const tool = tools.get(event.tool_call_id);
        if (tool) {
          tool.text = event.text || (event.success ? "执行完成" : "执行失败");
          tool.state = event.success ? "done" : "failed";
        } else {
          items.push({ key: `tool-${event.event_id}`, kind: "tool", role: event.tool_name || "工具", text: event.text || "执行完成", state: event.success ? "done" : "failed", time: event.time });
        }
        break;
      }
      case "task_updated":
        items.push({ key: `task-${event.event_id}`, kind: "task", role: "后台任务", text: event.text || `任务 #${event.task_id} 已更新`, state: event.terminal ? "done" : "running", time: event.time, meta: event.task_id ? `task ${event.task_id}` : "" });
        break;
      case "artifact_created":
        items.push({ key: `artifact-${event.event_id}`, kind: "system", role: "产物", text: event.artifact_path || event.text || "已创建产物", state: "done", time: event.time });
        break;
      case "compaction_start":
      case "compaction_done":
      case "compaction_rejected":
        items.push({ key: `context-${event.event_id}`, kind: "system", role: "上下文", text: event.text || (event.kind === "compaction_start" ? "正在整理上下文…" : event.kind === "compaction_done" ? "上下文整理完成" : "上下文整理未应用"), state: event.kind === "compaction_start" ? "running" : "done", time: event.time });
        break;
      case "recovery_required":
      case "recovery_resolved":
        items.push({ key: `recovery-${event.event_id}`, kind: "system", role: "恢复", text: event.text || (event.kind === "recovery_required" ? "会话需要恢复" : "会话已恢复"), state: event.kind === "recovery_required" ? "failed" : "done", time: event.time });
        break;
      case "error":
        items.push({ key: `error-${event.event_id}`, kind: "error", role: "运行错误", text: event.text || "Agent 运行失败", state: "failed", time: event.time });
        break;
      case "agent_done": {
        const answer = [...items].reverse().find((item) => item.key === `assistant-${runKey}`);
        if (answer) answer.state = event.success ? "done" : "failed";
        else if (event.text) items.push({ key: `done-${event.event_id}`, kind: "assistant", role: event.model || "Agent", text: event.text, state: event.success ? "done" : "failed", time: event.time });
        break;
      }
      case "model_start":
        break;
      default:
        items.push({ key: `event-${event.event_id}`, kind: "system", role: "事件", text: event.text || event.kind || "未知事件", state: event.terminal ? "done" : "running", time: event.time });
    }
  }
  return items;
}

function timelineNode(item) {
  const header = element("div", { className: "timeline-item-header" }, [
    element("span", { className: "timeline-role", text: item.role }),
    element("time", { className: "timeline-time", text: formatClock(item.time) }),
  ]);
  const children = [header, element("div", { className: "timeline-body", text: item.text })];
  if (item.meta) children.push(element("div", { className: "timeline-meta", text: item.meta }));
  return element("li", {
    className: "timeline-item",
    attrs: { "data-kind": item.kind, "data-state": item.state },
  }, children);
}

export function createTimelineView({ container, welcome, store }) {
  let pendingState = store.get();
  let frame = 0;
  let followTail = true;
  const scroller = container.closest(".conversation");

  function render() {
    frame = 0;
    const state = pendingState;
    const data = state.data;
    const items = eventsToTimeline(data?.events ?? [], data?.historyLost);
    welcome.hidden = Boolean(data?.sessionId && items.length > 0);
    clear(container);
    if (state.status === "error") {
      container.append(timelineNode({ key: "load-error", kind: "error", role: "无法读取时间线", text: errorMessage(state.error), state: "failed", time: 0 }));
    } else {
      for (const item of items) container.append(timelineNode(item));
    }
    if (followTail) scroller.scrollTop = scroller.scrollHeight;
  }

  function queueRender(state) {
    pendingState = state;
    if (!frame) frame = requestAnimationFrame(render);
  }

  scroller.addEventListener("scroll", () => {
    followTail = scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight < 100;
  }, { passive: true });

  const unsubscribe = store.subscribe(queueRender);
  return Object.freeze({
    follow() { followTail = true; },
    destroy() { unsubscribe(); if (frame) cancelAnimationFrame(frame); },
  });
}
