import { createProject, readProject, updateProject } from "../../state/catalogs.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import { projectDefaultsFromWorkspace, projectIdFromName } from "./project-identity.js";
import { findModel } from "../../utils/models.js";
import { droppedFolderPath } from "./folder-drop.js";
import { api } from "../../api/client.js";

export function createProjectDialog({ dialog, form, error, submit, modelsStore,
  onCreated, onUpdated, directoryPicker, browse, status, folderUI = {},
  resolveDrop = droppedFolderPath,
  readDirectory = (path, signal) => api.get(`/workspace/directories?path=${encodeURIComponent(path)}`, { signal }).then((reply) => reply.data),
  read = readProject, create = createProject, update = updateProject,
  notify = toast }) {
  const workspace = form.elements.workspace_root;
  const name = form.elements.name;
  const id = form.elements.id;
  const model = form.elements.default_model_id;
  let nameEdited = false;
  let idEdited = false;
  let editing = null;
  let generation = 0;
  let loading = false;
  let loadFailed = false;
  let saving = null;
  let returnFocus = null;
  let returnFocusKey = "";
  let dropController = null;
  let selecting = false;
  const title = dialog.querySelector("#project-dialog-title");
  const description = dialog.querySelector("#project-dialog-description");

  function suggestIdentity() {
    const defaults = projectDefaultsFromWorkspace(workspace.value);
    if (!nameEdited) name.value = defaults.name;
    if (!idEdited) id.value = nameEdited ? projectIdFromName(name.value) : defaults.id;
    renderFolder(); renderState();
  }
  workspace.addEventListener("input", suggestIdentity);
  browse?.addEventListener("click", () => {
    const owner = generation;
    void directoryPicker.open(workspace.value, (path) => {
      if (owner !== generation || !dialog.open) return;
      workspace.value = path; suggestIdentity(); name.focus();
    });
  });
  name.addEventListener("input", () => {
    nameEdited = true;
    if (!idEdited) id.value = projectIdFromName(name.value);
  });
  id.addEventListener("input", () => { idEdited = true; });

  function renderFolder() {
    const path = workspace.value.trim();
    if (folderUI.summary) folderUI.summary.hidden = !path;
    if (folderUI.empty) folderUI.empty.hidden = Boolean(path);
    if (folderUI.name) folderUI.name.textContent = projectDefaultsFromWorkspace(path).name || path;
    if (folderUI.path) folderUI.path.textContent = path;
    if (folderUI.browseLabel) {
      folderUI.browseLabel.removeAttribute("data-i18n");
      folderUI.browseLabel.textContent = path ? t("project.changeFolder", {}, "更换文件夹")
        : t("project.addFolder", {}, "添加文件夹");
    }
  }
  folderUI.clear?.addEventListener("click", () => {
    workspace.value = ""; suggestIdentity(); browse?.focus();
  });
  for (const type of ["dragenter", "dragover"]) folderUI.zone?.addEventListener(type, (event) => {
    event.preventDefault(); event.stopPropagation();
    if (loading || saving || selecting) return;
    if (event.dataTransfer) event.dataTransfer.dropEffect = "copy";
    folderUI.zone.classList.add("is-dragging");
  });
  folderUI.zone?.addEventListener("dragleave", (event) => {
    if (!folderUI.zone.contains(event.relatedTarget)) folderUI.zone.classList.remove("is-dragging");
  });
  folderUI.zone?.addEventListener("drop", async (event) => {
    event.preventDefault(); event.stopPropagation();
    folderUI.zone.classList.remove("is-dragging");
    if (loading || saving || selecting || !dialog.open) return;
    const owner = generation;
    dropController?.abort();
    const controller = dropController = new AbortController();
    selecting = true; error.hidden = true; renderState();
    try {
      const path = await resolveDrop(event.dataTransfer, { signal: controller.signal });
      const directory = await readDirectory(path, controller.signal);
      if (owner !== generation || !dialog.open || controller.signal.aborted) return;
      workspace.value = directory.path; suggestIdentity(); name.focus();
    } catch (cause) {
      if (owner !== generation || !dialog.open || controller.signal.aborted) return;
      const key = { folder_drop_one: "project.dropOneFolder", folder_drop_directory: "project.dropDirectoryOnly",
        folder_drop_unavailable: "project.dropUnavailable" }[cause.code];
      error.textContent = key ? t(key) : errorMessage(cause); error.hidden = false;
    } finally {
      if (owner === generation) { selecting = false; renderState(); }
    }
  });

  function fillModels(selected = model.value) {
    const models = modelsStore.get().data?.models ?? [];
    selected = findModel(models, selected)?.id ?? selected;
    model.replaceChildren(element("option", { text: t("project.followGlobal", {}, "跟随全局默认"),
      attrs: { value: "" } }), ...
      models.map((item) => element("option", {
        text: item.name || item.id, attrs: { value: item.id },
      })));
    if (selected && !models.some((item) => item.id === selected))
      model.append(element("option", { text: t("project.unavailableModel", { id: selected },
        `${selected}（已不可用）`), attrs: { value: selected } }));
    model.value = selected;
  }
  function renderText() {
    title.textContent = editing ? t("project.editTitle", {}, "编辑项目") :
      t("project.create", {}, "创建项目");
    description.textContent = editing
      ? t("project.editDescription", {}, "修改只影响此后创建的任务；已有会话仍使用创建时的配置。")
      : t("project.addDescription", {}, "指定工作区目录；项目会保留在侧栏，新任务将使用该目录。");
    submit.textContent = editing ? t("project.save", {}, "保存项目") :
      t("project.create", {}, "创建项目");
    renderFolder();
    renderState();
  }
  function renderState() {
    const localSave = saving?.generation === generation;
    submit.disabled = loading || loadFailed || Boolean(saving) || selecting || !workspace.value.trim();
    for (const field of [workspace, name, id, model, browse, folderUI.clear].filter(Boolean))
      field.disabled = loading || localSave || selecting;
    if (status) {
      status.hidden = !loading && !localSave && !selecting;
      status.textContent = loading ? t("project.loading", {}, "正在读取项目…") :
        localSave ? t("project.saving", {}, "正在保存项目…") : selecting ? t("project.checkingFolder", {}, "正在确认文件夹…") : "";
    }
  }
  modelsStore.subscribe(() => fillModels());
  subscribeLocale(() => { fillModels(); renderText(); });
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (loading || loadFailed || saving || selecting || !dialog.open || !form.reportValidity()) return;
    error.hidden = true;
    const values = Object.fromEntries(new FormData(form));
    // A save owns its submitted definition, even if the user closes the dialog
    // and opens another project while the server is still replying.
    const owner = { generation, editing: editing && { ...editing } };
    saving = owner;
    if (!owner.editing && !values.default_model_id) delete values.default_model_id;
    renderState();
    try {
      const result = owner.editing
        ? await update(owner.editing.id, {
          name: values.name,
          workspace_root: values.workspace_root,
          default_model_id: values.default_model_id,
        }, owner.editing.etag)
        : await create(values);
      // Catalog writers already refresh the sidebar. A completed background
      // save must not close a newer form or navigate away from the user's task.
      if (owner.generation === generation && dialog.open) {
        // Creating enters a new task; its composer owns focus. Cancellation
        // still restores the sidebar button, as does editing an existing item.
        if (!owner.editing) { returnFocus = null; returnFocusKey = ""; }
        dialog.close();
        if (!owner.editing) onCreated(result);
        else onUpdated?.(result);
      }
      notify(owner.editing ? t("project.updated", { name: result.name },
        `已更新项目 ${result.name}`) : t("project.added", { name: result.name },
        `已添加项目 ${result.name}`));
    } catch (cause) {
      if (owner.generation === generation && dialog.open) {
        error.textContent = errorMessage(cause);
        error.hidden = false;
        error.focus();
      } else notify(errorMessage(cause), "error");
    } finally {
      saving = null;
      renderState();
    }
  });
  dialog.addEventListener("close", () => {
    if (dialog.open) return;
    ++generation; loading = false; error.hidden = true;
    dropController?.abort(); selecting = false;
    folderUI.zone?.classList.remove("is-dragging");
    directoryPicker?.close(); renderState();
    // Saving can refresh the catalog and replace the original sidebar button.
    // Restore its current counterpart rather than a detached DOM node.
    const origin = returnFocus?.isConnected ? returnFocus : returnFocusKey
      ? [...document.querySelectorAll("[data-sidebar-focus]")]
        .find((node) => node.dataset.sidebarFocus === returnFocusKey) : null;
    returnFocus = null; returnFocusKey = "";
    if (origin?.getClientRects().length) origin.focus({ preventScroll: true });
  });

  return Object.freeze({
    async open(project = null, origin = document.activeElement) {
      const version = ++generation;
      returnFocus = origin;
      returnFocusKey = returnFocus?.dataset?.sidebarFocus || "";
      directoryPicker?.close();
      dropController?.abort(); selecting = false;
      folderUI.zone?.classList.remove("is-dragging");
      form.reset();
      if (folderUI.options) folderUI.options.open = Boolean(project);
      dialog.dataset && (dialog.dataset.mode = project ? "edit" : "create");
      editing = project && { ...project };
      loading = Boolean(project);
      loadFailed = false;
      error.hidden = true;
      nameEdited = Boolean(project);
      idEdited = Boolean(project);
      id.readOnly = Boolean(project);
      fillModels();
      renderText();
      if (!dialog.open) dialog.showModal();
      if (project) {
        try {
          const result = await read(project.id);
          if (version !== generation || !dialog.open) return;
          editing = result;
        } catch (cause) {
          if (version !== generation || !dialog.open) return;
          loading = false; loadFailed = true;
          error.textContent = errorMessage(cause); error.hidden = false;
          // An unread definition cannot be submitted without its ETag.
          renderState(); submit.disabled = true;
          error.focus(); return;
        }
        workspace.value = editing.workspace_root;
        name.value = editing.name;
        id.value = editing.id;
        fillModels(editing.default_model_id);
      }
      loading = false;
      renderText();
      name.focus();
    },
  });
}
