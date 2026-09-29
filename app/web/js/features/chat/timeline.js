import { element, clear, formatClock, errorMessage, toast } from "../../utils/dom.js";
import { copyText } from "../../utils/clipboard.js";
import { attachmentUrl } from "../../api/client.js";
import { readCompleteSessionEventText } from "../../state/sessions.js";
import { mountIcons } from "../../components/icons.js";
import { renderMarkdown } from "./markdown.js";
import { artifactPreviewNode } from "./artifact-preview.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";

function modelKey(event, epoch) {
  return `${event.run_id || event.agent_id || event.event_id}-${epoch}-${event.agent_turn || 0}`;
}

function appendOrCreate(items, streams, event, kind, role, key) {
  let item = streams.get(key);
  if (!item) {
    item = { key, kind, role, text: "", time: event.time, state: "running", meta: "" };
    items.push(item);
    streams.set(key, item);
  }
  const start = item.text.length;
  item.text += event.text ?? "";
  if (event.text_truncated) {
    item.textTruncated = true;
    (item.copySpans ??= []).push({ eventId: event.event_id,
      kind: event.kind, start, end: item.text.length });
  }
  return item;
}

export function eventsToTimeline(events, historyLost = false) {
  const items = [];
  const tools = new Map();
  const modelStarts = new Map();
  const streams = new Map();
  const promptsByRun = new Map();
  const runEpochs = new Map();
  // An explicit history boundary already explains why earlier events are
  // absent. A second generic gap notice would imply an unrelated loss.
  if (historyLost && events[0]?.kind !== "history_truncated") {
    items.push({ key: "history-gap", kind: "system",
      role: t("timeline.recordNotice", {}, "记录提示"),
      text: t("timeline.historyGap", {}, "更早的事件已不在当前记录中。"),
      state: "done", time: 0 });
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
            textTruncated: Boolean(event.text_truncated),
            copySpans: event.text_truncated ? [{ eventId: event.event_id,
              kind: event.kind, start: 0, end: (event.text || "").length }] : [],
            attachments: event.attachments || [],
          });
        if (event.agent_depth > 0) {
          items.push({ key: `subagent-${event.event_id}`, kind: "task",
            role: t("timeline.subagent", {}, "子 Agent"),
            text: event.text || t("timeline.subagentStarted", {}, "子 Agent 已启动"),
            state: "running", time: event.time, meta: `depth ${event.agent_depth}` });
        } else if (Number(event.schema_version) >= 3 &&
                   Number(event.user_message_sequence || 0) === 0) {
          // A resumed run has no new user message. Older event schemas did not
          // carry a durable message sequence, so keep their original projection.
          items.push({ key: `resume-${event.event_id}`, kind: "system",
            role: t("timeline.recovery", {}, "恢复"),
            text: t("timeline.resumed", {}, "从上次中断处继续运行"), state: "done",
            time: event.time });
        } else {
          items.push({ key: `user-${event.event_id}`, kind: "user",
            role: t("timeline.you", {}, "你"),
            text: event.text || "", state: "done", time: event.time,
            attachments: Array.isArray(event.attachments) ? event.attachments : [],
            userMessageSequence: Number(event.user_message_sequence || 0),
            textTruncated: Boolean(event.text_truncated),
            copySpans: event.text_truncated ? [{ eventId: event.event_id,
              kind: event.kind, start: 0, end: (event.text || "").length }] : [] });
        }
        break;
      case "model_reasoning_delta": {
        const thought = appendOrCreate(items, streams, event, "reasoning",
          t("timeline.reasoning", {}, "思考"),
          `reasoning-${modelKey(event, epoch)}`);
        thought.runKey = runKey;
        thought.runEpoch = epoch;
        break;
      }
      case "model_text_delta":
        {
          const answer = appendOrCreate(items, streams, event, "assistant", event.model || "Agent", `assistant-${modelKey(event, epoch)}`);
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
          role: event.tool_name || t("timeline.tool", {}, "工具"),
          text: event.text || t("timeline.executing", {}, "正在执行…"),
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
          tool.outputText = event.text || (event.success
            ? t("timeline.executionDone", {}, "执行完成")
            : t("timeline.executionFailed", {}, "执行失败"));
          tool.text = tool.outputText;
          tool.state = event.success ? "done" : "failed";
          tool.durationSeconds = Math.max(0,
            (Number(event.time) - Number(tool.time)) / 1e6);
          tool.artifactId = event.artifact_id;
          tool.artifactEventId = event.event_id;
          tool.artifactPath = event.artifact_path;
        } else {
          const outputText = event.text || (event.success
            ? t("timeline.executionDone", {}, "执行完成")
            : t("timeline.executionFailed", {}, "执行失败"));
          items.push({ key: `tool-${event.event_id}`, kind: "tool",
            role: event.tool_name || t("timeline.tool", {}, "工具"), text: outputText,
            inputText: "", outputText,
            artifactId: event.artifact_id, artifactPath: event.artifact_path,
            artifactEventId: event.event_id,
            state: event.success ? "done" : "failed", time: event.time });
        }
        break;
      }
      case "task_updated":
        items.push({ key: `task-${event.event_id}`, kind: "task",
          role: t("timeline.backgroundTask", {}, "后台任务"),
          text: event.text || t("timeline.taskUpdated", { id: event.task_id },
            `任务 #${event.task_id} 已更新`),
          state: event.terminal ? "done" : "running", time: event.time,
          meta: event.task_id ? `task ${event.task_id}` : "" });
        break;
      case "artifact_created":
        items.push({ key: `artifact-${event.event_id}`, kind: "system",
          role: t("timeline.artifact", {}, "产物"),
          text: event.artifact_path || event.text || t("timeline.artifactCreated", {}, "已创建产物"),
          artifactId: event.artifact_id,
          artifactEventId: event.event_id,
          state: "done", time: event.time });
        break;
      case "compaction_start":
      case "compaction_done":
      case "compaction_rejected":
        items.push({ key: `context-${event.event_id}`, kind: "system",
          role: t("timeline.context", {}, "上下文"),
          text: event.text || (event.kind === "compaction_start"
            ? t("timeline.compacting", {}, "正在整理上下文…")
            : event.kind === "compaction_done"
              ? t("timeline.compacted", {}, "上下文整理完成")
              : t("timeline.compactionRejected", {}, "上下文整理未应用")),
          state: event.kind === "compaction_start" ? "running" : "done", time: event.time });
        break;
      case "recovery_required":
      case "recovery_resolved":
        items.push({ key: `recovery-${event.event_id}`, kind: "system",
          role: t("timeline.recovery", {}, "恢复"),
          text: event.text || (event.kind === "recovery_required"
            ? t("timeline.recoveryRequired", {}, "会话需要恢复")
            : t("timeline.recoveryResolved", {}, "会话已恢复")),
          state: event.kind === "recovery_required" ? "failed" : "done", time: event.time });
        break;
      case "error":
        items.push({ key: `error-${event.event_id}`, kind: "error",
          role: t("timeline.runError", {}, "运行错误"),
          text: event.text || t("timeline.agentFailed", {}, "Agent 运行失败"),
          state: "failed", time: event.time });
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
          textTruncated: Boolean(event.text_truncated),
          copySpans: event.text_truncated ? [{ eventId: event.event_id,
            kind: event.kind, start: 0, end: (event.text || "").length }] : [],
        });
        break;
      }
      case "history_truncated":
        items.push({ key: `history-${event.event_id}`, kind: "system",
          role: t("timeline.history", {}, "历史"),
          text: event.text || t("timeline.historyTruncated", {}, "会话历史已截断"),
          state: "done", time: event.time });
        break;
      case "model_start":
        modelStarts.set(modelKey(event, epoch), event.time);
        break;
      default:
        items.push({ key: `event-${event.event_id}`, kind: "system",
          role: t("timeline.event", {}, "事件"),
          text: event.text || event.kind || t("timeline.unknownEvent", {}, "未知事件"),
          state: event.terminal ? "done" : "running", time: event.time });
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

