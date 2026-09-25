import { createProject, readProject, updateProject } from "../../state/catalogs.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";

function pathName(path) {
  const segments = path.trim().replace(/[\\/]+$/, "").split(/[\\/]/);
  return segments.at(-1) || "";
}

function projectId(name) {
  const slug = name.toLowerCase().replace(/[^a-z0-9._-]+/g, "-")
    .replace(/^[._-]+|[._-]+$/g, "").slice(0, 64);
  return slug || `project-${Date.now().toString(36)}`;
}

export function createProjectDialog({ dialog, form, error, submit, modelsStore,
  onCreated, onUpdated }) {
  const workspace = form.elements.workspace_root;
  const name = form.elements.name;
  const id = form.elements.id;
  const model = form.elements.default_model_id;
  let nameEdited = false;
  let idEdited = false;
  let editing = null;
  const title = dialog.querySelector("#project-dialog-title");
  const description = dialog.querySelector("#project-dialog-description");

  workspace.addEventListener("input", () => {
    if (!nameEdited) name.value = pathName(workspace.value);
    if (!idEdited) id.value = projectId(name.value);
  });
  name.addEventListener("input", () => {
    nameEdited = true;
    if (!idEdited) id.value = projectId(name.value);
  });
  id.addEventListener("input", () => { idEdited = true; });

  function fillModels(selected = model.value) {
    const models = modelsStore.get().data?.models ?? [];
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
  }
  modelsStore.subscribe(() => fillModels());
  subscribeLocale(() => { fillModels(); renderText(); });
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!form.reportValidity()) return;
    submit.disabled = true;
    error.hidden = true;
    const values = Object.fromEntries(new FormData(form));
    if (!editing && !values.default_model_id) delete values.default_model_id;
    try {
      const result = editing
        ? await updateProject(editing.id, {
          name: values.name,
          workspace_root: values.workspace_root,
          default_model_id: values.default_model_id,
        }, editing.etag)
        : await createProject(values);
      dialog.close();
      if (!editing) onCreated(result);
      else onUpdated?.(result);
      toast(editing ? t("project.updated", { name: result.name },
        `已更新项目 ${result.name}`) : t("project.added", { name: result.name },
        `已添加项目 ${result.name}`));
    } catch (cause) {
      error.textContent = errorMessage(cause);
      error.hidden = false;
    } finally {
      submit.disabled = false;
    }
  });
  dialog.addEventListener("close", () => { error.hidden = true; });

  return Object.freeze({
    async open(project = null) {
      form.reset();
      editing = null;
      error.hidden = true;
      fillModels();
      if (project) {
        try { editing = await readProject(project.id); }
        catch (cause) { toast(errorMessage(cause), "error"); return; }
        workspace.value = editing.workspace_root;
        name.value = editing.name;
        id.value = editing.id;
        fillModels(editing.default_model_id);
      }
      nameEdited = Boolean(editing);
      idEdited = Boolean(editing);
      id.readOnly = Boolean(editing);
      renderText();
      if (!dialog.open) dialog.showModal();
      window.setTimeout(() => (editing ? name : workspace).focus(), 0);
    },
  });
}
