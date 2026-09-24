import { loadProjects, readProject, unregisterProject, updateProject } from "../../state/catalogs.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { openMemoryPanel } from "./memory-panel.js";

export function createProjectPanel({ panel, projectsStore, modelsStore,
  projectDialog, navigation }) {
  const list = panel.querySelector("#settings-projects-list");
  const dialog = document.querySelector("#project-unregister-dialog");
  const description = dialog.querySelector("#project-unregister-description");
  const error = dialog.querySelector("#project-unregister-error");
  const confirm = dialog.querySelector('[value="unregister"]');
  let target = null;

  function focusProject(id, control = "edit") {
    window.requestAnimationFrame(() => {
      const card = [...list.querySelectorAll("[data-project-id]")]
        .find((item) => item.dataset.projectId === id);
      const targetControl = control === "model" ? card?.querySelector("select")
        : [...(card?.querySelectorAll("button") ?? [])]
          .find((button) => button.textContent === "编辑") ??
          card?.querySelector("button");
      targetControl?.focus();
      card?.scrollIntoView({ block: "nearest" });
    });
  }

  function render() {
    const snapshot = projectsStore.get();
    clear(list);
    if (snapshot.status === "error") {
      list.append(element("p", { className: "resource-error",
        text: errorMessage(snapshot.error) }));
      return;
    }
    const projects = snapshot.data?.items ?? [];
    if (!projects.length) {
      list.append(element("p", { className: "empty-state",
        text: "还没有项目。可以在这里或侧栏添加工作区。" }));
      return;
    }
    const models = modelsStore.get().data?.models ?? [];
    for (const project of projects) {
      const card = element("article", { className: "resource-card project-settings-card",
        attrs: { "data-project-id": project.id } });
      card.append(element("h3", { text: project.name || project.id }));
      card.append(element("p", { text: project.managed
        ? project.workspace_root : "从已有会话或计划中发现" }));
      card.append(element("p", { text: `${project.session_count} 个会话 · ${project.schedule_count} 项计划 · ${project.id}` }));
      const actions = element("div", { className: "project-settings-actions" });
      const task = element("button", { className: "secondary-button", text: "新任务",
        attrs: { type: "button" } });
      task.addEventListener("click", () => navigation.newTask(project.id));
      actions.append(task);
      const memory = element("button", { className: "secondary-button", text: "项目记忆",
        attrs: { type: "button" } });
      memory.addEventListener("click", () => openMemoryPanel(project));
      actions.append(memory);
      if (project.managed) {
        const label = element("label", { text: "默认模型" });
        const select = element("select", { attrs: { "aria-label": `${project.name} 的默认模型` } });
        select.append(element("option", { text: "跟随全局默认",
          attrs: { value: "" } }));
        for (const model of models)
          select.append(element("option", { text: model.name || model.id,
            attrs: { value: model.id } }));
        if (project.default_model_id && !models.some((model) =>
          model.id === project.default_model_id))
          select.append(element("option", { text: `${project.default_model_id}（已不可用）`,
            attrs: { value: project.default_model_id } }));
        select.value = project.default_model_id;
        select.addEventListener("change", async () => {
          select.disabled = true;
          try {
            const current = await readProject(project.id);
            await updateProject(project.id, {
              name: current.name, workspace_root: current.workspace_root,
              default_model_id: select.value,
            }, current.etag);
            toast("项目默认模型已更新");
            focusProject(project.id, "model");
          } catch (cause) {
            toast(errorMessage(cause), "error");
            render();
          }
        });
        label.append(select);
        actions.append(label);
        const edit = element("button", { className: "secondary-button", text: "编辑",
          attrs: { type: "button" } });
        edit.addEventListener("click", () => { void projectDialog.open(project); });
        actions.append(edit);
        const remove = element("button", { className: "danger-link", text: "取消注册",
          attrs: { type: "button" } });
        remove.addEventListener("click", async () => {
          try {
            target = await readProject(project.id);
            description.textContent = `“${target.name}” 的项目定义将移除，已有的 ${project.session_count} 个会话和 ${project.schedule_count} 项计划会保留。`;
            error.hidden = true;
            dialog.showModal();
          } catch (cause) { toast(errorMessage(cause), "error"); }
        });
        actions.append(remove);
      }
      card.append(actions);
      list.append(card);
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
      toast("项目已取消注册；会话和计划仍保留");
      panel.querySelector("#projects-add").focus();
    } catch (cause) {
      error.textContent = errorMessage(cause);
      error.hidden = false;
    } finally { confirm.disabled = false; }
  });
  dialog.addEventListener("close", () => { target = null; });
  panel.querySelector("#projects-add").addEventListener("click", () => {
    void projectDialog.open();
  });
  panel.querySelector("#projects-refresh").addEventListener("click", () => {
    void loadProjects();
  });
  panel.querySelector("#global-memory").addEventListener("click", () => openMemoryPanel());
  projectsStore.subscribe(render);
  modelsStore.subscribe(render);

  return Object.freeze({
    focusProject,
  });
}
