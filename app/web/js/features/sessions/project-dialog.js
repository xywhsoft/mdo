import { createProject } from "../../state/catalogs.js";
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
  onCreated }) {
  const workspace = form.elements.workspace_root;
  const name = form.elements.name;
  const id = form.elements.id;
  const model = form.elements.default_model_id;
  let nameEdited = false;
  let idEdited = false;

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
    if (!values.default_model_id) delete values.default_model_id;
    try {
      const created = await createProject(values);
      dialog.close();
      onCreated(created);
      toast(`已添加项目 ${created.name}`);
    } catch (cause) {
      error.textContent = errorMessage(cause);
      error.hidden = false;
    } finally {
      submit.disabled = false;
    }
  });
  dialog.addEventListener("close", () => { error.hidden = true; });

  return Object.freeze({
    open() {
      form.reset();
      nameEdited = false;
      idEdited = false;
      error.hidden = true;
      fillModels();
      if (!dialog.open) dialog.showModal();
      window.setTimeout(() => workspace.focus(), 0);
    },
  });
}
