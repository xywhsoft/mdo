import { currentLocale, t } from "../i18n.js";
import { modelErrorMessage } from "./model-errors.js";

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
  remote_offline: ["error.remoteOffline", "目标设备离线，操作不会转到本机。"],
  target_settings_pending: ["error.targetSettingsPending", "请先保存或放弃未保存的设置，再切换设备。"],
  remote_result_unconfirmed: ["error.remoteUnconfirmed", "远程写入结果尚未确认，请核对目标状态，勿重复提交。"],
  remote_runtime_changed: ["error.remoteRuntimeChanged", "目标设备已重启，请保留未保存草稿并重新载入页面。"],
  remote_revoked: ["error.remoteRevoked", "设备授权已撤销，请返回本机或选择其他设备。"],
  remote_native_unavailable: ["error.remoteNativeUnavailable", "此操作需要在目标设备的原生窗口中完成。"],
  read_only: ["error.remoteReadOnly", "当前设备连接为只读模式。"],
  target_switch_busy: ["error.targetSwitchBusy", "请等待操作完成，保存或复制未保存草稿后切换设备。"],
  invalid_directory_path: ["error.invalidDirectoryPath", "请输入有效的目录路径（不超过 2048 字节）。"],
  directory_unavailable: ["error.directoryUnavailable", "无法打开此目录，请检查路径和访问权限。"],
  write_token_required: ["error.writeTokenRequired", "请重新载入页面，取得当前服务的写入版本后再继续操作。"],
  write_token_invalid: ["error.writeTokenInvalid", "页面的写入版本无效，请重新载入后继续。"],
  write_token_conflict: ["error.writeTokenConflict", "项目已清除或服务已重启。请先复制尚未保存的草稿，再重新载入页面。"],
  service_restarted: ["shell.serviceRestarted", "本地服务已重启。请先保留未保存的草稿，再重新载入继续。"],
  purge_client_busy: ["error.purgeClientBusy", "请等待提交、图片上传和其他操作完成，并处理运行态或诊断项，再核对清除。"],
  purge_draft_unsaved: ["error.purgeDraftUnsaved", "仍有未保存的草稿。请先保存，或复制草稿后重新载入，再核对清除结果。"],
  write_admission_busy: ["error.writeAdmissionBusy", "已有写入或项目清除仍在处理，请等待完成后重新核对原请求。"],
  write_admission_unavailable: ["error.writeAdmissionUnavailable", "写入保护暂时不可用，请保持原请求并重新启动 mdo。"],
  purge_review_required: ["error.purgeReviewRequired", "请先核对项目清除请求，再继续修改或发送任务。"],
  network_error: ["error.network",
    "无法连接本地服务，请确认 mdo 仍在运行。"],
  session_busy: ["error.sessionBusy", "这个会话仍有任务在运行。"],
  session_capture_busy: ["error.sessionCaptureBusy", "会话数据正在保存或清理，请稍后重新导出。"],
  session_backup_busy: ["error.sessionBackupBusy", "另一个会话备份正在进行，请稍后重试。"],
  session_backup_limit: ["error.sessionBackupLimit", "会话备份超出大小或时间限额，请稍后重试或减少保留内容。"],
  session_backup_invalid: ["error.sessionBackupInvalid", "会话记录存在不可读内容或缺失附件，暂时无法完整备份。"],
  session_backup_unavailable: ["error.sessionBackupUnavailable", "备份服务暂时不可用，原始会话仍保留，请稍后重试。"],
  backup_download_invalid: ["error.backupDownloadInvalid", "备份响应不完整或格式异常，没有生成下载文件，请重新导出。"],
  backup_download_checksum: ["error.backupDownloadChecksum", "备份校验和不一致，没有生成下载文件，请重新导出。"],
  backup_download_timeout: ["error.backupDownloadTimeout", "备份整理或下载超时，没有生成下载文件，请稍后重试。"],
  backup_download_failed: ["error.backupDownloadFailed", "备份下载失败，原会话仍保留，请稍后重试。"],
  backup_upload_invalid: ["error.backupUploadInvalid", "备份上传信息或分段格式无效，请重新选择原始备份文件。"],
  backup_upload_limit: ["error.backupUploadLimit", "备份文件最多 96 MiB，每个上传分段最多 256 KiB。"],
  backup_upload_busy: ["error.backupUploadBusy", "另一个备份正在上传或校验，请结束后重试。"],
  backup_upload_conflict: ["error.backupUploadConflict", "上传内容、进度或状态不一致，请核对已接收的进度后重试。"],
  backup_upload_incomplete: ["error.backupUploadIncomplete", "备份文件尚未完整接收，请继续上传。"],
  backup_upload_checksum: ["error.backupUploadChecksum", "备份文件校验不一致，请重新上传原始文件。"],
  backup_upload_not_found: ["error.backupUploadNotFound", "上传已过期、取消或结束，请重新选择备份文件。"],
  backup_upload_unavailable: ["error.backupUploadUnavailable", "备份上传服务暂时不可用，请稍后重试。"],
  backup_upload_timeout: ["error.backupUploadTimeout", "上传超时，请先清理本次上传。"],
  backup_preview_failed: ["error.backupPreviewFailed", "备份校验失败，请核对服务端说明。"],
  backup_preview_invalid: ["error.backupPreviewInvalid", "备份预览请求无效。"],
  backup_preview_busy: ["error.backupPreviewBusy", "另一份备份仍在预览或恢复。"],
  backup_preview_not_found: ["error.backupPreviewNotFound", "预览已过期或取消。"],
  backup_preview_unavailable: ["error.backupPreviewUnavailable", "预览服务暂不可用。"],
  restore_request_invalid: ["error.restoreInvalid", "恢复请求无效，请重新审核目标。"],
  restore_review_invalid: ["error.restoreInvalid", "恢复目标无效，请重新审核。"],
  restore_review_unavailable: ["error.restoreReviewUnavailable", "无法准备恢复审核，请查询当前审核。"],
  restore_target_unavailable: ["error.restoreTargetUnavailable", "目标项目或工作区不可用。"],
  restore_preview_unavailable: ["error.backupPreviewUnavailable", "备份预览暂不可用。"],
  restore_partial_backup: ["error.restorePartial", "旧版不完整导出不能恢复完整会话。"],
  restore_busy: ["error.restoreBusy", "另一项恢复或审核仍在进行。"],
  restore_result_unavailable: ["error.restoreResultUnavailable", "结果暂不可读，请保留原 ID 查询。"],
  restore_unavailable: ["error.restoreResultUnavailable", "恢复暂不可用，请保留原 ID 查询。"],
  restore_request_not_found: ["error.restoreNotFound", "暂未找到原请求，请保留原 ID 核对。"],
  restore_review_not_found: ["error.restoreNotFound", "原审核已不可用，请保留原 ID 核对。"],
  restore_admission_pending: ["error.restoreAdmissionPending", "原恢复正在接受，请按原 ID 查询。"],
  restore_bookmark_invalid: ["error.restoreBookmarkInvalid", "URL 中的恢复身份不完整，请核对原 ID。"],
  restore_bookmark_unavailable: ["error.restoreBookmarkUnavailable", "无法保留恢复 ID，本次没有继续提交。"],
  restore_identity_mismatch: ["error.restoreIdentityMismatch", "响应与原恢复身份不一致。"],
  restore_observation_timeout: ["error.restoreObservationTimeout", "查询超时，请按原请求 ID 查询。"],
  session_message_changed: ["error.sessionMessageChanged",
    "原消息已在其他窗口更改或移除。请保留编辑内容，刷新会话后重新选择消息。"],
  session_not_found: ["error.sessionNotFound",
    "会话已经不存在，请刷新列表。"],
  revision_conflict: ["error.revisionConflict",
    "内容已在其他窗口更新，请刷新后重试。"],
  recovery_state_conflict: ["error.recoveryConflict",
    "中断状态已经变化，请核对刷新后的调用再决定。"],
  migration_conflict: ["error.migrationConflict",
    "迁移条件已变化或相关项目正忙，请等待当前操作结束，再重新检测。"],
  migration_invalid: ["error.migrationInvalid",
    "旧数据未通过当前版本的迁移校验。"],
  migration_storage_unsupported: ["error.migrationStorageUnsupported",
    "当前位置的文件系统不支持安全导入所需的原子操作。请将便携目录放到支持这些操作的文件系统，再导入。"],
  home_import_busy: ["error.homeImportBusy", "旧数据正在导入，请等待完成。"],
  home_restart_required: ["error.homeRestartRequired",
    "导入的数据需要重启后启用，请关闭并重新启动 mdo。"],
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
  project_busy: ["error.projectBusy",
    "项目数据正在变更，请稍后重试；本次操作未执行。"],
  purge_preview_unavailable: ["error.purgePreviewUnavailable",
    "无法完整检查项目数据；请勿依据旧清单操作，稍后重试。"],
  purge_invalid: ["error.purgeInvalid", "清除请求参数无效，请刷新清单后核对。"],
  purge_request_invalid: ["error.purgeRequestInvalid", "清除请求编号无效，请保留原记录并核对。"],
  purge_request_not_found: ["error.purgeRequestNotFound",
    "尚无已接受的清除请求或结果；请核对原请求，不要另起请求。"],
  purge_unavailable: ["error.purgeUnavailable",
    "清除暂时无法完成核对；请保留请求编号并查询原结果。"],
  purge_result_unavailable: ["error.purgeResultUnavailable",
    "清除结果暂时无法验证；请保留请求编号，不要重复清除。"],
  project_purge_aborted: ["error.purgeAborted",
    "本次清除未提交；请核对数据和结果后再发起新请求。"],
  purge_restart_required: ["error.purgeRestartRequired",
    "清除需要重启以完成恢复；请先核对是否已提交，不要重新清除。"],
  purge_request_conflict: ["error.purgeRequestConflict",
    "该清除请求编号已绑定其他项目或版本，请核对原请求。"],
  purge_intent_unavailable: ["error.purgeIntentUnavailable",
    "清除请求意图暂时无法验证；请保留原编号，恢复读取后再操作。"],
  purge_intent_conflict: ["error.purgeIntentConflict",
    "还有一项清除请求待核对；请处理原请求后再开始新请求。"],
  purge_intent_unsettled: ["error.purgeIntentUnsettled",
    "请先核对原请求的最终结果，或以原编号确认取消，再移除请求意图。"],
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
  session_operation_failed: ["error.sessionOperationFailed",
    "无法确认会话操作的结果，请刷新核对后再决定是否重试。"],
  session_update_invalid: ["error.sessionUpdateInvalid",
    "会话更改无效，请检查输入后重试。"],
  session_update_failed: ["error.sessionUpdateFailed",
    "无法确认会话更改是否生效，请刷新核对后再试。"],
  session_patch_invalid: ["error.sessionPatchInvalid",
    "会话标题或状态更改无效，请检查后重试。"],
  session_truncate_invalid: ["error.sessionTruncateInvalid",
    "截断位置无效，请重新选择保留到哪条消息。"],
  session_fork_invalid: ["error.sessionForkInvalid",
    "分支参数无效，请检查标题和起始消息。"],
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
  image_name_invalid: ["error.imageNameInvalid",
    "图片文件名无效，请使用不含路径分隔符或控制字符、且不超过 1024 UTF-8 字节的名称。"],
  image_upload_id_invalid: ["error.imageUploadConflict",
    "图片上传记录不一致，请重新选择图片。"],
  image_upload_conflict: ["error.imageUploadConflict",
    "图片上传记录不一致，请重新选择图片。"],
  image_upload_unavailable: ["error.imageUploadUnavailable",
    "多次尝试后仍无法确认图片已保存。请检查连接，然后重新选择图片。"],
  image_upload_unsupported: ["error.imageUploadUnsupported",
    "目标设备不支持可恢复图片上传，请先更新墨斗。"],
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
  if (typeof error?.code === "string" && error.code.startsWith("model_"))
    return modelErrorMessage(error.code.slice(6), error.message || t("error.generic"));
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