export async function resolveTimelineCopyText(item, owner,
  readEventText = readCompleteSessionEventText) {
  const visible = item.text ?? "";
  if (!item.textTruncated) return { text: visible, complete: true };
  const spans = item.copySpans;
  if (!owner?.projectId || !owner?.sessionId || !Array.isArray(spans) ||
      spans.length === 0 || spans.length > 16) return { text: visible, complete: false };
  let result = "";
  let offset = 0;
  try {
    for (const span of spans) {
      if (!Number.isSafeInteger(span.eventId) || span.eventId < 1 ||
          !Number.isSafeInteger(span.start) || !Number.isSafeInteger(span.end) ||
          span.start < offset || span.end < span.start || span.end > visible.length)
        return { text: visible, complete: false };
      const full = await readEventText(owner.projectId, owner.sessionId,
        span.eventId, span.kind);
      if (typeof full !== "string" || !full.startsWith(
        visible.slice(span.start, span.end)))
        return { text: visible, complete: false };
      result += visible.slice(offset, span.start) + full;
      offset = span.end;
      if (result.length > 1048576) return { text: visible, complete: false };
    }
  } catch { return { text: visible, complete: false }; }
  const complete = result + visible.slice(offset);
  return complete.length <= 1048576
    ? { text: complete, complete: true }
    : { text: visible, complete: false };
}

