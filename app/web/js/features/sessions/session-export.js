import { eventsToTimeline } from "../chat/timeline.js";
import { currentLocale, t } from "../../i18n.js";

function singleLine(value) {
  return String(value ?? "").replace(/\s+/g, " ").trim();
}

function imageLabel(value) {
  return String(value).replace(/[\\`*_{}\[\]()#+\-.!<>|]/g, "\\$&");
}

function fenced(value) {
  const content = String(value ?? "");
  let longest = 2;
  for (const [run] of content.matchAll(/`+/g)) longest = Math.max(longest, run.length);
  const fence = "`".repeat(longest + 1);
  return `${fence}\n${content}\n${fence}`;
}

function messageTime(microseconds) {
  const milliseconds = Number(microseconds) / 1000;
  if (!Number.isFinite(milliseconds) || milliseconds <= 0) return "";
  const date = new Date(milliseconds);
  return Number.isNaN(date.getTime()) ? "" : ` · ${date.toLocaleString(currentLocale())}`;
}

export function sessionMarkdownFilename(session) {
  const title = singleLine(session.title || t("sessionAction.untitled", {}, "未命名任务"))
    .replace(/[\\/:*?"<>|\u0000-\u001f]/g, "_")
    .slice(0, 96).replace(/[. ]+$/g, "");
  return `mdo-${title || "session"}.md`;
}

export function formatSessionMarkdown(session, transcript, exportedAt = new Date()) {
  const title = singleLine(session.title || t("sessionAction.untitled", {}, "未命名任务"));
  const exportedTime = exportedAt.toLocaleString(currentLocale());
  const lines = [
    `# ${title}`,
    "",
    `_${t("sessionExport.exportedAt", { date: exportedTime },
      `导出于 ${exportedTime}`)} · mdo · ${session.project_id}/${session.id}_`,
    "",
  ];
  if (transcript.historyLost)
    lines.push(`> ${t("sessionExport.historyLost", {},
      "记录提示：较早的事件已从有界日志中滚出，本文只包含可回放部分。")}`, "");
  if (transcript.limitReached)
    lines.push(`> ${t("sessionExport.limitReached", {},
      "记录提示：已达到单次导出 4096 条事件上限，后续事件未包含。")}`, "");
  if (transcript.textTruncated)
    lines.push(`> ${t("sessionExport.textTruncated", {},
      "记录提示：部分事件正文未能完整导出。")}`, "");

  let count = 0;
  const imageDefinitions = new Map();
  let imagesIncomplete = Boolean(transcript.imagesIncomplete);
  for (const item of eventsToTimeline(transcript.events ?? [])) {
    if (item.key === "history-gap" || item.kind === "reasoning") continue;
    if (!item.text && !item.attachments?.length && item.kind !== "tool") continue;
    const role = item.kind === "user" ? t("sessionExport.user", {}, "用户") :
      item.kind === "assistant" ? t("sessionExport.assistant", {}, "助手") :
        item.kind === "tool" ? `${t("sessionExport.tool", {}, "工具")} · ${singleLine(item.role)}` :
          singleLine(item.role || t("sessionExport.event", {}, "事件"));
    lines.push(`## ${role}${messageTime(item.time)}`, "");
    if (item.kind === "tool") {
      if (item.inputText) lines.push(t("sessionExport.call", {}, "调用："), "", fenced(item.inputText), "");
      if (item.outputText) lines.push(t("sessionExport.result", {}, "结果："), "", fenced(item.outputText), "");
      else if (item.text) lines.push(item.text, "");
    } else if (item.text) lines.push(String(item.text), "");
    if (item.attachments?.length) {
      lines.push(t("sessionExport.images", {}, "图片附件："), "");
      for (const id of item.attachments) {
        const image = transcript.images?.get(id);
        if (image) {
          // Reference definitions carry each original image only once, even
          // when retries repeat it in many messages. The file is self-contained.
          const reference = `mdo-image-${id}`;
          lines.push(`![${imageLabel(image.name)}][${reference}]`);
          imageDefinitions.set(reference, image.dataUrl);
        } else {
          lines.push(`- ${id}`);
          imagesIncomplete = true;
        }
      }
      lines.push("");
    }
    count += 1;
  }
  if (!count) lines.push(`_${t("sessionExport.empty", {}, "暂无对话消息。")}_`, "");
  if (imagesIncomplete) lines.push(`> ${t("sessionExport.imagesIncomplete", {},
    "记录提示：部分图片未包含（图片缺失、读取失败或达到导出上限）；保留的附件 ID 需要原 Home。")}`, "");
  for (const [reference, dataUrl] of imageDefinitions)
    lines.push(`[${reference}]: ${dataUrl}`, "");
  return lines.join("\n");
}
