import { loadProjects, readProject, readProjectPurgePreview,
  unregisterProject, updateProject } from "../../state/catalogs.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import { openMemoryPanel, openMemoryDirectory } from "./memory-panel.js";
import { reviewedPurgeIntent, purgeBindingsMatch } from "./project-purge-contract.js";
import { findModel } from "../../utils/models.js";

export function createProjectPanel({ panel, projectsStore, modelsStore,
  projectDialog, navigation, purgeRecovery, purgeConfirmation }) {
  const list = panel.querySelector("#settings-projects-list");
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
  let previewProject = null;
  let previewOrigin = null;
  let previewRequest = null;
  let previewData = null;

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

  function focusProject(id, control = "edit") {
    window.requestAnimationFrame(() => {
      const card = [...list.querySelectorAll("[data-project-id]")]
        .find((item) => item.dataset.projectId === id);
      const targetControl = card?.querySelector(`[data-project-action="${control}"]`) ??
        card?.querySelector("button");
      targetControl?.focus();
      card?.scrollIntoView({ block: "nearest" });
    });
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

  function render() {
    const snapshot = projectsStore.get();
    const activeCard = document.activeElement?.closest?.("[data-project-id]");
    const activeId = activeCard?.dataset.projectId;
    const activeAction = document.activeElement?.dataset.projectAction;
    clear(list);
    if (snapshot.status === "error") {
      list.append(element("p", { className: "resource-error",
        text: errorMessage(snapshot.error) }));
      return;
    }
    const projects = snapshot.data?.items ?? [];
    if (!projects.length) {
      list.append(element("p", { className: "empty-state",
        text: t("project.empty", {}, "还没有项目。可以在这里或侧栏添加工作区。") }));
      return;
    }
    const models = modelsStore.get().data?.models ?? [];
    for (const project of projects) {
      const card = element("article", { className: "resource-card project-settings-card",
        attrs: { "data-project-id": project.id } });
      card.append(element("h3", { text: project.name || project.id }));
      card.append(element("p", { text: project.managed
        ? project.workspace_root : t("project.discovered", {}, "从已有会话或计划中发现") }));
      card.append(element("p", { text: t("project.counts", {
        sessions: project.session_count, schedules: project.schedule_count, id: project.id,
      }, `${project.session_count} 个会话 · ${project.schedule_count} 项计划 · ${project.id}`) }));
      const actions = element("div", { className: "project-settings-actions" });
      const task = element("button", { className: "secondary-button",
        text: t("project.newTask", {}, "新任务"),
        attrs: { type: "button", "data-project-action": "task" } });
      task.addEventListener("click", () => navigation.newTask(project.id));
      actions.append(task);
      const memory = element("button", { className: "secondary-button",
        text: t("project.memory", {}, "项目记忆"),
        attrs: { type: "button", "data-project-action": "memory" } });
      memory.addEventListener("click", () => openMemoryPanel(project));
      actions.append(memory);
      const memoryDirectory = element("button", { className: "secondary-button",
        text: t("project.openMemoryDirectory", {}, "打开记忆目录"), attrs: {
          type: "button", "data-project-action": "memory-directory",
          "aria-label": t("project.openProjectMemoryDirectory",
            { name: project.name || project.id },
            `打开 ${project.name || project.id} 的记忆目录`),
        } });
      memoryDirectory.addEventListener("click", () => { void openMemoryDirectory(project); });
      actions.append(memoryDirectory);
      if (project.managed) {
        const label = element("label", { text: t("project.defaultModel", {}, "默认模型") });
        const select = element("select", { attrs: {
          "aria-label": t("project.projectDefaultModel", { name: project.name },
            `${project.name} 的默认模型`), "data-project-action": "model",
        } });
        select.append(element("option", { text: t("project.followGlobal", {}, "跟随全局默认"),
          attrs: { value: "" } }));
        for (const model of models)
          select.append(element("option", { text: model.name || model.id,
            attrs: { value: model.id } }));
        const selected = findModel(models, project.default_model_id)?.id ?? project.default_model_id;
        if (selected && !models.some((model) => model.id === selected))
          select.append(element("option", { text: t("project.unavailableModel",
            { id: project.default_model_id }, `${project.default_model_id}（已不可用）`),
            attrs: { value: project.default_model_id } }));
        select.value = selected;
        select.addEventListener("change", async () => {
          select.disabled = true;
          try {
            const current = await readProject(project.id);
            await updateProject(project.id, {
              name: current.name, workspace_root: current.workspace_root,
              default_model_id: select.value,
            }, current.etag);
            toast(t("project.modelUpdated", {}, "项目默认模型已更新"));
            focusProject(project.id, "model");
          } catch (cause) {
            toast(errorMessage(cause), "error");
            render();
          }
        });
        label.append(select);
        actions.append(label);
        const edit = element("button", { className: "secondary-button",
          text: t("project.edit", {}, "编辑"),
          attrs: { type: "button", "data-project-action": "edit" } });
        edit.addEventListener("click", () => { void projectDialog.open(project); });
        actions.append(edit);
        const preview = element("button", { className: "secondary-button",
          text: t("project.preview", {}, "核对清除范围"), attrs: {
            type: "button", "data-project-action": "preview",
            "aria-label": t("project.previewProject", { name: project.name || project.id },
              `核对 ${project.name || project.id} 的清除范围`),
          } });
        preview.addEventListener("click", () => openPreview(project, preview));
        actions.append(preview);
        const remove = element("button", { className: "danger-link",
          text: t("project.unregister", {}, "取消注册"),
          attrs: { type: "button", "data-project-action": "unregister" } });
        remove.addEventListener("click", async () => {
          try {
            target = await readProject(project.id);
            description.textContent = t("project.unregisterDescription", {
              name: target.name, sessions: project.session_count,
              schedules: project.schedule_count,
            }, `“${target.name}” 的项目定义将移除，已有的 ${project.session_count} 个会话和 ${project.schedule_count} 项计划会保留。`);
            error.hidden = true;
            dialog.showModal();
          } catch (cause) { toast(errorMessage(cause), "error"); }
        });
        actions.append(remove);
      }
      card.append(actions);
      list.append(card);
    }
    if (activeId && activeAction) focusProject(activeId, activeAction);
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
      panel.querySelector("#projects-add").focus();
    } catch (cause) {
      error.textContent = errorMessage(cause);
      error.hidden = false;
      error.focus();
    } finally { confirm.disabled = false; }
  });
  dialog.addEventListener("close", () => { target = null; });
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
      else if (id) focusProject(id, "preview");
    }
    previewProject = null;
    previewOrigin = null;
    previewData = null;
  });
  panel.querySelector("#projects-add").addEventListener("click", () => {
    void projectDialog.open();
  });
  panel.querySelector("#projects-refresh").addEventListener("click", () => {
    void loadProjects();
  });
  panel.querySelector("#global-memory").addEventListener("click", () => openMemoryPanel());
  panel.querySelector("#global-memory-directory").addEventListener("click", () => {
    void openMemoryDirectory();
  });
  projectsStore.subscribe(render);
  modelsStore.subscribe(render);
  purgeRecovery.subscribe(() => { previewConfirm.disabled = !canConfirmPreview(); });
  subscribeLocale(() => {
    render();
    if (previewDialog.open && previewProject) {
      previewTitle.textContent = t("project.previewTitle",
        { name: previewProject.name || previewProject.id },
        `${previewProject.name || previewProject.id} · 清除范围`);
      if (previewData) showPreview(previewData);
    }
  });

  return Object.freeze({
    focusProject,
    openPurgeReview(intent, origin) {
      openPreview({ id: intent.project_id, name: intent.name }, origin);
    },
  });
}
