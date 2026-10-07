import { element, clear, formatClock, errorMessage, toast } from "../../utils/dom.js";
import { copyText } from "../../utils/clipboard.js";
import { attachmentUrl } from "../../api/client.js";
import { targetImage } from "../../api/target-image.js";
import { readCompleteSessionEventText } from "../../state/sessions.js";
import { mountIcons } from "../../components/icons.js";
import { renderMarkdown } from "./markdown.js";
import { artifactPreviewNode } from "./artifact-preview.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";
import { labelImageName } from "./image-names.js";
import { toolCallSummary, toolSectionNode } from "./tool-content.js";
import { conversationGroups } from "./conversation-history.js";
import { createConversationNavigation } from "./conversation-navigation.js";
import { modelErrorMessage, modelErrorTitle } from "../../utils/model-errors.js";

export function searchResultTop(row, content, searchBottom, viewportBottom, lineHeight = 24) {
  const room = Math.max(0, viewportBottom - searchBottom);
  const firstLine = Math.min(content.height, lineHeight);
  // A short viewport can fit the header or the first text line, but not both.
  // Give the search result's text priority; larger views retain its metadata.
  return room < content.top - row.top + firstLine ? content.top : row.top;
}

function modelKey(event, epoch) {
  return `${event.run_id || event.agent_id || event.event_id}-${epoch}-${event.agent_turn || 0}`;
}

function durationLabel(seconds) {
  const number = new Intl.NumberFormat(currentLocale(),
    { minimumFractionDigits: 1, maximumFractionDigits: 1 });
  const value = seconds < 0.1 ? `<${number.format(0.1)}` : number.format(seconds);
  return t("timeline.seconds", { seconds: value }, `${value} 秒`);
}

