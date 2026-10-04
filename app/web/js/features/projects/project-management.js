import { readProject, readProjectPurgePreview,
  unregisterProject } from "../../state/catalogs.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import { reviewedPurgeIntent, purgeBindingsMatch } from "../settings/project-purge-contract.js";

// Project operations belong to the workspace sidebar, independent of Settings.
export function createProjectManagement({ projectsStore, purgeRecovery, purgeConfirmation, focusProject }) {
  const dialog = document.querySelector("#project-unregister-dialog");
  const description = dialog.querySelector("#project-unregister-description");
  const error = dialog.querySelector("#project-unregister-error");
  const confirm = dialog.querySelector('[value="unregister"]');
  const previewDialog = document.querySelector("#project-purge-preview-dialog");
  const previewTitle = previewDialog.querySelector("#project-purge-preview-title");
  const previewStatus = previewDialog.querySelector("#project-purge-preview-status");
  const previewList = previewDialog.querySelector("#project-purge-preview-list");
  const previewNote = previewDialog.querySelector("#project-purge-preview-note");
  const previewRefresh = previewDialog.querySelector("#project-purge-preview-refresh");
  const previewClose = previewDialog.querySelector("#project-purge-preview-close");
  const previewConfirm = previewDialog.querySelector("#project-purge-preview-confirm");
  let target = null;
  let unregisterOrigin = null;
  let unregisterEpoch = 0;
  let previewProject = null;
  let previewOrigin = null;
  let previewRequest = null;
  let previewData = null;

  async function openUnregister(project, origin) {
    const epoch = ++unregisterEpoch;
    const current = await readProject(project.id);
    if (epoch !== unregisterEpoch) return;
    target = current;
    unregisterOrigin = origin ?? document.activeElement;
    const summary = projectsStore.get().data?.items?.find((item) => item.id === project.id) ?? project;
    description.textContent = Number.isInteger(summary.session_count) && Number.isInteger(summary.schedule_count)
      ? t("project.unregisterDescription", { name: current.name,
        sessions: summary.session_count, schedules: summary.schedule_count })
      : t("project.unregisterPreserveDescription", { name: current.name },
        `“${current.name}” 的项目定义将移除，已有会话和计划会保留。`);
    error.hidden = true;
    dialog.showModal();
    dialog.querySelector('[value="cancel"]')?.focus();
  }

  function canConfirmPreview() {
    if (!previewData || purgeRecovery.get().busy || purgeRecovery.get().writeConflict || purgeRecovery.get().error) return false;
    try {
      const intent = purgeRecovery.get().intent;
      const reviewed = reviewedPurgeIntent(previewData, intent?.purge_request_id ?? "0".repeat(32));
      return !intent ? !purgeRecovery.isPaused() : purgeRecovery.get().intentSaved &&
        purgeRecovery.get().result?.outcome === "not_accepted" && purgeBindingsMatch(reviewed, intent);
    } catch { return false; }
  }

  function openPreview(project, origin) {
    previewProject = project; previewOrigin = origin;
    previewTitle.textContent = t("project.previewTitle", { name: project.name || project.id });
    previewConfirm.disabled = true;
    previewDialog.showModal(); previewClose.focus();
    void loadPreview();
  }

  function showPreview(data) {
    previewData = data;
    clear(previewList);
    const presence = (present) => typeof present !== "boolean"
      ? t("project.unknown", {}, "未知") : present
        ? t("project.exists", {}, "存在") : t("project.none", {}, "无");
    const rows = [
      [t("project.previewDefinitionBackup", {}, "项目定义备份"),
        presence(data.project_definition_backup_present)],
      [t("project.previewDraft", {}, "项目新任务草稿"), presence(data.project_draft_present)],
      [t("project.previewDraftBackup", {}, "项目新任务草稿备份"), presence(data.project_draft_backup_present)],
      [t("project.previewGlobalDraft", {}, "当前全局新任务草稿引用"),
        presence(data.global_draft_reference_present)],
      [t("project.previewSelection", {}, "上次会话选择引用"),
        presence(data.selection_reference_present)],
      [t("project.previewSessions", {}, "会话"), data.session_count],
      [t("project.previewSessionDirectory", {}, "会话数据目录"),
        presence(data.session_directory_present)],
      [t("project.previewSessionRuntime", {}, "已打开的会话运行态"), data.session_runtime_count],
      [t("project.previewSchedules", {}, "关联计划"), data.schedule_count],
      [t("project.previewScheduleBackups", {}, "关联计划备份"), data.schedule_backup_count],
      [t("project.previewScheduleHistories", {}, "关联计划历史文件"), data.schedule_history_count],
      [t("project.previewMemoryEntries", {}, "项目记忆条目"), data.project_memory_entry_count],
      [t("project.previewMemoryFile", {}, "项目记忆文件"),
        presence(data.project_memory_present)],
      [t("project.previewMemoryBackup", {}, "项目记忆备份"),
        presence(data.project_memory_backup_present)],
      [t("project.previewMigrationSidecar", {}, "旧版会话提示目录"),
        presence(data.migration_sidecar_present)],
      [t("project.previewInteractiveRuns", {}, "本项目交互运行"), data.active_interactive_run_count],
      [t("project.previewScheduledRuns", {}, "全局计划运行"), data.active_scheduled_run_count_global],
      [t("project.previewSessionDiagnostics", {}, "本项目会话诊断项"), data.session_diagnostic_count],
      [t("project.previewGlobalSessionDiagnostics", {}, "全局会话诊断项"), data.session_catalog_diagnostic_count_global],
      [t("project.previewScheduleDiagnostics", {}, "全局计划诊断项"), data.schedule_catalog_diagnostic_count_global],
      [t("project.previewFiles", {}, "候选文件数"), data.file_count],
      [t("project.previewDirectories", {}, "候选目录数"), data.directory_count],
      [t("project.previewBytes", {}, "候选文件字节数"), data.total_bytes],
    ];
    for (const [label, value] of rows)
      previewList.append(element("dt", { text: label }),
        element("dd", { text: String(value ?? t("project.unknown", {}, "未知")) }));
    if (Array.isArray(data.targets)) {
      const paths = element("ol", { className: "project-preview-paths" });
      // The server bounds this inventory; every candidate must be reviewable.
      for (const target of data.targets)
        paths.append(element("li", {}, [element("code", { text: target.path })]));
      previewList.append(element("dt", { className: "project-preview-target-label",
        text: t("project.previewTargets", { count: data.target_count },
          `候选路径（${data.target_count} 项）`) }),
        element("dd", { className: "project-preview-targets" }, [paths]));
    }
    previewList.hidden = false;
    previewStatus.textContent = t("project.previewRevision", { revision: data.revision },
      `项目版本 ${data.revision} · 当前清单仅供核对`);
    const unsettled = [data.session_runtime_count,
      data.active_interactive_run_count, data.active_scheduled_run_count_global,
      data.session_diagnostic_count, data.session_catalog_diagnostic_count_global,
      data.schedule_catalog_diagnostic_count_global].some((value) =>
      Number(value) > 0);
    previewNote.textContent = unsettled
      ? t("project.previewUnsettled", {}, "存在运行态或诊断项；执行彻底清除前必须重新核对并处理。")
      : t("project.previewClear", {}, "当前未发现运行态或诊断项；执行彻底清除前仍须重新核对。");
    previewNote.hidden = false;
    previewConfirm.disabled = !canConfirmPreview();
    const intent = purgeRecovery.get().intent;
    if (intent && (intent.project_id !== data.id || intent.revision !== data.revision || intent.created_at !== data.created_at))
      previewNote.textContent = t("purgeConfirm.changedBinding");
  }

  async function loadPreview() {
    if (!previewProject) return;
    previewRequest?.abort();
    const request = new AbortController();
    previewRequest = request;
    previewData = null;
    previewConfirm.disabled = true;
    previewStatus.textContent = t("project.previewLoading", {}, "正在读取清单…");
    previewList.hidden = true;
    previewNote.hidden = true;
    try {
      const data = await readProjectPurgePreview(previewProject.id,
        { signal: request.signal });
      if (previewDialog.open && previewRequest === request)
        showPreview(data);
    } catch (cause) {
      if (cause?.name !== "AbortError" && previewDialog.open &&
          previewRequest === request)
        previewStatus.textContent = t("project.previewFailed", { error: errorMessage(cause) },
          `清单读取失败：${errorMessage(cause)}`);
    } finally {
      if (previewRequest === request) previewRequest = null;
    }
  }

  dialog.querySelector("form").addEventListener("submit", async (event) => {
    if (event.submitter?.value !== "unregister") return;
    event.preventDefault();
    if (!target) return;
    confirm.disabled = true;
    error.hidden = true;
    try {
      await unregisterProject(target.id, target.etag);
      dialog.close();
      toast(t("project.unregistered", {}, "项目已取消注册；会话和计划仍保留"));
    } catch (cause) {
      error.textContent = errorMessage(cause);
      error.hidden = false;
      error.focus();
    } finally { confirm.disabled = false; }
  });
  dialog.addEventListener("close", () => {
    if (dialog.open) return;
    ++unregisterEpoch;
    const projectId = target?.id;
    target = null;
    if (unregisterOrigin?.isConnected && unregisterOrigin.getClientRects().length)
      unregisterOrigin.focus({ preventScroll: true });
    else focusProject(projectId);
    unregisterOrigin = null;
  });
  previewRefresh.addEventListener("click", () => { void loadPreview(); });
  previewClose.addEventListener("click", () => previewDialog.close());
  previewConfirm.addEventListener("click", () => {
    if (!canConfirmPreview()) return;
    const data = previewData, origin = previewOrigin;
    previewDialog.close();
    purgeConfirmation.open(data, origin);
  });
  previewDialog.addEventListener("close", () => {
    previewRequest?.abort();
    previewRequest = null;
    const id = previewProject?.id;
    if (!document.querySelector("#project-purge-confirm-dialog").open) {
      if (previewOrigin?.isConnected) previewOrigin.focus();
      else if (id) focusProject(id);
    }
    previewProject = null;
    previewOrigin = null;
    previewData = null;
  });
  purgeRecovery.subscribe(() => { previewConfirm.disabled = !canConfirmPreview(); });
  subscribeLocale(() => {
    if (previewDialog.open && previewProject) {
      previewTitle.textContent = t("project.previewTitle",
        { name: previewProject.name || previewProject.id },
        `${previewProject.name || previewProject.id} · 清除范围`);
      if (previewData) showPreview(previewData);
    }
  });

  return Object.freeze({
    openUnregister,
    openPreview,
    openPurgeReview(intent, origin) {
      openPreview({ id: intent.project_id, name: intent.name }, origin);
    },
  });
}
