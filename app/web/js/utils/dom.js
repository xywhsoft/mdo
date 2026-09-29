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
  queue_cleanup_full: ["error.queueCleanupFull",
    "待清理图片已达上限，请先处理现有图片。"],
  queue_item_invalid: ["error.queueItemInvalid",
    "待发送消息无效，请检查正文和图片后重试。"],
  queue_item_not_found: ["error.queueItemNotFound",
    "这条待发送消息已不存在，请刷新队列。"],
  queue_receipt_not_found: ["error.queueReceiptNotFound",
    "找不到这条消息的运行回执；请保留原消息并核对会话历史。"],
  queue_run_conflict: ["error.queueRunConflict",
    "待发送消息与运行请求不一致，请刷新后核对，避免重复发送。"],
  queue_run_started: ["error.queueRunStarted",
    "这条消息已启动运行，请核对会话历史，不要重复发送。"],
  queue_state_invalid: ["error.queueStateInvalid",
    "待发送消息的目标状态无效，请刷新页面后重试。"],
  queue_unavailable: ["error.queueUnavailable",
    "暂时无法核对或保存待发送队列；请保留输入，稍后刷新。"],
  draft_conflict: ["error.draftConflict",
    "草稿已在其他窗口修改，请刷新后核对。"],
  draft_full: ["error.draftFull",
    "待提交输入已达上限，请先处理前面的消息。"],
  draft_invalid: ["error.draftInvalid",
    "草稿内容无效，请检查正文和图片后重试。"],
  draft_state_conflict: ["error.draftStateConflict",
    "输入的提交状态已变化，请刷新后核对。"],
  draft_state_invalid: ["error.draftStateInvalid",
    "输入的目标提交状态无效，请刷新页面后重试。"],
  draft_submission_conflict: ["error.draftSubmissionConflict",
    "同一条输入已有不同内容，请刷新并核对，避免重复提交。"],
  draft_submission_invalid: ["error.draftSubmissionInvalid",
    "待提交输入无效，请检查正文、图片和选项。"],
  draft_submission_not_found: ["error.draftSubmissionNotFound",
    "待提交输入已不存在，请刷新草稿并核对队列。"],
  draft_unavailable: ["error.draftUnavailable",
    "草稿暂时无法读取或保存，请保留输入并重试。"],
  recovery_abandon_invalid: ["error.recoveryAbandonInvalid",
    "无法结束这轮中断任务，请刷新决策状态后重试。"],
  recovery_result_unavailable: ["error.recoveryResultUnavailable",
    "暂时无法显示恢复结果，请刷新后核对会话状态。"],
  recovery_resume_invalid: ["error.recoveryResumeInvalid",
    "恢复决定无效或已过期，请刷新待决调用后重新选择。"],
  recovery_service_unavailable: ["error.recoveryServiceUnavailable",
    "中断恢复暂不可用；输入仍会保留，请稍后重试。"],
  recovery_view_too_large: ["error.recoveryViewTooLarge",
    "待恢复状态过大，无法安全显示；请保留数据并检查诊断信息。"],
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
  session_create_failed: ["error.sessionCreateFailed",
    "无法确认任务是否已创建，请刷新任务列表核对后再试。"],
  session_create_incomplete: ["error.sessionCreateIncomplete",
    "任务目录有未完成的数据，请保留并检查诊断信息，不要重复创建。"],
  session_result_unavailable: ["error.sessionResultUnavailable",
    "会话资料暂不可读；如果刚提交过更改，请刷新核对后再决定是否重试。"],
  session_service_unavailable: ["error.sessionServiceUnavailable",
    "任务服务暂不可用，请保留输入，稍后重试。"],
  session_persistence_failed: ["error.sessionPersistenceFailed",
    "任务未能写入便携数据目录，请检查存储状态并保留输入。"],
  session_read_failed: ["error.sessionReadFailed",
    "无法读取任务资料，请刷新或检查便携数据目录。"],
  session_history_unavailable: ["error.sessionHistoryUnavailable",
    "暂时无法读取会话历史，请稍后重试。"],
  session_events_unavailable: ["error.sessionEventsUnavailable",
    "暂时无法读取会话事件，请稍后重试。"],
  session_export_unavailable: ["error.sessionExportUnavailable",
    "暂时无法导出会话；原始记录仍保留，请稍后重试。"],
  attachment_invalid: ["error.attachmentInvalid",
    "图片引用缺失、损坏或超过上限，请检查图片后重试。"],
  attachment_not_found: ["error.attachmentNotFound",
    "图片已不存在，请从草稿移除并重新添加。"],
  attachment_in_use: ["error.attachmentInUse",
    "图片仍被会话引用，暂不能删除；请先移除相关引用。"],
  attachment_storage_full: ["error.attachmentStorageFull",
    "图片配额或磁盘空间不足，请清理空间后重试。"],
  attachment_response_failed: ["error.attachmentResponseFailed",
    "图片上传未完成，请重新选择图片后重试。"],
  image_body_invalid: ["error.imageBodyInvalid",
    "无法读取图片内容，请重新选择图片。"],
  image_type_invalid: ["error.imageTypeInvalid",
    "图片内容与文件类型不符，请选择有效的 PNG、JPEG 或 WebP。"],
  unsupported_media_type: ["error.unsupportedMediaType",
    "请求内容类型不受支持，请检查当前操作要求的格式。"],
  image_too_large: ["error.imageTooLarge",
    "图片超过 8 MiB 上限，请缩小后重试。"],
  workspace_files_unavailable: ["error.workspaceFilesUnavailable",
    "暂时无法列出工作区文件，请检查工作区后重试补全。"],
  ask_answer_invalid: ["error.askAnswerInvalid",
    "回答无效，请填写不超过 1024 字节的内容。"],
  asks_unavailable: ["error.asksUnavailable",
    "回答暂时未能提交，请保留内容并重试。"],
  ask_result_unavailable: ["error.askResultUnavailable",
    "回答可能已提交，请刷新询问状态后再决定是否重试。"],
  approval_decision_invalid: ["error.approvalDecisionInvalid",
    "审批选择无效，请刷新待决请求后重新选择。"],
  approvals_unavailable: ["error.approvalsUnavailable",
    "审批决定暂时未能提交，请刷新状态后重试。"],
  approval_result_unavailable: ["error.approvalResultUnavailable",
    "审批可能已处理，请刷新待决请求后再决定是否重试。"],
  run_start_invalid: ["error.runStartInvalid",
    "运行请求无效，请检查消息和配置后重试。"],
  run_result_unavailable: ["error.runResultUnavailable",
    "运行结果暂不可读，请核对会话历史后再决定是否重试。"],
  todo_unavailable: ["error.todoUnavailable",
    "暂时无法读取待办，请检查工具结果后重试。"],
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