function historyBoundaryText(event) {
  // Existing journals use one kind for both operations. Translate only the
  // server's canonical markers; imported/custom history notes stay verbatim.
  if (event.text === "会话历史已清空")
    return t("timeline.historyCleared", {}, "会话历史已清空");
  if (!event.text || event.text === "会话历史已截断")
    return t("timeline.historyTruncated", {}, "会话历史已截断");
  return event.text;
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

export function eventsToTimeline(events, historyLost = false,
  { showHistoryTruncations = true } = {}) {
  const items = [];
  const tools = new Map();
  const modelStarts = new Map();
  const toolStageDurations = new Map();
  const runUsages = new Map();
  const streams = new Map();
  const promptsByRun = new Map();
  let latestMainPrompt = null;
  const runEpochs = new Map();
  const terminalRuns = new Map();
  const finalErrors = new Map();
  function settleStreams(runKey, epoch, state) {
    // ERROR is a terminal event in xwork, just like AGENT_DONE. Scope the
    // settlement to this execution: other Agents and reused run IDs survive.
    for (const item of items) {
      if (item.runKey === runKey && item.runEpoch === epoch && item.state === "running" &&
          ["assistant", "reasoning", "tool"].includes(item.kind)) item.state = state;
    }
  }
  // An explicit history boundary already explains why earlier events are
  // absent. A second generic gap notice would imply an unrelated loss.
  if (historyLost && events[0]?.kind !== "history_truncated") {
    items.push({ key: "history-gap", kind: "system",
      role: t("timeline.recordNotice", {}, "记录提示"),
      text: t("timeline.historyGap", {}, "更早的事件已不在当前记录中。"),
      state: "done", time: 0 });
  }
  for (const event of events) {
    const itemCount = items.length;
    const runKey = String(event.run_id || event.agent_id || event.event_id);
    const epoch = runEpochs.get(runKey) ?? 0;
    switch (event.kind) {
      case "agent_start":
        runEpochs.set(runKey, Number(event.event_id) || 0);
        runUsages.set(`${runKey}:${Number(event.event_id) || 0}`,
          { calls: 0, input: 0, output: 0, modelSeconds: 0,
            valid: true, durationValid: true });
        if (event.agent_depth === 0 && Number(event.user_message_sequence) > 0) {
          latestMainPrompt = {
            sourceEventId: Number(event.event_id),
            sequence: Number(event.user_message_sequence),
            text: event.text || "",
            textTruncated: Boolean(event.text_truncated),
            copySpans: event.text_truncated ? [{ eventId: event.event_id,
              kind: event.kind, start: 0, end: (event.text || "").length }] : [],
            attachments: event.attachments || [],
          };
          promptsByRun.set(runKey, latestMainPrompt);
        } else if (!(event.agent_depth > 0)) {
          // Resume adds no user message. Its reply still belongs to the most
          // recent retained main-Agent input, including its images and full
          // text reference. Missing history must never invent an action target.
          if (!(Number(event.schema_version) >= 3 &&
                Number(event.user_message_sequence || 0) === 0)) latestMainPrompt = null;
          if (latestMainPrompt) promptsByRun.set(runKey, latestMainPrompt);
          else promptsByRun.delete(runKey);
        }
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
            text: event.text || "", state: "done", time: event.time,
            attachments: Array.isArray(event.attachments) ? event.attachments : [],
            userMessageSequence: Number(event.user_message_sequence || 0),
            sourceEventId: Number(event.event_id),
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
        const usage = runUsages.get(`${runKey}:${epoch}`);
        if (usage) {
          const input = Number(event.input_tokens);
          const output = Number(event.output_tokens);
          const startedAt = modelStarts.get(modelKey(event, epoch));
          const elapsed = (Number(event.time) - Number(startedAt)) / 1e6;
          usage.calls += 1;
          if (!Number.isSafeInteger(input) || input < 0 ||
              !Number.isSafeInteger(output) || output < 0 ||
              !Number.isSafeInteger(usage.input + input) ||
              !Number.isSafeInteger(usage.output + output)) usage.valid = false;
          else if (usage.valid) {
            usage.input += input;
            usage.output += output;
          }
          if (startedAt == null || !Number.isFinite(elapsed) || elapsed <= 0)
            usage.durationValid = false;
          else if (usage.durationValid) usage.modelSeconds += elapsed;
        }
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
          answer.inputTokens = Number(event.input_tokens || 0);
          answer.outputTokens = Number(event.output_tokens || 0);
          const startedAt = modelStarts.get(modelKey(event, epoch));
          const elapsed = (Number(event.time) - Number(startedAt ?? answer.time)) / 1e6;
          if (startedAt != null && Number.isFinite(Number(startedAt)) && elapsed > 0)
            answer.modelDurationSeconds = elapsed;
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
          inputEventId: event.event_id,
          inputTruncated: event.text_truncated === true,
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
          tool.outputEventId = event.event_id;
          tool.outputTruncated = event.text_truncated === true;
          tool.state = event.success ? "done" : "failed";
          // Tool start precedes ask/approval; this is wall time, not executor time.
          tool.durationSeconds = Math.max(0,
            (Number(event.time) - Number(tool.time)) / 1e6);
          if (Number.isFinite(tool.durationSeconds) && tool.durationSeconds > 0) {
            const key = `${runKey}:${epoch}`;
            toolStageDurations.set(key,
              (toolStageDurations.get(key) || 0) + tool.durationSeconds);
          }
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
            outputEventId: event.event_id,
            outputTruncated: event.text_truncated === true,
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
      case "error": {
        terminalRuns.set(`${runKey}:${epoch}`, "failed");
        settleStreams(runKey, epoch, "failed");
        const answer = [...items].reverse().find((item) => item.kind === "assistant" &&
          item.runKey === runKey && item.runEpoch === epoch);
        if (answer) answer.state = "failed";
        const text = modelErrorMessage(event.model_error_kind,
          event.text || t("timeline.agentFailed", {}, "Agent 运行失败"));
        const role = modelErrorTitle(event.model_error_kind,
          t("timeline.runError", {}, "运行错误"));
        const errorKey = `${runKey}:${epoch}`;
        const previous = finalErrors.get(errorKey);
        if (previous) {
          previous.text = text;
          previous.role = role;
          previous.time = event.time;
          break;
        }
        const failure = { key: `error-${event.event_id}`,
          kind: event.agent_depth > 0 ? "task" : "error",
          role,
          text, state: "failed", time: event.time };
        finalErrors.set(errorKey, failure);
        items.push(failure);
        break;
      }
      case "agent_done": {
        // Some hosts report both a final error and cleanup completion. Keep
        // the failure and partial reply instead of adding a cancellation card.
        if (terminalRuns.get(`${runKey}:${epoch}`) === "failed") break;
        const terminalState = event.success ? "done" : "cancelled";
        terminalRuns.set(`${runKey}:${epoch}`, terminalState);
        settleStreams(runKey, epoch, terminalState);
        let answer = [...items].reverse().find((item) => item.kind === "assistant" &&
          item.runKey === runKey && item.runEpoch === epoch);
        if (!answer && (event.text || !event.success)) {
          answer = {
            key: `done-${event.event_id}`, kind: "assistant",
            role: event.model || "Agent", text: event.text || "",
            state: terminalState, time: event.time,
            runKey, runEpoch: epoch, retryPrompt: promptsByRun.get(runKey),
            textTruncated: Boolean(event.text_truncated),
            copySpans: event.text_truncated ? [{ eventId: event.event_id,
              kind: event.kind, start: 0, end: (event.text || "").length }] : [],
          };
          items.push(answer);
        }
        if (answer) {
          answer.state = terminalState;
          const toolSeconds = toolStageDurations.get(`${runKey}:${epoch}`);
          if (Number.isFinite(toolSeconds) && toolSeconds > 0)
            answer.toolStageSeconds = toolSeconds;
          const usage = runUsages.get(`${runKey}:${epoch}`);
          if (usage?.valid && usage.calls === 1 && answer.inputTokens == null) {
            answer.inputTokens = usage.input;
            answer.outputTokens = usage.output;
            if (usage.durationValid && usage.modelSeconds > 0) {
              answer.modelDurationSeconds = usage.modelSeconds;
              if (usage.output > 0)
                answer.tokensPerSecond = usage.output / usage.modelSeconds;
            }
          }
          if (usage?.valid && usage.calls > 1) {
            answer.runUsage = { calls: usage.calls, input: usage.input,
              output: usage.output };
            if (usage.durationValid && usage.modelSeconds > 0) {
              answer.modelDurationSeconds = usage.modelSeconds;
              if (usage.output > 0)
                answer.tokensPerSecond = usage.output / usage.modelSeconds;
            } else {
              delete answer.modelDurationSeconds;
              delete answer.tokensPerSecond;
            }
          }
        }
        break;
      }
      case "history_truncated": {
        // Editing/retrying records the removed event range for replay. Keep
        // those markers in exports, but avoid adding a chat card per retry.
        // Clears, imported notes and unrecognized boundaries remain visible.
        const first = Number(event.source_event_id);
        const end = Number(event.event_id);
        if (Number.isSafeInteger(first) && first > 0 &&
            Number.isSafeInteger(end) && first <= end) {
          const discarded = prompt => prompt &&
            prompt.sourceEventId >= first && prompt.sourceEventId < end;
          if (discarded(latestMainPrompt)) latestMainPrompt = null;
          for (const [key, prompt] of promptsByRun)
            if (discarded(prompt)) promptsByRun.delete(key);
        }
        if (!showHistoryTruncations &&
            (!event.text || event.text === "会话历史已截断") &&
            Number.isSafeInteger(first) && first > 0 &&
            Number.isSafeInteger(end) && first <= end) break;
        items.push({ key: `history-${event.event_id}`, kind: "system",
          role: t("timeline.history", {}, "历史"),
          text: historyBoundaryText(event),
          state: "done", time: event.time });
        break;
      }
      case "model_start":
        if (event.text === "stream_restart") {
          // A failed generation has not committed a response or dispatched
          // tools. Replace only its transient draft, keeping earlier work.
          const key = modelKey(event, epoch);
          for (const prefix of ["reasoning-", "assistant-"]) {
            const draft = streams.get(prefix + key);
            if (draft) {
              const index = items.indexOf(draft);
              if (index !== -1) items.splice(index, 1);
              streams.delete(prefix + key);
            }
          }
        } else modelStarts.set(modelKey(event, epoch), event.time);
        break;
      default:
        items.push({ key: `event-${event.event_id}`, kind: "system",
          role: t("timeline.event", {}, "事件"),
          text: event.text || event.kind || t("timeline.unknownEvent", {}, "未知事件"),
          state: event.terminal ? "done" : "running", time: event.time });
    }
    for (let index = itemCount; index < items.length; index += 1) {
      items[index].runKey ??= runKey;
      items[index].runEpoch ??= event.kind === "agent_start" ? Number(event.event_id) || 0 : epoch;
      items[index].agentDepth ??= event.agent_depth || 0;
    }
  }
  for (const item of items)
    item.turnState = terminalRuns.get(`${item.runKey}:${item.runEpoch}`) ?? "running";
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

function foldableNode(item, openState, previewOpen, previewScroll, projectId, sessionId, toolTextCache) {
  const running = item.state === "running";
  const status = running ? t("timeline.running", {}, "运行中")
    : item.state === "failed" ? t("timeline.failed", {}, "失败")
      : item.state === "cancelled" ? t("timeline.stopped", {}, "已停止")
        : t("timeline.done", {}, "完成");
  const lastLine = item.text?.trimEnd().split("\n").at(-1) || "";
  const preview = item.kind === "reasoning"
    ? (running ? shortLine(lastLine) : "")
    : toolCallSummary(item.role, item.inputText || item.outputText || item.text,
      Boolean(item.inputText && item.inputTruncated));
  const duration = Number.isFinite(item.durationSeconds) && item.durationSeconds > 0
    ? t("timeline.seconds", { seconds: new Intl.NumberFormat(currentLocale(),
      { minimumFractionDigits: 1, maximumFractionDigits: 1 }).format(item.durationSeconds) },
    `${item.durationSeconds.toFixed(1)} 秒`) : "";
  const elapsed = duration && item.kind === "tool"
    ? t("timeline.toolElapsed", { duration }, `总历时 ${duration}`) : "";
  const foldStatus = elapsed && (item.state === "failed" || item.state === "cancelled")
    ? `${status} · ${elapsed}` : elapsed || duration || status;
  const details = element("details", { className: "timeline-fold",
    attrs: { "data-timeline-key": item.key } });
  details.open = openState ?? running;
  const summaryPreview = element("span", { className: "timeline-fold-preview", text: preview });
  details.append(element("summary", { className: "timeline-fold-summary" }, [
    element("span", { className: "timeline-fold-marker", attrs: { "aria-hidden": "true" } }),
    element("span", { className: "timeline-role", text: item.role }),
    summaryPreview,
    element("span", { className: "timeline-fold-status", text: foldStatus }),
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
      const owner = { projectId, sessionId };
      if (item.inputText) body.append(toolSectionNode(t("timeline.call", {}, "调用"), item, "input", owner,
        `${item.key}/tool-input`, toolTextCache, value => {
          summaryPreview.textContent = toolCallSummary(item.role, value);
        }));
      if (item.outputText) body.append(toolSectionNode(
        item.state === "failed" ? t("timeline.errorOutput", {}, "错误输出")
          : t("timeline.result", {}, "结果"), item, "output", owner,
        `${item.key}/tool-output`, toolTextCache));
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

function timelineNode(item, handlers, projectId, sessionId, writable,
  openState, previewOpen, previewScroll, toolTextCache) {
  if (item.kind === "process") {
    const details = element("details", { className: "conversation-process", attrs: {
      "data-timeline-key": item.key, "data-state": item.state } });
    details.open = openState ?? item.state === "running";
    const count = item.items.filter(child => child.kind === "tool").length;
    const label = item.state === "running" ? t("timeline.processRunning", {}, "执行过程")
      : t("timeline.processComplete", {}, "查看思考与执行过程");
    const summary = element("summary", {}, [actionIcon("chevron-down"),
      element("span", { text: label }), count ? element("span", {
        className: "conversation-process-count", text: t("timeline.processTools", { count }, `${count} 次工具调用`) }) : null]);
    details.append(summary);
    function expand() {
      if (!details.open || details.querySelector(".conversation-process-items")) return;
      const children = element("ol", { className: "conversation-process-items" });
      for (const child of item.items) children.append(timelineNode(child, handlers,
        projectId, sessionId, false, undefined, false, 0, toolTextCache));
      details.append(children);
      mountIcons(children);
    }
    details.addEventListener("toggle", expand);
    expand();
    return element("li", { className: "timeline-item", attrs: { "data-kind": "process" } }, [details]);
  }
  if (item.kind === "history-more") {
    const button = element("button", { className: "conversation-gap", text:
      t("timeline.loadBetween", {}, "加载这之间的对话"), attrs: { type: "button" } });
    button.addEventListener("click", async () => {
      button.disabled = true;
      try { await handlers.revealTurn?.(item.turnId, false); }
      catch (error) { toast(errorMessage(error), "error"); button.disabled = false; }
    });
    return element("li", { className: "timeline-item" }, [button]);
  }
  if (item.kind === "reasoning" || item.kind === "tool")
    return foldableNode(item, openState, previewOpen, previewScroll,
      projectId, sessionId, toolTextCache);
  const time = timeNode(item.time);
  const header = element("div", { className: "timeline-item-header" }, [
    item.kind === "user" ? null :
      element("span", { className: "timeline-role", text: item.role }),
    time,
  ]);
  if (item.kind === "assistant" && item.state === "cancelled")
    header.insertBefore(element("span", { className: "timeline-stopped",
      text: t("timeline.stopped", {}, "已停止") }), time);
  if (item.kind === "assistant" && item.state === "failed")
    header.insertBefore(element("span", { className: "timeline-failed",
      text: t("timeline.failed", {}, "失败") }), time);
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
      if (typeof id === "string" && /^[0-9a-f]{32}$/.test(id)) {
        const caption = element("bdi", { className: "image-file-name",
          text: t("timeline.userImage", { index: index + 1 }, `用户图片 ${index + 1}`) });
        const preview = element("button", { className: "timeline-image-preview",
          attrs: { type: "button", "aria-label": t("timeline.viewImage",
            { index: index + 1 }, `查看用户图片 ${index + 1}`),
            "data-image-preview": "",
            "data-image-ref": `timeline:${projectId}/${sessionId}/${item.key}/${id}/${index}` },
        }, [targetImage(attachmentUrl(projectId, sessionId, id), {
            alt: t("timeline.userImage", { index: index + 1 },
              `用户图片 ${index + 1}`), loading: "lazy",
        }), caption]);
        images.append(preview);
        labelImageName({ preview, caption, owner, id,
          viewLabel: (name) => t("image.viewNamed", { number: index + 1, name },
            `查看图片 ${index + 1}：${name}`) });
      }
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
          opener, () => resolveTimelineActionText(item, owner), item.sourceEventId));
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
            () => resolveTimelineActionText(retryPrompt, owner), retryPrompt.sourceEventId));
        actions.append(retry);
      }
      const stats = [];
      if (item.runUsage)
        stats.push(t("timeline.runUsage", item.runUsage,
          `本轮 ${item.runUsage.calls} 次模型调用 · ${item.runUsage.input} 输入 / ${item.runUsage.output} 输出 tokens`));
      else if (item.inputTokens || item.outputTokens)
        stats.push(t("timeline.usage", { input: item.inputTokens || 0,
          output: item.outputTokens || 0 },
        `${item.inputTokens || 0} 输入 / ${item.outputTokens || 0} 输出 tokens`));
      if (Number.isFinite(item.modelDurationSeconds) && item.modelDurationSeconds > 0) {
        stats.push(`LLM ${durationLabel(item.modelDurationSeconds)}`);
      }
      if (Number.isFinite(item.toolStageSeconds) && item.toolStageSeconds > 0)
        stats.push(t("timeline.toolStageTime", { duration: durationLabel(item.toolStageSeconds) },
          `工具和等待合计 ${durationLabel(item.toolStageSeconds)}`));
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
  onFork, onEdit, onRetry, onSearchCount, onLoadOlder, onLoadIndex, onRevealTurn, onReload }) {
  const busySessions = new Set();
  const renderedRows = new Map();
  const handlers = {
    onFork, onEdit, onRetry,
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
  const toolTextCache = new Map();
  const processStates = new Map();
  const scroller = container.closest(".conversation");
  const historyButton = onLoadOlder ? element("button", { className: "conversation-history-load",
    attrs: { type: "button" } }) : null;
  if (historyButton) {
    container.before(historyButton);
    historyButton.addEventListener("click", () => {
      followTail = false;
      if (store.get().status === "error" && store.get().data?.initializing) void onReload?.();
      else void onLoadOlder();
    });
  }
  let jumpVersion = 0;
  async function revealTurn(id, scroll = true) {
    const version = ++jumpVersion;
    const owner = renderedSession;
    followTail = false;
    if (searchQuery) { searchQuery = ""; onSearchCount?.(0, false); }
    await onRevealTurn?.(id);
    await new Promise(resolve => requestAnimationFrame(resolve));
    if (version !== jumpVersion || owner !== renderedSession) return;
    if (scroll) container.querySelector(`[data-turn-id="${id}"]`)?.scrollIntoView({ block: "start" });
  }
  handlers.revealTurn = revealTurn;
  const historyNavigation = onRevealTurn ? createConversationNavigation({
    scroller, container, onReveal: revealTurn, onLoadIndex,
  }) : null;

  function reconcileRows(entries, projectId, sessionId) {
    const retained = new Set();
    let cursor = container.firstChild;
    for (const { item, writable } of entries) {
      retained.add(item.key);
      const signature = JSON.stringify([item, writable]);
      let row = renderedRows.get(item.key);
      if (!row || row.signature !== signature) {
        const node = timelineNode(item, handlers, projectId,
          sessionId, writable, expanded.get(item.key),
          previewExpanded.get(`${item.key}/preview`) ?? false,
          previewScroll.get(`${item.key}/preview`) ?? 0, toolTextCache);
        node.dataset.timelineRow = item.key;
        if (item.kind === "user") node.dataset.turnId = item.sourceEventId;
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
    let anchor = null;
    let anchorTop = 0;
    if (sessionKey === renderedSession && !followTail && !searchQuery) {
      const top = scroller.getBoundingClientRect().top;
      anchor = [...container.children].find(node => node.getBoundingClientRect().bottom > top + 8);
      anchorTop = anchor?.getBoundingClientRect().top ?? 0;
    }
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
      toolTextCache.clear();
      processStates.clear();
      jumpVersion += 1;
      renderedRows.clear();
      clear(container);
      renderedSession = sessionKey;
      // A history position belongs to the previous session. New tasks start
      // at the welcome heading; existing sessions open on their latest turn.
      followTail = true;
    }
    const items = eventsToTimeline(data?.events ?? [], data?.historyLost,
      { showHistoryTruncations: false });
    const groups = conversationGroups(items);
    const projected = [];
    let previousTurn = 0;
    for (const group of groups) {
      if (group.standalone) { projected.push(group.standalone); continue; }
      const skipped = (data?.turns ?? []).filter(turn => turn.first_event_id > previousTurn &&
        turn.first_event_id < group.firstEventId && !groups.some(loaded => loaded.firstEventId === turn.first_event_id));
      if (previousTurn && skipped.length) projected.push({ key: `gap-${group.firstEventId}`,
        kind: "history-more", turnId: skipped.at(-1).first_event_id });
      previousTurn = group.firstEventId;
      projected.push(group.user);
      if (group.process.length) {
        const key = `process-${group.firstEventId}`;
        if (processStates.has(key) && processStates.get(key) !== group.state)
          expanded.set(key, group.state === "running");
        processStates.set(key, group.state);
        projected.push({ key, kind: "process", items: group.process, state: group.state });
      }
      if (group.answer) projected.push(group.answer);
    }
    const foldKeys = new Set([...items.filter((item) =>
      item.kind === "reasoning" || item.kind === "tool").map((item) => item.key),
      ...projected.filter(item => item.kind === "process").map(item => item.key)]);
    for (const key of expanded.keys()) if (!foldKeys.has(key)) expanded.delete(key);
    const previewKeys = new Set(items.filter((item) => item.artifactId)
      .map((item) => `${item.key}/preview`));
    for (const key of previewExpanded.keys())
      if (!previewKeys.has(key)) previewExpanded.delete(key);
    for (const key of previewScroll.keys())
      if (!previewKeys.has(key)) previewScroll.delete(key);
    const visible = searchQuery ? items.filter((item) =>
      `${item.role} ${item.inputText ?? ""} ${item.text} ${item.meta ?? ""}`
        .toLocaleLowerCase().includes(searchQuery)) : projected;
    onSearchCount?.(searchQuery ? visible.length : 0, Boolean(data?.historyLost || data?.hasOlder));
    // A replacement can briefly contain only its hidden history boundary.
    // Keep this conversation open instead of flashing new-task examples.
    welcome.hidden = Boolean(data?.sessionId &&
      (items.length > 0 || data?.events?.length > 0 || data?.initializing));
    const entries = [];
    if (state.status === "error") {
      entries.push({ item: { key: "load-error", kind: "error",
        role: t("timeline.loadError", {}, "无法读取时间线"),
        text: errorMessage(state.error), state: "failed", time: 0 } });
    }
    {
      const session = sessionStore?.get().data;
      const writable = !sessionStore || (session?.status === "active" &&
        session.project_id === data?.projectId && session.id === data?.sessionId);
      for (const item of visible)
        entries.push({ item, writable });
    }
    reconcileRows(entries, data?.projectId, data?.sessionId);
    if (historyButton) {
      const failedInitialLoad = state.status === "error" && data?.initializing;
      historyButton.hidden = !data?.hasOlder && !failedInitialLoad;
      historyButton.disabled = Boolean(data?.loadingHistory);
      historyButton.textContent = data?.loadingHistory ? t("timeline.loadingOlder", {}, "正在加载更早对话…")
        : data?.historyError || failedInitialLoad ? t("timeline.retryOlder", {}, "加载失败，点击重试")
          : t("timeline.loadOlder", {}, "加载更早的对话");
    }
    historyNavigation?.update(data);
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
      if (first && searchBar) {
        const content = first.querySelector(".timeline-body") ?? first;
        const bottom = searchBar.getBoundingClientRect().bottom;
        const target = searchResultTop(first.getBoundingClientRect(),
          content.getBoundingClientRect(), bottom, scroller.getBoundingClientRect().bottom,
          parseFloat(getComputedStyle(content).lineHeight) || 24);
        scroller.scrollTop += target - bottom;
      } else scroller.scrollTop = 0;
    } else if (followTail && !searchQuery) scroller.scrollTop = scroller.scrollHeight;
    else if (anchor?.isConnected) scroller.scrollTop += anchor.getBoundingClientRect().top - anchorTop;
    updateBottomButton();
  }

  function queueRender(state) {
    pendingState = state;
    if (!frame) frame = requestAnimationFrame(render);
  }

  scroller.addEventListener("scroll", () => {
    followTail = scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight < 100;
    updateBottomButton();
    if (!searchQuery && scroller.scrollTop < 100 && pendingState.data?.hasOlder &&
        !pendingState.data?.loadingHistory && !followTail) void onLoadOlder?.();
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
      unsubscribe(); unsubscribeSession?.(); unsubscribeLocale();
      historyNavigation?.destroy(); historyButton?.remove();
      if (frame) cancelAnimationFrame(frame);
    },
  });
}
