import { element, clear, formatClock, errorMessage, toast } from "../../utils/dom.js";
import { renderMarkdown } from "./markdown.js";

function modelKey(event) {
  return `${event.run_id || event.agent_id || event.event_id}-${event.agent_turn || 0}`;
}

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
  const modelStarts = new Map();
  const promptsByRun = new Map();
  if (historyLost) {
    items.push({ key: "history-gap", kind: "system", role: "记录提示", text: "更早的实时事件已超出保留窗口。持久会话上下文仍然完整。", state: "done", time: 0 });
  }
  for (const event of events) {
    const runKey = String(event.run_id || event.agent_id || event.event_id);
    switch (event.kind) {
      case "agent_start":
        if (event.agent_depth === 0 && Number(event.user_message_sequence) > 0)
          promptsByRun.set(runKey, {
            sequence: Number(event.user_message_sequence),
            text: event.text || "",
            truncated: Boolean(event.text_truncated),
          });
        items.push(event.agent_depth > 0
          ? { key: `subagent-${event.event_id}`, kind: "task", role: "子 Agent", text: event.text || "子 Agent 已启动", state: "running", time: event.time, meta: `depth ${event.agent_depth}` }
          : { key: `user-${event.event_id}`, kind: "user", role: "你", text: event.text || "", state: "done", time: event.time,
            userMessageSequence: Number(event.user_message_sequence || 0),
            textTruncated: Boolean(event.text_truncated) });
        break;
      case "model_reasoning_delta":
        appendOrCreate(items, event, "reasoning", "思考", `reasoning-${modelKey(event)}`);
        break;
      case "model_text_delta":
        {
          const answer = appendOrCreate(items, event, "assistant", event.model || "Agent", `assistant-${modelKey(event)}`);
          answer.runKey = runKey;
          answer.retryPrompt = promptsByRun.get(runKey);
        }
        break;
      case "model_done": {
        const answer = [...items].reverse().find((item) => item.key === `assistant-${modelKey(event)}`);
        if (answer) {
          answer.state = event.success ? "done" : "failed";
          if (event.success && Number.isSafeInteger(Number(event.event_id)))
            answer.feedbackEventId = Number(event.event_id);
          answer.inputTokens = Number(event.input_tokens || 0);
          answer.outputTokens = Number(event.output_tokens || 0);
          const elapsed = (Number(event.time) - Number(modelStarts.get(modelKey(event)) || answer.time)) / 1e6;
          if (elapsed > 0 && answer.outputTokens > 0) answer.tokensPerSecond = answer.outputTokens / elapsed;
        }
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
        const answer = [...items].reverse().find((item) => item.kind === "assistant" && item.runKey === runKey);
        if (answer) answer.state = event.success ? "done" : "failed";
        else if (event.text) items.push({ key: `done-${event.event_id}`, kind: "assistant", role: event.model || "Agent", text: event.text, state: event.success ? "done" : "failed", time: event.time,
          retryPrompt: promptsByRun.get(runKey) });
        break;
      }
      case "model_start":
        modelStarts.set(modelKey(event), event.time);
        break;
      default:
        items.push({ key: `event-${event.event_id}`, kind: "system", role: "事件", text: event.text || event.kind || "未知事件", state: event.terminal ? "done" : "running", time: event.time });
    }
  }
  return items;
}

async function copyText(value) {
  if (navigator.clipboard?.writeText) return navigator.clipboard.writeText(value);
  const input = document.createElement("textarea");
  input.value = value;
  input.style.position = "fixed";
  input.style.opacity = "0";
  document.body.append(input);
  input.select();
  const copied = document.execCommand("copy");
  input.remove();
  if (!copied) throw new Error("clipboard unavailable");
}