export async function resolveTimelineActionText(item, owner,
  readEventText = readCompleteSessionEventText) {
  const resolved = await resolveTimelineCopyText(item, owner, readEventText);
  if (!resolved.complete) throw new Error(t("messageAction.fullTextUnavailable", {},
    "无法取回完整消息；当前会话历史没有改动，请刷新后重试。"));
  return resolved.text;
}

function timeNode(value) {
  const time = element("time", { className: "timeline-time", text: formatClock(value) });
  if (value) {
    const date = new Date(Number(value) / 1000);
    time.dateTime = date.toISOString();
    time.title = date.toLocaleString(currentLocale());
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
    const button = element("button", { text: t("timeline.copy", {}, "复制"), attrs: {
      type: "button", "aria-label": t("timeline.copySection", { label }, `复制${label}`),
      "data-timeline-action": actionRef,
    } });
    button.addEventListener("click", async () => {
      try { await copyText(value); toast(t("timeline.copied", {}, "已复制")); }
      catch { toast(t("timeline.copyFailed", {}, "无法复制"), "error"); }
    });
    heading.append(button);
  }
  return element("div", { className: "timeline-fold-section" }, [
    heading, element("pre", { text: value }),
  ]);
}

function foldableNode(item, openState, previewOpen, previewScroll, projectId, sessionId) {
  const running = item.state === "running";
  const status = running ? t("timeline.running", {}, "运行中")
    : item.state === "failed" ? t("timeline.failed", {}, "失败")
      : item.state === "cancelled" ? t("timeline.stopped", {}, "已停止")
        : t("timeline.done", {}, "完成");
  const lastLine = item.text?.trimEnd().split("\n").at(-1) || "";
  const preview = item.kind === "reasoning"
    ? (running ? shortLine(lastLine) : "")
    : toolSummaryText(item.inputText || item.outputText || item.text);
  const duration = Number.isFinite(item.durationSeconds) && item.durationSeconds > 0
    ? t("timeline.seconds", { seconds: new Intl.NumberFormat(currentLocale(),
      { minimumFractionDigits: 1, maximumFractionDigits: 1 }).format(item.durationSeconds) },
    `${item.durationSeconds.toFixed(1)} 秒`) : "";
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
    if (!details.open) {
      if (focusedAction) details.querySelector("summary")?.focus({ preventScroll: true });
      return;
    }
    if (details.querySelector(".timeline-fold-body")) return;
    const body = element("div", { className: "timeline-fold-body" });
    if (item.kind === "reasoning") {
      body.append(foldSection(t("timeline.reasoningProcess", {}, "思考过程"),
        item.text || t("timeline.thinking", {}, "正在思考…")));
    } else {
      if (item.inputText) body.append(foldSection(t("timeline.call", {}, "调用"), item.inputText, true,
        `${item.key}/tool-input`));
      if (item.outputText) body.append(foldSection(
        item.state === "failed" ? t("timeline.errorOutput", {}, "错误输出")
          : t("timeline.result", {}, "结果"), item.outputText, true,
        `${item.key}/tool-output`));
      if (!item.inputText && !item.outputText)
        body.append(foldSection(t("timeline.executingLabel", {}, "执行中"),
          item.text || t("timeline.executing", {}, "正在执行…")));
      if (item.artifactId) {
        const preview = artifactPreviewNode(projectId, sessionId,
          item.artifactEventId, `${item.key}/preview`, previewOpen, previewScroll);
        if (preview) body.append(preview);
      }
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
  button.addEventListener("click", () => handlers.runAction(sessionKey, () => action(button)));
  return button;
}

function timelineNode(item, handlers, feedback, projectId, sessionId, writable,
  openState, previewOpen, previewScroll) {
  if (item.kind === "reasoning" || item.kind === "tool")
    return foldableNode(item, openState, previewOpen, previewScroll,
      projectId, sessionId);
  const time = timeNode(item.time);
  const header = element("div", { className: "timeline-item-header" }, [
    element("span", { className: "timeline-role", text: item.role }),
    time,
  ]);
  if (item.kind === "assistant" && item.state === "cancelled")
    header.insertBefore(element("span", { className: "timeline-stopped",
      text: t("timeline.stopped", {}, "已停止") }), time);
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
  if ((item.kind === "user" || item.kind === "assistant") &&
      item.textTruncated) children.push(element("p", {
    className: "timeline-truncation-note",
    text: t("timeline.partialMessage", {},
      "消息内容有截断；复制时会尝试获取全文，无法获取时仅复制可见部分。"),
  }));
  if (item.artifactId) {
    const preview = artifactPreviewNode(projectId, sessionId,
      item.artifactEventId, `${item.key}/preview`, previewOpen, previewScroll);
    if (preview) children.push(preview);
  }
  const sessionKey = `${projectId ?? ""}/${sessionId ?? ""}`;
  const owner = { projectId, sessionId };
  if (item.kind === "user" && projectId && sessionId &&
      item.attachments?.length) {
    const images = element("div", { className: "timeline-images" });
    for (const [index, id] of item.attachments.entries()) {
      if (typeof id === "string" && /^[0-9a-f]{32}$/.test(id))
        images.append(element("button", { className: "timeline-image-preview",
          attrs: { type: "button", "aria-label": t("timeline.viewImage",
            { index: index + 1 }, `查看用户图片 ${index + 1}`),
            "data-image-preview": "",
            "data-image-ref": `timeline:${projectId}/${sessionId}/${item.key}/${id}/${index}` },
        }, [element("img", {
          attrs: { src: attachmentUrl(projectId, sessionId, id),
            alt: t("timeline.userImage", { index: index + 1 },
              `用户图片 ${index + 1}`), loading: "lazy" },
        })]));
    }
    if (images.childElementCount) children.push(images);
  }
  if (item.meta) children.push(element("div", { className: "timeline-meta", text: item.meta }));
  if ((item.kind === "assistant" && item.text) ||
      (item.kind === "user" && (item.text || item.attachments?.length))) {
    const actions = element("div", { className: "timeline-actions" });
    if (item.text) {
      const copyLabel = t("timeline.copyMessage", {}, "复制消息");
      const copy = element("button", { className: "timeline-action-button", attrs: {
        type: "button", "aria-label": copyLabel,
        title: item.textTruncated ? copyLabel : t("timeline.copy", {}, "复制"),
        "data-timeline-action": `${item.key}/copy` },
      }, [actionIcon("copy")]);
      let copying = false;
      copy.addEventListener("click", async () => {
        if (copying) return;
        copying = true;
        copy.setAttribute("aria-disabled", "true");
        try {
          const resolved = await resolveTimelineCopyText(item, owner);
          await copyText(resolved.text);
          toast(resolved.complete
            ? t("timeline.messageCopied", {}, "消息已复制")
            : t("timeline.visibleMessageCopied", {}, "已复制可见部分"));
        }
        catch { toast(t("timeline.messageCopyFailed", {}, "无法复制消息"), "error"); }
        finally { copying = false; copy.setAttribute("aria-disabled", "false"); }
      });
      actions.append(copy);
    }
    if (writable && item.kind === "user" &&
        Number.isSafeInteger(item.userMessageSequence) &&
        item.userMessageSequence > 0) {
      const edit = commandButton(t("timeline.edit", {}, "编辑"),
        t("timeline.editResend", {}, "编辑此消息并重新发送"), "compose", `${item.key}/edit`,
        handlers, sessionKey, (opener) => handlers.onEdit(
          item.userMessageSequence, item.text, item.attachments ?? [], owner,
          opener, () => resolveTimelineActionText(item, owner)));
      actions.append(edit);
    }
    if (item.kind === "assistant") {
      if (writable && "forkThroughSequence" in item) {
        const fork = commandButton(t("timeline.fork", {}, "分叉"),
          t("timeline.forkFromReply", {}, "从此回复分叉会话"), "branch", `${item.key}/fork`,
          handlers, sessionKey, () => handlers.onFork(item.forkThroughSequence, owner));
        actions.append(fork);
      }
      const retryPrompt = item.retryPrompt;
      if (writable && retryPrompt && Number.isSafeInteger(retryPrompt.sequence) &&
          retryPrompt.sequence > 0 && (retryPrompt.text || retryPrompt.attachments?.length)) {
        const retry = commandButton(t("timeline.retry", {}, "重试"),
          t("timeline.retryTurn", {}, "重试此回合"), "retry", `${item.key}/retry`,
          handlers, sessionKey, () => handlers.onRetry(
            retryPrompt.sequence, retryPrompt.text,
            retryPrompt.attachments ?? [], owner,
            () => resolveTimelineActionText(retryPrompt, owner)));
        actions.append(retry);
      }
      if (item.feedbackEventId && item.state === "done") {
        for (const [value, label] of [["good", t("timeline.like", {}, "点赞")],
          ["bad", t("timeline.dislike", {}, "点踩")]]) {
          const button = commandButton(label, label, "like",
            `${item.key}/feedback-${value}`, handlers, sessionKey,
            () => handlers.onFeedback(item.feedbackEventId,
              feedback === value ? "none" : value, owner));
          if (value === "bad") button.classList.add("timeline-action-dislike");
          button.setAttribute("aria-pressed", String(feedback === value));
          actions.append(button);
        }
      }
      const stats = [];
      if (item.inputTokens || item.outputTokens)
        stats.push(t("timeline.usage", { input: item.inputTokens || 0,
          output: item.outputTokens || 0 },
        `${item.inputTokens || 0} 输入 / ${item.outputTokens || 0} 输出 tokens`));
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

export function createTimelineView({ container, welcome, toBottom, store, sessionStore = null,
  feedbackStore, onFork, onFeedback, onEdit, onRetry, onSearchCount }) {
  const busySessions = new Set();
  const renderedRows = new Map();
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
  let revealFirstSearchMatch = false;
  let renderedSession = "";
  const expanded = new Map();
  const previewExpanded = new Map();
  const previewScroll = new Map();
  const scroller = container.closest(".conversation");

  function reconcileRows(entries, projectId, sessionId) {
    const retained = new Set();
    let cursor = container.firstChild;
    for (const { item, feedback, writable } of entries) {
      retained.add(item.key);
      const signature = JSON.stringify([item, feedback, writable]);
      let row = renderedRows.get(item.key);
      if (!row || row.signature !== signature) {
        const node = timelineNode(item, handlers, feedback, projectId,
          sessionId, writable, expanded.get(item.key),
          previewExpanded.get(`${item.key}/preview`) ?? false,
          previewScroll.get(`${item.key}/preview`) ?? 0);
        mountIcons(node);
        row = { node, signature };
        renderedRows.set(item.key, row);
      }
      if (row.node !== cursor) container.insertBefore(row.node, cursor);
      cursor = row.node.nextSibling;
    }
    while (cursor) {
      const next = cursor.nextSibling;
      cursor.remove();
      cursor = next;
    }
    for (const key of renderedRows.keys())
      if (!retained.has(key)) renderedRows.delete(key);
  }

  function syncBusy() {
    const busy = busySessions.has(renderedSession);
    for (const button of container.querySelectorAll("button[data-timeline-command]"))
      button.setAttribute("aria-disabled", String(busy));
  }

  function updateBottomButton() {
    toBottom.hidden = !pendingState.data?.sessionId || scroller.scrollHeight - scroller.scrollTop -
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
      for (const details of container.querySelectorAll("details.timeline-artifact-preview")) {
        const action = details.querySelector("summary")?.dataset.timelineAction;
        if (action) {
          previewExpanded.set(action, details.open);
          const content = details.querySelector(".timeline-artifact-content");
          if (content?.clientHeight) previewScroll.set(action, content.scrollTop);
        }
      }
    } else {
      expanded.clear();
      previewExpanded.clear();
      previewScroll.clear();
      renderedRows.clear();
      clear(container);
      renderedSession = sessionKey;
      // A history position belongs to the previous session. New tasks start
      // at the welcome heading; existing sessions open on their latest turn.
      followTail = true;
    }
    const items = eventsToTimeline(data?.events ?? [], data?.historyLost);
    const foldKeys = new Set(items.filter((item) =>
      item.kind === "reasoning" || item.kind === "tool").map((item) => item.key));
    for (const key of expanded.keys()) if (!foldKeys.has(key)) expanded.delete(key);
    const previewKeys = new Set(items.filter((item) => item.artifactId)
      .map((item) => `${item.key}/preview`));
    for (const key of previewExpanded.keys())
      if (!previewKeys.has(key)) previewExpanded.delete(key);
    for (const key of previewScroll.keys())
      if (!previewKeys.has(key)) previewScroll.delete(key);
    const visible = searchQuery ? items.filter((item) =>
      `${item.role} ${item.inputText ?? ""} ${item.text} ${item.meta ?? ""}`
        .toLocaleLowerCase().includes(searchQuery)) : items;
    onSearchCount?.(searchQuery ? visible.length : 0, Boolean(data?.historyLost));
    welcome.hidden = Boolean(data?.sessionId && items.length > 0);
    const entries = [];
    if (state.status === "error") {
      entries.push({ item: { key: "load-error", kind: "error",
        role: t("timeline.loadError", {}, "无法读取时间线"),
        text: errorMessage(state.error), state: "failed", time: 0 }, feedback: "" });
    } else {
      const selected = feedbackStore.get().data;
      const feedback = selected?.projectId === data?.projectId &&
        selected?.sessionId === data?.sessionId ? selected.items : new Map();
      const session = sessionStore?.get().data;
      const writable = !sessionStore || (session?.status === "active" &&
        session.project_id === data?.projectId && session.id === data?.sessionId);
      for (const item of visible)
        entries.push({ item, feedback: feedback.get(item.feedbackEventId) ?? "", writable });
    }
    reconcileRows(entries, data?.projectId, data?.sessionId);
    if ((focusedAction || focusedImage) && !container.contains(document.activeElement)) {
      const replacement = focusedAction
        ? [...container.querySelectorAll("[data-timeline-action]")].find((node) =>
          node.dataset.timelineAction === focusedAction)
        : [...container.querySelectorAll("button[data-image-ref]")].find((button) =>
          button.dataset.imageRef === focusedImage);
      (replacement ?? document.querySelector("#prompt"))?.focus({ preventScroll: true });
    } else if (focusedKey && !container.contains(document.activeElement)) {
      const replacement = [...container.querySelectorAll("details[data-timeline-key]")]
        .find((node) => node.getAttribute("data-timeline-key") === focusedKey);
      replacement?.querySelector("summary")?.focus({ preventScroll: true });
    }
    if (!data?.sessionId) scroller.scrollTop = 0;
    else if (revealFirstSearchMatch) {
      revealFirstSearchMatch = false;
      const first = container.querySelector(".timeline-item");
      const searchBar = scroller.querySelector(".conversation-find:not([hidden])");
      if (first && searchBar)
        scroller.scrollTop += first.getBoundingClientRect().top -
          searchBar.getBoundingClientRect().bottom;
      else scroller.scrollTop = 0;
    } else if (followTail && !searchQuery) scroller.scrollTop = scroller.scrollHeight;
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
  container.addEventListener("scroll", (event) => {
    const content = event.target;
    if (!content.classList?.contains("timeline-artifact-content") ||
        !content.clientHeight) return;
    const action = content.parentElement?.querySelector("summary")?.dataset.timelineAction;
    if (action) previewScroll.set(action, content.scrollTop);
  }, { capture: true, passive: true });
  toBottom.addEventListener("click", () => {
    followTail = true;
    scroller.scrollTo({ top: scroller.scrollHeight, behavior: "smooth" });
  });

  const unsubscribe = store.subscribe(queueRender);
  const unsubscribeSession = sessionStore?.subscribe(() => queueRender(store.get()));
  const unsubscribeFeedback = feedbackStore.subscribe(() => queueRender(store.get()));
  const unsubscribeLocale = subscribeLocale(() => {
    renderedRows.clear();
    queueRender(store.get());
  });
  return Object.freeze({
    follow() { followTail = true; },
    restorePreviewScroll() {
      for (const details of container.querySelectorAll("details.timeline-artifact-preview")) {
        if (!details.open) continue;
        const action = details.querySelector("summary")?.dataset.timelineAction;
        const content = details.querySelector(".timeline-artifact-content");
        if (action && content?.clientHeight && previewScroll.has(action))
          content.scrollTop = previewScroll.get(action);
      }
    },
    search(query) {
      const next = query.trim().toLocaleLowerCase();
      revealFirstSearchMatch = Boolean(next) && next !== searchQuery;
      searchQuery = next;
      queueRender(store.get());
    },
    destroy() {
      unsubscribe(); unsubscribeSession?.(); unsubscribeFeedback(); unsubscribeLocale();
      if (frame) cancelAnimationFrame(frame);
    },
  });
}
