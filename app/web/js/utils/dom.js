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

// Update only time text. Rebuilding a list here would discard keyboard focus,
// the open action menu, and the reader's current scroll position.
export function refreshRelativeTimes(root = document) {
  for (const node of root.querySelectorAll("[data-relative-time]")) {
    const label = formatRelativeTime(node.dataset.relativeTime);
    if (node.textContent !== label) node.textContent = label;
  }
}

export function formatClock(microseconds) {
  const time = Number(microseconds) / 1000;
  if (!Number.isFinite(time) || time <= 0) return "";
  return new Intl.DateTimeFormat(currentLocale(), { hour: "2-digit", minute: "2-digit" }).format(time);
}

// Stable API codes describe user actions; server messages remain useful for
// unknown failures, but should not determine the language of known failures.
const API_ERROR_COPY = Object.freeze({
  network_error: ["error.network",
    "无法连接本地服务，请确认 mdo 仍在运行。"],
  session_busy: ["error.sessionBusy", "这个会话仍有任务在运行。"],
  session_not_found: ["error.sessionNotFound",
    "会话已经不存在，请刷新列表。"],
  revision_conflict: ["error.revisionConflict",
    "内容已在其他窗口更新，请刷新后重试。"],
  recovery_state_conflict: ["error.recoveryConflict",
    "中断状态已经变化，请核对刷新后的调用再决定。"],
  migration_conflict: ["error.migrationConflict",
    "迁移来源、目标或预览令牌已经变化，请重新检测后确认。"],
  migration_invalid: ["error.migrationInvalid",
    "旧数据未通过当前版本的迁移校验。"],
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
  image_cleanup_unavailable: ["error.imageCleanupUnavailable",
    "清理记录未能保存，图片仍在草稿中。请重试。"],
  image_cleanup_full: ["error.imageCleanupFull",
    "待清理图片已达上限，图片仍在草稿中。请先处理其他图片。"],
  session_state_conflict: ["error.sessionStateConflict",
    "会话状态已变化，请刷新后重试。"],
  precondition_required: ["error.preconditionRequired",
    "缺少当前版本信息，请刷新页面后重试。"],
  settings_unavailable: ["error.settingsUnavailable",
    "设置暂时无法读取，请稍后重试。"],
  configuration_persistence_failed: ["error.configurationSaveFailed",
    "设置未能保存，请检查便携数据目录后重试。"],
  project_not_found: ["error.projectNotFound",
    "项目已不存在，请刷新项目列表。"],
  project_exists: ["error.projectExists",
    "项目标识已存在，请更换工作区目录或在项目设置中指定其他标识。"],
  project_invalid: ["error.projectInvalid",
    "项目目录或名称无效，请检查后重试。"],
  project_unavailable: ["error.projectUnavailable",
    "项目未能保存，请检查便携数据目录后重试。"],
  purge_preview_unavailable: ["error.purgePreviewUnavailable",
    "无法完整检查项目数据；请勿依据旧清单操作，稍后重试。"],
  session_create_invalid: ["error.sessionCreateInvalid",
    "无法创建任务，请检查标题和所选配置。"],
  session_create_conflict: ["error.sessionCreateConflict",
    "任务创建请求与已有任务冲突，请刷新并核对任务列表。"],
  schedule_busy: ["error.scheduleBusy",
    "计划当前无法删除，请等待运行结束后重试。"],
  schedule_state_conflict: ["error.scheduleStateConflict",
    "计划状态已变化，请刷新后重试。"],
  approval_not_found: ["error.approvalNotFound",
    "审批请求已不存在或已在其他窗口处理，请刷新后核对。"],
  ask_not_found: ["error.askNotFound",
    "询问已不存在或已在其他窗口回答，请刷新后核对。"],
  run_not_found: ["error.runNotFound",
    "运行记录已不在当前服务中，请检查会话历史。"],
});

export function errorMessage(error) {
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
