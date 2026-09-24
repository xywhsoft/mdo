import { createProject, readProject, updateProject } from "../../state/catalogs.js";
import { element, errorMessage, toast } from "../../utils/dom.js";

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

  function fillModels() {
    const selected = model.value;
    model.replaceChildren(element("option", { text: "跟随全局默认",
      attrs: { value: "" } }), ...(
      modelsStore.get().data?.models ?? []).map((item) => element("option", {
        text: item.name || item.id, attrs: { value: item.id },
      })));
    model.value = selected;
  }
  modelsStore.subscribe(fillModels);
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
      toast(editing ? `已更新项目 ${result.name}` : `已添加项目 ${result.name}`);
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
        model.value = editing.default_model_id;
      }
      nameEdited = Boolean(editing);
      idEdited = Boolean(editing);
      id.readOnly = Boolean(editing);
      title.textContent = editing ? "编辑项目" : "添加项目";
      description.textContent = editing
        ? "修改只影响此后创建的任务；已有会话仍使用创建时的配置。"
        : "指定工作区目录；项目会保留在侧栏，新任务将使用该目录。";
      submit.textContent = editing ? "保存项目" : "添加项目";
      if (!dialog.open) dialog.showModal();
      window.setTimeout(() => (editing ? name : workspace).focus(), 0);
    },
  });
}
