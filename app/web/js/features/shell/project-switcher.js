import { clear, element, isImeKey, revealListOption } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";

// The header project crumb changes the destination for a new task. Existing
// sessions stay in their own projects and keep their drafts when we leave.
export function createProjectSwitcher({ control, button, name, separator, menu,
  navigation, projectsStore, onSelectProject, onManageProjects,
  includeNewTask = false }) {
  function projectId() {
    const route = navigation.get();
    return route.view === "workspace" && (includeNewTask || route.sessionId)
      ? route.projectId || navigation.preferredProject() : "";
  }

  function projects() {
    const items = projectsStore.get().data?.items ?? [];
    const current = projectId();
    return current && !items.some((item) => item.id === current)
      ? [{ id: current, name: current }, ...items] : items;
  }

  function projectLabel(project) {
    return project.id === "default" ? t("nav.defaultProject", {}, "默认项目") :
      project.name || project.id;
  }

  function entries() {
    return [...menu.querySelectorAll('[role="menuitem"]')];
  }

  function close(restoreFocus = false) {
    if (menu.hidden) return;
    menu.hidden = true;
    button.setAttribute("aria-expanded", "false");
    if (restoreFocus && !control.hidden) button.focus({ preventScroll: true });
  }

  function focusItem(item) {
    if (!item) return;
    item.focus({ preventScroll: true });
    revealListOption(menu, item);
  }

  function render() {
    const current = projectId();
    control.hidden = !current;
    if (separator) separator.hidden = !current;
    if (!current) { close(); clear(menu); return; }
    const list = projects();
    const selected = list.find((item) => item.id === current);
    const label = projectLabel(selected ?? { id: current, name: current });
    name.textContent = label;
    button.setAttribute("aria-label", t("nav.switchProject", { name: label },
      `切换项目，当前 ${label}`));
    const focusedId = menu.contains(document.activeElement)
      ? document.activeElement.dataset.projectId : "";
    clear(menu);
    for (const project of list) {
      const item = element("button", { attrs: {
        type: "button", role: "menuitem", "data-project-id": project.id,
        "aria-current": String(project.id === current),
      } }, [element("span", { text: projectLabel(project) }),
        ...(project.id === current ? [element("span", { text: "✓",
          attrs: { "aria-hidden": "true" } })] : [])]);
      item.addEventListener("click", () => {
        close(project.id === projectId());
        if (project.id !== projectId()) onSelectProject(project.id);
      });
      menu.append(item);
    }
    const manage = element("button", { className: "project-menu-manage",
      text: t("nav.manageProjects", {}, "管理项目"), attrs: {
        type: "button", role: "menuitem", "data-project-id": "manage",
      } });
    manage.addEventListener("click", () => { close(); onManageProjects(); });
    menu.append(manage);
    if (focusedId) {
      const replacement = entries().find((item) => item.dataset.projectId === focusedId);
      focusItem(replacement ?? button);
    }
  }

  function open(index = -1) {
    if (!projectId()) return;
    render();
    menu.hidden = false;
    button.setAttribute("aria-expanded", "true");
    if (index === "last") focusItem(entries().at(-1));
    else if (index >= 0) focusItem(entries()[index]);
  }

  function onButtonKeyDown(event) {
    if (isImeKey(event) || !["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key) ||
        (menu.hidden && (event.key === "Home" || event.key === "End"))) return;
    event.preventDefault();
    open(event.key === "ArrowDown" || event.key === "Home" ? 0 : "last");
  }

  function onMenuKeyDown(event) {
    if (isImeKey(event)) return;
    const options = entries();
    const index = options.indexOf(document.activeElement);
    let next = index;
    if (event.key === "ArrowDown") next = (index + 1) % options.length;
    else if (event.key === "ArrowUp") next = (index - 1 + options.length) % options.length;
    else if (event.key === "Home") next = 0;
    else if (event.key === "End") next = options.length - 1;
    else return;
    event.preventDefault();
    focusItem(options[next]);
  }

  function onDocumentKeyDown(event) {
    if (menu.hidden || event.key !== "Escape" || isImeKey(event)) return;
    event.preventDefault();
    event.stopImmediatePropagation();
    close(true);
  }

  function onDocumentPointerDown(event) {
    if (!menu.hidden && !control.contains(event.target)) close();
  }

  function onDocumentFocusIn(event) {
    if (!menu.hidden && !control.contains(event.target)) close();
  }

  button.addEventListener("click", () => menu.hidden ? open() : close());
  button.addEventListener("keydown", onButtonKeyDown);
  menu.addEventListener("keydown", onMenuKeyDown);
  document.addEventListener("keydown", onDocumentKeyDown, true);
  document.addEventListener("pointerdown", onDocumentPointerDown);
  document.addEventListener("focusin", onDocumentFocusIn);
  const unsubscribeNavigation = navigation.subscribe(render);
  const unsubscribeProjects = projectsStore.subscribe(render);
  const unsubscribeLocale = subscribeLocale(render);
  return Object.freeze({ close, destroy() {
    unsubscribeNavigation();
    unsubscribeProjects();
    unsubscribeLocale();
    document.removeEventListener("keydown", onDocumentKeyDown, true);
    document.removeEventListener("pointerdown", onDocumentPointerDown);
    document.removeEventListener("focusin", onDocumentFocusIn);
  } });
}
