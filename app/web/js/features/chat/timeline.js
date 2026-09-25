import { element, clear, formatClock, errorMessage, toast } from "../../utils/dom.js";
import { attachmentUrl } from "../../api/client.js";
import { mountIcons } from "../../components/icons.js";
import { renderMarkdown } from "./markdown.js";

function modelKey(event, epoch) {
  return `${event.run_id || event.agent_id || event.event_id}-${epoch}-${event.agent_turn || 0}`;
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
  const runEpochs = new Map();
  if (historyLost) {
    items.push({ key: "history-gap", kind: "system", role: "记录提示", text: "更早的事件已不在当前记录中。", state: "done", time: 0 });
  }
  for (const event of events) {
    const runKey = String(event.run_id || event.agent_id || event.event_id);
    const epoch = runEpochs.get(runKey) ?? 0;
    switch (event.kind) {
      case "agent_start":
        runEpochs.set(runKey, Number(event.event_id) || 0);
        if (event.agent_depth === 0 && Number(event.user_message_sequence) > 0)
          promptsByRun.set(runKey, {
            sequence: Number(event.user_message_sequence),
            text: event.text || "",
            truncated: Boolean(event.text_truncated),
            attachments: event.attachments || [],
          });
        if (event.agent_depth > 0) {
          items.push({ key: `subagent-${event.event_id}`, kind: "task",
            role: "子 Agent", text: event.text || "子 Agent 已启动",
            state: "running", time: event.time, meta: `depth ${event.agent_depth}` });
        } else if (Number(event.schema_version) >= 3 &&
                   Number(event.user_message_sequence || 0) === 0) {
          // A resumed run has no new user message. Older event schemas did not
          // carry a durable message sequence, so keep their original projection.
          items.push({ key: `resume-${event.event_id}`, kind: "system",
            role: "恢复", text: "从上次中断处继续运行", state: "done",
            time: event.time });
        } else {
          items.push({ key: `user-${event.event_id}`, kind: "user", role: "你",
            text: event.text || "", state: "done", time: event.time,
            attachments: Array.isArray(event.attachments) ? event.attachments : [],
            userMessageSequence: Number(event.user_message_sequence || 0),
            textTruncated: Boolean(event.text_truncated) });
        }
        break;
      case "model_reasoning_delta": {
        const thought = appendOrCreate(items, event, "reasoning", "思考",
          `reasoning-${modelKey(event, epoch)}`);
        thought.runKey = runKey;
        thought.runEpoch = epoch;
        break;
      }
      case "model_text_delta":
        {
          const answer = appendOrCreate(items, event, "assistant", event.model || "Agent", `assistant-${modelKey(event, epoch)}`);
          answer.runKey = runKey;
          answer.runEpoch = epoch;
          answer.retryPrompt = promptsByRun.get(runKey);
        }
        break;
      case "model_done": {
        for (const thought of items) {
          if (thought.kind !== "reasoning" ||
              thought.key !== `reasoning-${modelKey(event, epoch)}`) continue;
          thought.state = event.success ? "done" : "failed";
          thought.durationSeconds = Math.max(0,
            (Number(event.time) - Number(thought.time)) / 1e6);
        }
        const answer = [...items].reverse().find((item) => item.key === `assistant-${modelKey(event, epoch)}`);
        if (answer) {
          answer.state = event.success ? "done" : "failed";
          if (event.success && Number.isSafeInteger(Number(event.event_id)))
            answer.feedbackEventId = Number(event.event_id);
          answer.inputTokens = Number(event.input_tokens || 0);
          answer.outputTokens = Number(event.output_tokens || 0);
          const elapsed = (Number(event.time) - Number(modelStarts.get(modelKey(event, epoch)) || answer.time)) / 1e6;
          if (elapsed > 0 && answer.outputTokens > 0) answer.tokensPerSecond = answer.outputTokens / elapsed;
        }
        break;
      }
      case "tool_start": {
        const item = {
          key: `tool-${event.event_id}`,
          kind: "tool",
          role: event.tool_name || "工具",
          text: event.text || "正在执行…",
          inputText: event.text || "",
          outputText: "",
          runKey,
          runEpoch: epoch,
          state: "running",
          time: event.time,
          meta: event.tool_call_id || "",
        };
        if (event.tool_call_id)
          tools.set(`${runKey}:${epoch}:${event.tool_call_id}`, item);
        items.push(item);
        break;
      }
      case "tool_done": {
        const tool = event.tool_call_id
          ? tools.get(`${runKey}:${epoch}:${event.tool_call_id}`) : null;
        if (tool) {
          tool.outputText = event.text || (event.success ? "执行完成" : "执行失败");
          tool.text = tool.outputText;
          tool.state = event.success ? "done" : "failed";
          tool.durationSeconds = Math.max(0,
            (Number(event.time) - Number(tool.time)) / 1e6);
        } else {
          const outputText = event.text || (event.success ? "执行完成" : "执行失败");
          items.push({ key: `tool-${event.event_id}`, kind: "tool",
            role: event.tool_name || "工具", text: outputText,
            inputText: "", outputText,
            state: event.success ? "done" : "failed", time: event.time });
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
        const terminalState = event.success ? "done" : "cancelled";
        for (const item of items) {
          if (item.runKey !== runKey || item.runEpoch !== epoch ||
              item.state !== "running" ||
              (item.kind !== "reasoning" && item.kind !== "tool")) continue;
          item.state = terminalState;
        }
        const answer = [...items].reverse().find((item) => item.kind === "assistant" &&
          item.runKey === runKey && item.runEpoch === epoch);
        if (answer) answer.state = terminalState;
        else if (event.text || !event.success) items.push({
          key: `done-${event.event_id}`, kind: "assistant",
          role: event.model || "Agent", text: event.text || "",
          state: terminalState, time: event.time,
          runKey, runEpoch: epoch, retryPrompt: promptsByRun.get(runKey),
        });
        break;
      }
      case "history_truncated":
        items.push({ key: `history-${event.event_id}`, kind: "system",
          role: "历史", text: event.text || "会话历史已截断",
          state: "done", time: event.time });
        break;
      case "model_start":
        modelStarts.set(modelKey(event, epoch), event.time);
        break;
      default:
        items.push({ key: `event-${event.event_id}`, kind: "system", role: "事件", text: event.text || event.kind || "未知事件", state: event.terminal ? "done" : "running", time: event.time });
    }
  }
  let nextUserSequence = null;
  for (let index = items.length - 1; index >= 0; index -= 1) {
    const item = items[index];
    if (item.kind === "user" && Number.isSafeInteger(item.userMessageSequence) &&
        item.userMessageSequence > 0) nextUserSequence = item.userMessageSequence;
    if (item.kind === "assistant" && item.retryPrompt &&
        Number.isSafeInteger(item.retryPrompt.sequence) &&
        item.retryPrompt.sequence > 0 &&
        (nextUserSequence === null || nextUserSequence > item.retryPrompt.sequence))
      item.forkThroughSequence = nextUserSequence === null ? null : nextUserSequence - 1;
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

function timeNode(value) {
  const time = element("time", { className: "timeline-time", text: formatClock(value) });
  if (value) {
    const date = new Date(Number(value) / 1000);
    time.dateTime = date.toISOString();
    time.title = date.toLocaleString("zh-CN");
  }
  return time;
}

function shortLine(value) {
  const line = String(value ?? "").replace(/\s+/g, " ").trim();
  return line.length > 110 ? `${line.slice(0, 110)}…` : line;
}

function toolSummaryText(value) {
  const raw = String(value ?? "");
  try {
    const args = JSON.parse(raw);
    if (args && typeof args === "object" && !Array.isArray(args)) {
      for (const name of ["command", "path", "file_path", "pattern",
        "url", "query", "goal", "argv"]) {
        const candidate = args[name];
        if (typeof candidate === "string" && candidate.trim())
          return shortLine(candidate);
        if (Array.isArray(candidate) && candidate.every((part) =>
          typeof part === "string")) return shortLine(candidate.join(" "));
      }
    }
  } catch { /* tool start may already be a human-readable summary */ }
  return shortLine(raw);
}

function foldSection(label, value, copy = false, actionRef = "") {
  const heading = element("div", { className: "timeline-fold-section-heading" }, [
    element("span", { text: label }),
  ]);
  if (copy) {
    const button = element("button", { text: "复制", attrs: {
      type: "button", "aria-label": `复制${label}`,
      "data-timeline-action": actionRef,
    } });
    button.addEventListener("click", async () => {
      try { await copyText(value); toast("已复制"); }
      catch { toast("无法复制", "error"); }
    });
    heading.append(button);
  }
  return element("div", { className: "timeline-fold-section" }, [
    heading, element("pre", { text: value }),
  ]);
}

function foldableNode(item, openState) {
  const running = item.state === "running";
  const status = running ? "运行中" : item.state === "failed" ? "失败" :
    item.state === "cancelled" ? "已停止" : "完成";
  const lastLine = item.text?.trimEnd().split("\n").at(-1) || "";
  const preview = item.kind === "reasoning"
    ? (running ? shortLine(lastLine) : "")
    : toolSummaryText(item.inputText || item.outputText || item.text);
  const duration = Number.isFinite(item.durationSeconds) && item.durationSeconds > 0
    ? `${item.durationSeconds.toFixed(1)} 秒` : "";
  const details = element("details", { className: "timeline-fold",
    attrs: { "data-timeline-key": item.key } });
  details.open = openState ?? running;
  details.append(element("summary", { className: "timeline-fold-summary" }, [
    element("span", { className: "timeline-fold-marker", attrs: { "aria-hidden": "true" } }),
    element("span", { className: "timeline-role", text: item.role }),
    element("span", { className: "timeline-fold-preview", text: preview }),
    element("span", { className: "timeline-fold-status", text: duration || status }),
    timeNode(item.time),
  ]));
  function updateBody() {
    const focusedAction = details.contains(document.activeElement)
      ? document.activeElement?.dataset.timelineAction : "";
    details.querySelector(".timeline-fold-body")?.remove();
    if (!details.open) {
      if (focusedAction) details.querySelector("summary")?.focus({ preventScroll: true });
      return;
    }
    const body = element("div", { className: "timeline-fold-body" });
    if (item.kind === "reasoning") {
      body.append(foldSection("思考过程", item.text || "正在思考…"));
    } else {
      if (item.inputText) body.append(foldSection("调用", item.inputText, true,
        `${item.key}/tool-input`));
      if (item.outputText) body.append(foldSection(
        item.state === "failed" ? "错误输出" : "结果", item.outputText, true,
        `${item.key}/tool-output`));
      if (!item.inputText && !item.outputText)
        body.append(foldSection("执行中", item.text || "正在执行…"));
    }
    details.append(body);
    if (focusedAction) {
      const replacement = [...body.querySelectorAll("[data-timeline-action]")]
        .find((node) => node.dataset.timelineAction === focusedAction);
      (replacement ?? details.querySelector("summary"))?.focus({ preventScroll: true });
    }
  }
  details.addEventListener("toggle", updateBody);
  updateBody();
  return element("li", { className: "timeline-item",
    attrs: { "data-kind": item.kind, "data-state": item.state } }, [details]);
}

function actionIcon(name) {
  return element("span", { className: "icon", attrs: {
    "data-icon": name, "aria-hidden": "true",
  } });
}

function commandButton(label, description, iconName, actionRef, handlers, sessionKey, action) {
  const button = element("button", { className: "timeline-action-button", attrs: {
    type: "button", "aria-label": description,
    title: label,
    "data-timeline-action": actionRef,
    "data-timeline-command": "",
    "aria-disabled": String(handlers.isBusy(sessionKey)),
  } }, [actionIcon(iconName)]);
  button.addEventListener("click", () => handlers.runAction(sessionKey, action));
  return button;
}

function timelineNode(item, handlers, feedback, projectId, sessionId, openState) {
  if (item.kind === "reasoning" || item.kind === "tool")
    return foldableNode(item, openState);
  const time = timeNode(item.time);
  const header = element("div", { className: "timeline-item-header" }, [
    element("span", { className: "timeline-role", text: item.role }),
    time,
  ]);
  if (item.kind === "assistant" && item.state === "cancelled")
    header.insertBefore(element("span", { className: "timeline-stopped", text: "已停止" }), time);
  const body = element("div", { className: "timeline-body" +
    (item.kind === "assistant" ? " markdown-body" : "") });
  if (item.kind === "assistant") {
    body.append(renderMarkdown(item.text));
    for (const [index, button] of [...body.querySelectorAll(".md-code-head button")].entries())
      button.dataset.timelineAction = `${item.key}/code-${index}`;
    for (const [index, link] of [...body.querySelectorAll("a")].entries())
      link.dataset.timelineAction = `${item.key}/link-${index}`;
    for (const [index, image] of [...body.querySelectorAll("button.md-image-preview")].entries()) {
      image.dataset.timelineAction = `${item.key}/markdown-image-${index}`;
      image.dataset.imageRef = `timeline:${projectId}/${sessionId}/${item.key}/markdown/${index}`;
    }
  }
  else body.textContent = item.text;
  const children = [header, body];
  const sessionKey = `${projectId ?? ""}/${sessionId ?? ""}`;
  if (item.kind === "user" && projectId && sessionId &&
      item.attachments?.length) {
    const images = element("div", { className: "timeline-images" });
    for (const [index, id] of item.attachments.entries()) {
      if (typeof id === "string" && /^[0-9a-f]{32}$/.test(id))
        images.append(element("button", { className: "timeline-image-preview",
          attrs: { type: "button", "aria-label": `查看用户图片 ${index + 1}`,
            "data-image-preview": "",
            "data-image-ref": `timeline:${projectId}/${sessionId}/${item.key}/${id}/${index}` },
        }, [element("img", {
          attrs: { src: attachmentUrl(projectId, sessionId, id),
            alt: `用户图片 ${index + 1}`, loading: "lazy" },
        })]));
    }
    if (images.childElementCount) children.push(images);
  }
  if (item.meta) children.push(element("div", { className: "timeline-meta", text: item.meta }));
  if ((item.kind === "assistant" && item.text) ||
      (item.kind === "user" && (item.text || item.attachments?.length))) {
    const actions = element("div", { className: "timeline-actions" });
    if (item.text) {
      const copy = element("button", { className: "timeline-action-button", attrs: {
        type: "button", "aria-label": "复制消息", title: "复制",
        "data-timeline-action": `${item.key}/copy` },
      }, [actionIcon("copy")]);
      copy.addEventListener("click", async () => {
        try { await copyText(item.text); toast("消息已复制"); }
        catch { toast("无法复制消息", "error"); }
      });
      actions.append(copy);
    }
    if (item.kind === "user" &&
        Number.isSafeInteger(item.userMessageSequence) &&
        item.userMessageSequence > 0 && !item.textTruncated) {
      const edit = commandButton("编辑", "编辑此消息并重新发送", "compose", `${item.key}/edit`,
        handlers, sessionKey, () => handlers.onEdit(
          item.userMessageSequence, item.text, item.attachments ?? []));
      actions.append(edit);
    }
    if (item.kind === "assistant") {
      if ("forkThroughSequence" in item) {
        const fork = commandButton("分叉", "从此回复分叉会话", "branch", `${item.key}/fork`,
          handlers, sessionKey, () => handlers.onFork(item.forkThroughSequence));
        actions.append(fork);
      }
      const retryPrompt = item.retryPrompt;
      if (retryPrompt && Number.isSafeInteger(retryPrompt.sequence) &&
          retryPrompt.sequence > 0 && (retryPrompt.text || retryPrompt.attachments?.length) &&
          !retryPrompt.truncated) {
        const retry = commandButton("重试", "重试此回合", "retry", `${item.key}/retry`,
          handlers, sessionKey, () => handlers.onRetry(
            retryPrompt.sequence, retryPrompt.text, retryPrompt.attachments ?? []));
        actions.append(retry);
      }
      if (item.feedbackEventId && item.state === "done") {
        for (const [value, label] of [["good", "点赞"], ["bad", "点踩"]]) {
          const button = commandButton(label, label, "like",
            `${item.key}/feedback-${value}`, handlers, sessionKey,
            () => handlers.onFeedback(item.feedbackEventId,
              feedback === value ? "none" : value));
          if (value === "bad") button.classList.add("timeline-action-dislike");
          button.setAttribute("aria-pressed", String(feedback === value));
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
  const busySessions = new Set();
  const handlers = {
    onFork, onFeedback, onEdit, onRetry,
    isBusy: (key) => busySessions.has(key),
    async runAction(key, action) {
      if (busySessions.has(key)) return;
      busySessions.add(key);
      syncBusy();
      try { await action(); }
      catch (error) { toast(errorMessage(error), "error"); }
      finally { busySessions.delete(key); syncBusy(); }
    },
  };
  let pendingState = store.get();
  let frame = 0;
  let followTail = true;
  let searchQuery = "";
  let renderedSession = "";
  const expanded = new Map();
  const scroller = container.closest(".conversation");

  function syncBusy() {
    const busy = busySessions.has(renderedSession);
    for (const button of container.querySelectorAll("button[data-timeline-command]"))
      button.setAttribute("aria-disabled", String(busy));
  }

  function updateBottomButton() {
    toBottom.hidden = scroller.scrollHeight - scroller.scrollTop -
      scroller.clientHeight <= 120;
  }

  function render() {
    frame = 0;
    const state = pendingState;
    const data = state.data;
    const sessionKey = `${data?.projectId ?? ""}/${data?.sessionId ?? ""}`;
    let focusedKey = "";
    let focusedAction = "";
    let focusedImage = "";
    if (sessionKey === renderedSession) {
      const active = document.activeElement;
      if (container.contains(active)) {
        focusedAction = active?.dataset.timelineAction || "";
        focusedImage = active?.dataset.imageRef || "";
        const focusedFold = active?.closest?.("details[data-timeline-key]");
        if (focusedFold) focusedKey = focusedFold.getAttribute("data-timeline-key") || "";
      }
      for (const details of container.querySelectorAll("details[data-timeline-key]"))
        expanded.set(details.getAttribute("data-timeline-key"), details.open);
    } else {
      expanded.clear();
      renderedSession = sessionKey;
    }
    const items = eventsToTimeline(data?.events ?? [], data?.historyLost);
    const foldKeys = new Set(items.filter((item) =>
      item.kind === "reasoning" || item.kind === "tool").map((item) => item.key));
    for (const key of expanded.keys()) if (!foldKeys.has(key)) expanded.delete(key);
    const visible = searchQuery ? items.filter((item) =>
      `${item.role} ${item.inputText ?? ""} ${item.text} ${item.meta ?? ""}`
        .toLocaleLowerCase().includes(searchQuery)) : items;
    onSearchCount?.(searchQuery ? visible.length : 0, Boolean(data?.historyLost));
    welcome.hidden = Boolean(data?.sessionId && items.length > 0);
    clear(container);
    if (state.status === "error") {
      container.append(timelineNode({ key: "load-error", kind: "error", role: "无法读取时间线", text: errorMessage(state.error), state: "failed", time: 0 }, handlers, "", data?.projectId, data?.sessionId));
    } else {
      const selected = feedbackStore.get().data;
      const feedback = selected?.projectId === data?.projectId &&
        selected?.sessionId === data?.sessionId ? selected.items : new Map();
      for (const item of visible)
        container.append(timelineNode(item, handlers,
          feedback.get(item.feedbackEventId) ?? "", data?.projectId,
          data?.sessionId, expanded.get(item.key)));
    }
    mountIcons(container);
    if (focusedAction || focusedImage) {
      const replacement = focusedAction
        ? [...container.querySelectorAll("[data-timeline-action]")].find((node) =>
          node.dataset.timelineAction === focusedAction)
        : [...container.querySelectorAll("button[data-image-ref]")].find((button) =>
          button.dataset.imageRef === focusedImage);
      (replacement ?? document.querySelector("#prompt"))?.focus({ preventScroll: true });
    } else if (focusedKey) {
      const replacement = [...container.querySelectorAll("details[data-timeline-key]")]
        .find((node) => node.getAttribute("data-timeline-key") === focusedKey);
      replacement?.querySelector("summary")?.focus({ preventScroll: true });
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
