import { createProject, readProject, updateProject } from "../../state/catalogs.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import { projectDefaultsFromWorkspace, projectIdFromName } from "./project-identity.js";
import { findModel } from "../../utils/models.js";

export function createProjectDialog({ dialog, form, error, submit, modelsStore,
  onCreated, onUpdated, directoryPicker, browse, status,
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
  const title = dialog.querySelector("#project-dialog-title");
  const description = dialog.querySelector("#project-dialog-description");

  function suggestIdentity() {
    const defaults = projectDefaultsFromWorkspace(workspace.value);
    if (!nameEdited) name.value = defaults.name;
    if (!idEdited) id.value = nameEdited ? projectIdFromName(name.value) : defaults.id;
  }
  workspace.addEventListener("input", suggestIdentity);
  browse?.addEventListener("click", () => {
    const owner = generation;
    void directoryPicker.open(workspace.value, (path) => {
      if (owner !== generation || !dialog.open) return;
      workspace.value = path; suggestIdentity(); workspace.focus();
    });
  });
  name.addEventListener("input", () => {
    nameEdited = true;
    if (!idEdited) id.value = projectIdFromName(name.value);
  });
  id.addEventListener("input", () => { idEdited = true; });

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
      t("project.add", {}, "添加项目");
    description.textContent = editing
      ? t("project.editDescription", {}, "修改只影响此后创建的任务；已有会话仍使用创建时的配置。")
      : t("project.addDescription", {}, "指定工作区目录；项目会保留在侧栏，新任务将使用该目录。");
    submit.textContent = editing ? t("project.save", {}, "保存项目") :
      t("project.add", {}, "添加项目");
    renderState();
  }
  function renderState() {
    const localSave = saving?.generation === generation;
    submit.disabled = loading || loadFailed || Boolean(saving);
    for (const field of [workspace, name, id, model, browse].filter(Boolean))
      field.disabled = loading || localSave;
    if (status) {
      status.hidden = !loading && !localSave;
      status.textContent = loading ? t("project.loading", {}, "正在读取项目…") :
        localSave ? t("project.saving", {}, "正在保存项目…") : "";
    }
  }
  modelsStore.subscribe(() => fillModels());
  subscribeLocale(() => { fillModels(); renderText(); });
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (loading || loadFailed || saving || !dialog.open || !form.reportValidity()) return;
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
    directoryPicker?.close(); renderState();
  });

  return Object.freeze({
    async open(project = null) {
      const version = ++generation;
      directoryPicker?.close();
      form.reset();
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
      (editing ? name : workspace).focus();
    },
  });
}
