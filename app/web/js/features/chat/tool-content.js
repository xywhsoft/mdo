import { element, toast } from "../../utils/dom.js";
import { copyText } from "../../utils/clipboard.js";
import { readCompleteSessionEventText } from "../../state/sessions.js";
import { api, resourceId } from "../../api/client.js";
import { t } from "../../i18n.js";

function argumentsObject(text) {
  try {
    const value = JSON.parse(text);
    return value && typeof value === "object" && !Array.isArray(value) ? value : null;
  } catch { return null; }
}

function shortLine(value) {
  const text = String(value ?? "").replace(/\s+/g, " ").trim();
  return text.length > 110 ? `${text.slice(0, 110)}…` : text;
}

export function toolCallSummary(name, text, truncated = false) {
  let args = argumentsObject(text);
  if (!args && truncated) {
    // Accept only a complete first top-level string field. A clipped JSON
    // string, or a path mentioned inside file content, is not a target path.
    const prefix = /^\s*\{\s*"(path|file_path|command|pattern|url|query|goal|prompt)"\s*:\s*("(?:\\[\s\S]|[^"\\])*")\s*,/u.exec(text);
    if (prefix) {
      try { args = { [prefix[1]]: JSON.parse(prefix[2]) }; } catch { /* incomplete token */ }
    }
    if (!args) return t("toolContent.truncatedArguments", {}, "调用参数有截断");
  }
  if (!args) return shortLine(text);
  let summary;
  switch (name) {
    case "exec": case "spawn":
      if (Array.isArray(args.argv) && args.argv.every(part => typeof part === "string"))
        summary = args.argv.map(part => /\s|["']/u.test(part) ? JSON.stringify(part) : part).join(" ");
      break;
    case "read": case "write": case "edit": case "ls":
      summary = args.path;
      break;
    case "grep": case "search":
      if (typeof args.pattern === "string") summary = `/${args.pattern}/ · ${args.path || "."}`;
      break;
    case "poll": case "stdin": case "stop":
      if (args.task_id != null || args.id != null) summary = `#${args.task_id ?? args.id}`;
      break;
    case "wait":
      if (Array.isArray(args.task_ids)) summary = args.task_ids.map(id => `#${id}`).join(", ");
      break;
  }
  if (typeof summary === "string" && summary.trim()) return shortLine(summary);
  for (const key of ["command", "path", "file_path", "pattern", "url", "query", "goal", "prompt", "argv"]) {
    const value = args[key];
    if (typeof value === "string" && value.trim()) return shortLine(value);
    if (Array.isArray(value) && value.every(part => typeof part === "string")) return shortLine(value.join(" "));
  }
  return shortLine(text);
}

export function formatToolArguments(text) {
  const args = argumentsObject(text);
  return args ? JSON.stringify(args, null, 2) : text;
}

// This is the requested replacement fragment, not a reconstructed file diff.
// Keep the original arguments available and never invent file line numbers.
export function toolInputPreview(name, text, truncated = false) {
  if (truncated) return null;
  const args = argumentsObject(text);
  if (name === "write" && typeof args?.content === "string")
    return { kind: "write", text: args.content };
  if (name !== "edit" || !Array.isArray(args?.edits) || !args.edits.length || args.edits.length > 64 ||
      args.edits.some(edit => typeof edit?.old_text !== "string" || typeof edit?.new_text !== "string")) return null;
  const lines = [];
  let omitted = false;
  for (const [index, edit] of args.edits.entries()) {
    for (const [kind, text] of [["removed", edit.old_text], ["added", edit.new_text]]) {
      const fragment = text.split(/\r?\n/u);
      // A final newline does not add a second blank display row; the raw
      // arguments retain exact line endings for inspection and copying.
      if (text.endsWith("\n")) fragment.pop();
      for (const line of fragment) {
        if (lines.length === 200) { omitted = true; break; }
        lines.push({ index: index + 1, kind, text: line });
      }
      if (omitted) break;
    }
    if (omitted) break;
  }
  return { kind: "edit", lines, omitted };
}

async function readToolArtifact(owner, eventId) {
  const path = `/projects/${resourceId(owner.projectId, "project")}` +
    `/sessions/${resourceId(owner.sessionId, "session")}/artifacts/${eventId}`;
  const { data } = await api.get(`${path}?offset=0&limit=65536`);
  if (data?.eof !== true || !/^(text\/|application\/(json|xml|javascript))/.test(data.media_type || "")) return null;
  const bytes = Uint8Array.from(window.atob(data.data || ""), char => char.charCodeAt(0));
  if (bytes.length > 65536) return null;
  return new TextDecoder("utf-8", { fatal: true }).decode(bytes);
}

export async function resolveToolSectionText(item, part, owner, read = readCompleteSessionEventText,
  readArtifact = readToolArtifact) {
  const text = item[`${part}Text`] ?? "";
  const artifact = part === "output" && Boolean(item.artifactId);
  if (!artifact && !item[`${part}Truncated`]) return { text, complete: true };
  const id = item[`${part}EventId`];
  if (!owner?.projectId || !owner?.sessionId || !Number.isSafeInteger(id) || id < 1)
    return { text, complete: false };
  try {
    // Output artifacts are bound to this session's immutable tool-done event.
    // Its inline text is a summary, so it is not a prefix of the artifact.
    const full = artifact ? await readArtifact(owner, id)
      : await read(owner.projectId, owner.sessionId, id,
        part === "input" ? "tool_start" : "tool_done");
    if (typeof full === "string" && full.length <= 65536 && (artifact || full.startsWith(text)))
      return { text: full, complete: true };
  } catch { /* Visible text remains useful when the original is unavailable. */ }
  return { text, complete: false };
}

export function toolSectionNode(label, item, part, owner, actionRef, cache, onComplete) {
  const initial = item[`${part}Text`] ?? "";
  const key = JSON.stringify([owner.projectId, owner.sessionId, part, item[`${part}EventId`],
    part === "output" ? item.artifactId : null]);
  const cached = cache?.get(key);
  const validCache = cached?.original === initial && typeof cached.text === "string";
  let value = validCache ? cached.text : initial;
  let complete = validCache || (!item[`${part}Truncated`] && !(part === "output" && item.artifactId));
  let pending = null;
  let copying = false;
  const content = element("div", { className: "timeline-tool-content", attrs: { tabindex: "-1" } });
  const note = element("p", { className: "timeline-truncation-note", attrs: { role: "status" } });
  const copy = element("button", { text: t("timeline.copy", {}, "复制"), attrs: {
    type: "button", "aria-label": t("timeline.copySection", { label }, `复制${label}`),
    "data-timeline-action": actionRef,
  } });
  const more = element("button", { className: "timeline-tool-more",
    text: t("toolContent.loadFull", {}, "读取完整内容"), attrs: {
      type: "button", "data-timeline-action": `${actionRef}/full`,
    } });
  const section = element("div", { className: "timeline-fold-section" }, [
    element("div", { className: "timeline-fold-section-heading" }, [element("span", { text: label }), copy]),
    content, note, more,
  ]);
  function paint() {
    const oldScroll = content.querySelector("pre")?.scrollTop ?? 0;
    const rawOpen = content.querySelector("details")?.open ?? false;
    const preview = part === "input" ? toolInputPreview(item.role, value, !complete) : null;
    content.replaceChildren();
    if (preview) {
      content.append(element("p", { className: "timeline-tool-caption", text: preview.kind === "edit"
        ? t("toolContent.requestedEdits", {}, "请求的修改片段") : t("toolContent.writeContent", {}, "写入内容") }));
      if (preview.kind === "write") content.append(element("pre", { text: preview.text }));
      else {
        const diff = element("pre", { className: "timeline-tool-diff" });
        for (const line of preview.lines) diff.append(element("span", { className: `tool-diff-${line.kind}`,
          attrs: { "data-edit": line.index }, text: `${line.kind === "removed" ? "−" : "+"} ${line.text}` }));
        content.append(diff);
        if (preview.omitted) content.append(element("p", { className: "timeline-truncation-note",
          text: t("toolContent.previewLimited", {}, "预览仅显示前 200 行；完整修改见调用参数。") }));
      }
      const raw = element("details", { className: "timeline-tool-raw" }, [
        element("summary", { text: t("toolContent.rawArguments", {}, "调用参数") }),
        element("pre", { text: formatToolArguments(value) }),
      ]);
      raw.open = rawOpen;
      content.append(raw);
    } else content.append(element("pre", { text: part === "input" ? formatToolArguments(value) : value }));
    content.querySelector("pre").scrollTop = oldScroll;
    note.hidden = complete;
    if (!complete) note.textContent = t("toolContent.partial", {}, "当前只显示部分内容；复制时会尝试读取全文。");
    if (complete && document.activeElement === more) content.focus({ preventScroll: true });
    more.hidden = complete;
    if (complete) onComplete?.(value);
  }
  function load() {
    if (complete) return Promise.resolve({ text: value, complete });
    if (pending) return pending;
    more.setAttribute("aria-disabled", "true");
    pending = resolveToolSectionText(item, part, owner).then(result => {
      if (result.complete) {
        value = result.text;
        complete = true;
        if (cache) {
          cache.set(key, { original: initial, text: value });
          if (cache.size > 64) cache.delete(cache.keys().next().value);
        }
        if (section.isConnected) paint();
      } else if (section.isConnected) note.textContent = t("toolContent.fullUnavailable", {},
        "完整内容暂不可用；可复制可见部分，较大结果请查看产物。");
      return result;
    }).finally(() => { pending = null; more.removeAttribute("aria-disabled"); });
    return pending;
  }
  more.addEventListener("click", () => { void load(); });
  copy.addEventListener("click", async () => {
    if (copying) return;
    copying = true;
    copy.setAttribute("aria-disabled", "true");
    try {
      const result = await load();
      await copyText(result.text);
      toast(result.complete ? t("timeline.copied", {}, "已复制")
        : t("toolContent.partialCopied", {}, "已复制可见部分（内容截断）"));
    } catch { toast(t("timeline.copyFailed", {}, "无法复制"), "error"); }
    finally { copying = false; copy.removeAttribute("aria-disabled"); }
  });
  paint();
  return section;
}
