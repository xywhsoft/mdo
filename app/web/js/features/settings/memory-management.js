import { clear, element, errorMessage } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import { createMemoryBrowser } from "./memory-browser.js";

export function createMemoryManagement({ panel, projectsStore, openEditor, openDirectory,
  browser = createMemoryBrowser() }) {
  const scope = panel.querySelector("#memory-scope");
  const files = panel.querySelector("#memory-page-files");
  const status = panel.querySelector("#memory-page-status");
  const title = panel.querySelector("#memory-file-title");
  const path = panel.querySelector("#memory-file-path");
  const content = panel.querySelector("#memory-file-content");
  const refresh = panel.querySelector("#memory-page-refresh");
  const create = panel.querySelector("#memory-page-new");
  const edit = panel.querySelector("#memory-page-edit");
  const directory = panel.querySelector("#memory-page-directory");
  let active = false;
  let project = null;

  function renderScopes() {
    const projects = [...(projectsStore.get().data?.items ?? [])];
    // A project definition can be removed while its memory file stays open.
    if (project && !projects.some((item) => item.id === project.id)) projects.push(project);
    clear(scope);
    scope.append(element("option", { text: t("memory.globalTitle"), attrs: { value: "" } }));
    for (const item of projects) scope.append(element("option", {
      text: t("memory.projectTitle", { name: item.name || item.id }), attrs: { value: item.id },
    }));
    scope.value = project?.id ?? "";
  }

  function render(state) {
    const focusedId = document.activeElement?.dataset?.memoryFile;
    panel.setAttribute("aria-busy", String(state.busy));
    refresh.disabled = create.disabled = state.busy;
    edit.disabled = state.busy || !state.entry;
    status.dataset.tone = state.error ? "error" : "neutral";
    status.textContent = state.error ? errorMessage(state.error) : state.busy
      ? t("memory.loading") : t("memory.fileCount", { count: state.collection?.items?.length ?? 0 });
    clear(files);
    const items = state.collection?.items ?? [];
    if (!items.length) files.append(element("p", { className: "empty-state",
      text: state.busy ? t("memory.loading") : t("memory.empty") }));
    for (const item of items) {
      const button = element("button", { className: "memory-list-item", attrs: {
        type: "button", "data-memory-file": item.id,
        "aria-current": item.id === state.selectedId ? "true" : "false",
      } }, [element("strong", { text: `${item.id}.md` })]);
      if (item.title && item.title !== item.id && item.title !== `${item.id}.md`)
        button.append(element("small", { text: item.title }));
      button.addEventListener("click", () => { void browser.select(item.id); });
      files.append(button);
    }
    if (focusedId) [...files.querySelectorAll("[data-memory-file]")]
      .find((node) => node.dataset.memoryFile === focusedId)?.focus({ preventScroll: true });
    const directoryPath = state.project ? `mdo-home/memory/projects/${state.project.id}/` : "mdo-home/memory/global/";
    title.textContent = state.selectedId ? `${state.selectedId}.md` : t("memory.selectFile");
    path.textContent = directoryPath + (state.selectedId ? `${state.selectedId}.md` : "");
    // Show the file verbatim. Markdown and HTML in memory cannot run scripts
    // or load external images when the user is only inspecting local data.
    content.textContent = state.entry?.content ?? (state.busy ? t("memory.loading") : t("memory.selectFileHint"));
  }

  function selectScope(id) {
    project = id ? projectsStore.get().data?.items?.find((item) => item.id === id) ??
      { id, name: id } : null;
    renderScopes();
    if (active) void browser.refresh(project, "");
  }
  const onScope = () => selectScope(scope.value);
  const onRefresh = () => { void browser.refresh(project); };
  function showEditor(selectId = "") {
    const origin = document.activeElement;
    const editedProject = project;
    openEditor(project, { selectId, onClose: async ({ selectedId }) => {
      if (!active || project?.id !== editedProject?.id) return;
      await browser.refresh(project, selectedId);
      // Refresh temporarily disables toolbar controls. Restore focus after
      // loading, unless the user has already moved to another control.
      if (active && origin?.isConnected && !origin.disabled &&
          (document.activeElement === document.body || document.activeElement === document.documentElement))
        origin.focus({ preventScroll: true });
    } });
  }
  const onCreate = () => showEditor();
  const onEdit = () => { if (browser.get().entry) showEditor(browser.get().selectedId); };
  const onDirectory = () => { void openDirectory(project); };
  scope.addEventListener("change", onScope);
  refresh.addEventListener("click", onRefresh);
  create.addEventListener("click", onCreate);
  edit.addEventListener("click", onEdit);
  directory.addEventListener("click", onDirectory);
  const unsubscribeProjects = projectsStore.subscribe(renderScopes);
  const unsubscribe = browser.subscribe(render);
  const unsubscribeLocale = subscribeLocale(() => { renderScopes(); render(browser.get()); });
  renderScopes();

  return Object.freeze({
    selectScope,
    setActive(value) {
      if (active === value) return;
      active = value;
      if (active) void browser.refresh(project);
      else browser.cancel();
    },
    destroy() {
      browser.cancel(); unsubscribe(); unsubscribeProjects(); unsubscribeLocale();
      scope.removeEventListener("change", onScope);
      refresh.removeEventListener("click", onRefresh); create.removeEventListener("click", onCreate);
      edit.removeEventListener("click", onEdit); directory.removeEventListener("click", onDirectory);
    },
  });
}