function timelineNode(item, handlers, feedback) {
  const time = element("time", { className: "timeline-time", text: formatClock(item.time) });
  if (item.time) {
    const date = new Date(Number(item.time) / 1000);
    time.dateTime = date.toISOString();
    time.title = date.toLocaleString("zh-CN");
  }
  const header = element("div", { className: "timeline-item-header" }, [
    element("span", { className: "timeline-role", text: item.role }),
    time,
  ]);
  const body = element("div", { className: "timeline-body" +
    (item.kind === "assistant" ? " markdown-body" : "") });
  if (item.kind === "assistant") body.append(renderMarkdown(item.text));
  else body.textContent = item.text;
  const children = [header, body];
  if (item.meta) children.push(element("div", { className: "timeline-meta", text: item.meta }));
  if (["user", "assistant"].includes(item.kind) && item.text) {
    const actions = element("div", { className: "timeline-actions" });
    const copy = element("button", { text: "复制", attrs: { type: "button", "aria-label": "复制消息" } });
    copy.addEventListener("click", async () => {
      try { await copyText(item.text); toast("消息已复制"); }
      catch { toast("无法复制消息", "error"); }
    });
    actions.append(copy);
    if (item.kind === "user" && Number.isSafeInteger(item.userMessageSequence) &&
        item.userMessageSequence > 0 && !item.textTruncated) {
      const edit = element("button", { text: "编辑并分叉", attrs: { type: "button", "aria-label": "编辑此消息并创建会话分支" } });
      edit.addEventListener("click", async () => {
        edit.disabled = true;
        try { await handlers.onEdit(item.userMessageSequence, item.text); }
        catch (error) { toast(errorMessage(error), "error"); }
        finally { edit.disabled = false; }
      });
      actions.append(edit);
    }
    if (item.kind === "assistant") {
      const fork = element("button", { text: "分叉", attrs: { type: "button", "aria-label": "分叉当前会话" } });
      fork.addEventListener("click", async () => {
        fork.disabled = true;
        try { await handlers.onFork(); }
        catch (error) { toast(errorMessage(error), "error"); }
        finally { fork.disabled = false; }
      });
      actions.append(fork);
      const retryPrompt = item.retryPrompt;
      if (retryPrompt && Number.isSafeInteger(retryPrompt.sequence) &&
          retryPrompt.sequence > 0 && retryPrompt.text && !retryPrompt.truncated) {
        const retry = element("button", { text: "重试并分叉", attrs: { type: "button", "aria-label": "重试此回合并创建会话分支" } });
        retry.addEventListener("click", async () => {
          retry.disabled = true;
          try { await handlers.onRetry(retryPrompt.sequence, retryPrompt.text); }
          catch (error) { toast(errorMessage(error), "error"); }
          finally { retry.disabled = false; }
        });
        actions.append(retry);
      }
      if (item.feedbackEventId && item.state === "done") {
        for (const [value, label] of [["good", "点赞"], ["bad", "点踩"]]) {
          const button = element("button", {
            text: label,
            attrs: { type: "button", "aria-label": label,
              "aria-pressed": String(feedback === value) },
          });
          button.addEventListener("click", async () => {
            button.disabled = true;
            try {
              await handlers.onFeedback(item.feedbackEventId,
                feedback === value ? "none" : value);
            } catch (error) { toast(errorMessage(error), "error"); }
            finally { button.disabled = false; }
          });
          actions.append(button);
        }
      }
      const stats = [];
      if (item.inputTokens || item.outputTokens)
        stats.push(`${item.inputTokens || 0} 输入 / ${item.outputTokens || 0} 输出 tokens`);
      if (Number.isFinite(item.tokensPerSecond)) stats.push(`${item.tokensPerSecond.toFixed(1)} tok/s`);
      if (stats.length) actions.append(element("span", { className: "timeline-stats", text: stats.join(" · ") }));
    }
    children.push(actions);
  }
  return element("li", {
    className: "timeline-item",
    attrs: { "data-kind": item.kind, "data-state": item.state },
  }, children);
}

export function createTimelineView({ container, welcome, toBottom, store, feedbackStore, onFork, onFeedback, onEdit, onRetry, onSearchCount }) {
  const handlers = { onFork, onFeedback, onEdit, onRetry };
  let pendingState = store.get();
  let frame = 0;
  let followTail = true;
  let searchQuery = "";
  const scroller = container.closest(".conversation");

  function updateBottomButton() {
    toBottom.hidden = scroller.scrollHeight - scroller.scrollTop -
      scroller.clientHeight <= 120;
  }

  function render() {
    frame = 0;
    const state = pendingState;
    const data = state.data;
    const items = eventsToTimeline(data?.events ?? [], data?.historyLost);
    const visible = searchQuery ? items.filter((item) =>
      `${item.role} ${item.text} ${item.meta ?? ""}`.toLocaleLowerCase().includes(searchQuery)) : items;
    onSearchCount?.(searchQuery ? visible.length : 0, Boolean(data?.historyLost));
    welcome.hidden = Boolean(data?.sessionId && items.length > 0);
    clear(container);
    if (state.status === "error") {
      container.append(timelineNode({ key: "load-error", kind: "error", role: "无法读取时间线", text: errorMessage(state.error), state: "failed", time: 0 }, handlers, ""));
    } else {
      const selected = feedbackStore.get().data;
      const feedback = selected?.projectId === data?.projectId &&
        selected?.sessionId === data?.sessionId ? selected.items : new Map();
      for (const item of visible)
        container.append(timelineNode(item, handlers,
          feedback.get(item.feedbackEventId) ?? ""));
    }
    if (followTail && !searchQuery) scroller.scrollTop = scroller.scrollHeight;
    updateBottomButton();
  }

  function queueRender(state) {
    pendingState = state;
    if (!frame) frame = requestAnimationFrame(render);
  }

  scroller.addEventListener("scroll", () => {
    followTail = scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight < 100;
    updateBottomButton();
  }, { passive: true });
  toBottom.addEventListener("click", () => {
    followTail = true;
    scroller.scrollTo({ top: scroller.scrollHeight, behavior: "smooth" });
  });

  const unsubscribe = store.subscribe(queueRender);
  const unsubscribeFeedback = feedbackStore.subscribe(() => queueRender(store.get()));
  return Object.freeze({
    follow() { followTail = true; },
    search(query) {
      searchQuery = query.trim().toLocaleLowerCase();
      queueRender(store.get());
    },
    destroy() { unsubscribe(); unsubscribeFeedback(); if (frame) cancelAnimationFrame(frame); },
  });
}
