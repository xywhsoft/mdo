import { currentLocale, t } from "../i18n.js";

export function element(tag, options = {}, children = []) {
  const node = document.createElement(tag);
  if (options.className) node.className = options.className;
  if (options.text !== undefined) node.textContent = String(options.text);
  for (const [name, value] of Object.entries(options.attrs ?? {})) {
    if (value !== null && value !== undefined) node.setAttribute(name, String(value));
  }
  for (const child of children) {
    if (child !== null && child !== undefined) node.append(child);
  }
  return node;
}

export function clear(node) {
  node.replaceChildren();
}

// Some WebViews report the key that commits an IME candidate with
// isComposing=false but keyCode=229. Keep it out of send/menu shortcuts.
export function isImeKey(event, composing = false) {
  return composing || event.isComposing || event.keyCode === 229;
}

export function revealListOption(list, option) {
  if (!option) return;
  const top = option.offsetTop;
  const bottom = top + option.offsetHeight;
  if (top < list.scrollTop) list.scrollTop = top;
  else if (bottom > list.scrollTop + list.clientHeight) {
    list.scrollTop = bottom - list.clientHeight;
  }
}

export function formatRelativeTime(microseconds) {
  const time = Number(microseconds) / 1000;
  if (!Number.isFinite(time) || time <= 0) return "";
  const seconds = Math.round((time - Date.now()) / 1000);
  const formatter = new Intl.RelativeTimeFormat(currentLocale(), { numeric: "auto" });
  if (Math.abs(seconds) < 60) return formatter.format(seconds, "second");
  const minutes = Math.round(seconds / 60);
  if (Math.abs(minutes) < 60) return formatter.format(minutes, "minute");
  const hours = Math.round(minutes / 60);
  if (Math.abs(hours) < 24) return formatter.format(hours, "hour");
  const days = Math.round(hours / 24);
  if (Math.abs(days) < 14) return formatter.format(days, "day");
  return new Intl.DateTimeFormat(currentLocale(), { month: "short", day: "numeric" }).format(time);
}

export function formatClock(microseconds) {
  const time = Number(microseconds) / 1000;
  if (!Number.isFinite(time) || time <= 0) return "";
  return new Intl.DateTimeFormat(currentLocale(), { hour: "2-digit", minute: "2-digit" }).format(time);
}

// Stable API codes describe user actions; server messages remain useful for
// unknown failures, but should not determine the language of known failures.
const API_ERROR_COPY = Object.freeze({
  invalid_response: ["error.invalidResponse", "服务返回了无效响应，请重试。"],
  session_profile_invalid: ["error.sessionProfileInvalid",
    "会话配置无效，请检查模型、思考强度和权限。"],
  image_model_unsupported: ["error.imageModelUnsupported",
    "当前模型不支持图片输入，请切换模型或移除图片。"],
  run_limit_reached: ["error.runLimitReached",
    "同时运行的任务已达上限，请稍后重试。"],
  run_service_unavailable: ["error.runServiceUnavailable",
    "模型服务暂不可用，请检查模型配置后重试。"],
  run_receipt_unavailable: ["error.runReceiptUnavailable",
    "运行可能已开始，但队列回执未保存；请核对运行记录。"],
  queue_run_starting: ["error.queueRunStarting",
    "这条消息已经进入启动流程；请核对运行记录后处理，避免重复发送。"],
  run_start_uncertain: ["error.runStartUncertain",
    "运行可能已经开始，请核对运行记录后再决定是否重试。"],
  queue_full: ["error.queueFull", "待发送队列已满，请先处理已有消息。"],
  queue_state_conflict: ["error.queueStateConflict",
    "待发送消息状态已变化，请刷新后核对。"],
  queue_item_consumed: ["error.queueItemConsumed",
    "这条待发送消息已启动运行，请核对运行记录。"],
  draft_conflict: ["error.draftConflict",
    "草稿已在其他窗口修改，请刷新后核对。"],
  draft_too_large: ["draft.tooLarge", "草稿超过 64 KiB 保存上限"],
  attachment_unavailable: ["error.attachmentUnavailable",
    "图片存储暂不可用，请重试。"],
  session_state_conflict: ["error.sessionStateConflict",
    "会话状态已变化，请刷新后重试。"],
});

export function errorMessage(error) {
  if (error?.code === "network_error") return t("error.network", {},
    "无法连接本地服务，请确认 mdo 仍在运行。");
  if (error?.code === "session_busy") return t("error.sessionBusy", {},
    "这个会话仍有任务在运行。");
  if (error?.code === "session_not_found") return t("error.sessionNotFound", {},
    "会话已经不存在，请刷新列表。");
  if (error?.code === "revision_conflict") return t("error.revisionConflict", {},
    "内容已在其他窗口更新，请刷新后重试。");
  if (error?.code === "recovery_state_conflict") return t("error.recoveryConflict", {},
    "中断状态已经变化，请核对刷新后的调用再决定。");
  if (error?.code === "migration_conflict") return t("error.migrationConflict", {},
    "迁移来源、目标或预览令牌已经变化，请重新检测后确认。");
  if (error?.code === "migration_invalid") return t("error.migrationInvalid", {},
    "旧数据未通过当前版本的迁移校验。");
  const known = Object.hasOwn(API_ERROR_COPY, error?.code)
    ? API_ERROR_COPY[error.code] : null;
  if (known) return t(known[0], {}, known[1]);
  return error?.message || t("error.generic", {}, "操作未完成，请重试。");
}

export function toast(message, tone = "neutral") {
  const region = document.querySelector("#toast-region");
  if (!region) return;
  const item = element("div", { className: "toast", text: message, attrs: { "data-tone": tone, role: "status" } });
  region.append(item);
  window.setTimeout(() => item.remove(), 4200);
}
