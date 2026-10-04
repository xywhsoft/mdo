import { element, clear, formatRelativeTime, errorMessage, isImeKey, toast } from "../../utils/dom.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";
import { sessionActionItems } from "./session-actions.js";
import { mountIcons } from "../../components/icons.js";
import { recentSessions, sidebarGroups, sidebarWindow } from "./sidebar-tree.js";

export function sessionRunActivities(runs = []) {
  const activities = new Map();
  const rank = { created: 1, stopping: 2, running: 3 };
  for (const run of runs) {
    if (run?.terminal !== false || !["created", "running"].includes(run.state) ||
        typeof run.project_id !== "string" || !run.project_id ||
        typeof run.session_id !== "string" || !run.session_id) continue;
    const key = `${run.project_id}/${run.session_id}`;
    const activity = run.cancel_requested ? "stopping" : run.state;
    // A finished older run must not hide a new one. If a snapshot contains
    // several active runs, show ongoing work before a stopping/preparing run.
    if (rank[activity] > (rank[activities.get(key)] ?? 0)) activities.set(key, activity);
  }
  return activities;
}

function activityText(activity) {
  if (activity === "created") return t("run.created", {}, "准备中");
  if (activity === "running") return t("run.running", {}, "运行中");
  if (activity === "stopping") return t("task.stopping", {}, "正在停止…");
  return "";
}

export function createSessionList({ container, count, filter, searchInput, store, projectsStore, runsStore,
  navigation, onSelect, onAction, onNewInProject, onAddProject, onBrowseProject,
  onManageProject, onManageProjects, onProjectAction }) {
  let query = "";
  let status = filter.value;
  let openMenu = "";
  let focusRequest = null;
  let state = store.get();
  let renderedStoreKey = null;
  let unread = new Set();
  let activities = sessionRunActivities(runsStore?.get().data?.items);
  let openMenuNode = null;
  let openMenuButton = null;
  let quickProjectOpen = false;
  let quickProjectPath = "";
  let quickProjectBusy = false;
  let quickProjectError = "";
  let quickProjectEpoch = 0;
  const collapsed = new Set();
  const limits = new Map();
  let projectSort = "name";
  let previousSelection = "";
  const sidebar = container.closest(".sidebar");
  const sessionKey = (session) => `${session.project_id}/${session.id}`;
  const currentSession = (key) => (state.data?.items ?? [])
    .find((session) => sessionKey(session) === key);

  function storeContentKey(snapshot) {
    const items = snapshot.data?.items ?? [];
    if (snapshot.status === "error") return JSON.stringify(["error", errorMessage(snapshot.error)]);
    if (snapshot.status === "loading" && items.length === 0) return '"loading"';
    // A revision or timestamp can change without changing any visible row.
    // Keep the row mounted so polling cannot clear selection or menu focus.
    return JSON.stringify(recentSessions(items).map(({ project_id, id, title, status, pinned,
      model_id, agent_id }) => [project_id, id, title, status, pinned, model_id, agent_id]));
  }

  function syncTimes() {
    const sessions = new Map((state.data?.items ?? [])
      .map((session) => [sessionKey(session), session]));
    for (const button of container.querySelectorAll(".session-item")) {
      const session = sessions.get(button.dataset.sessionKey);
      const time = button.querySelector(".session-item-time");
      if (!session || !time) continue;
      time.dataset.relativeTime = String(session.updated_at);
      const label = formatRelativeTime(session.updated_at);
      if (time.textContent !== label) time.textContent = label;
    }
  }

  function syncIndicators() {
    // Polling changes only the status spans. Keep row/menu nodes, selection,
    // scroll and any quick project form intact while another task finishes.
    for (const button of container.querySelectorAll(".session-item")) {
      const key = button.dataset.sessionKey;
      const row = button.parentElement;
      const activity = activities.get(key) || "";
      if (activity) row.setAttribute("data-run-state", activity);
      else row.removeAttribute("data-run-state");
      if (unread.has(key)) row.setAttribute("data-unread", "true");
      else row.removeAttribute("data-unread");
      const indicator = button.querySelector(".session-item-activity");
      const activityLabel = activityText(activity);
      if (indicator.textContent !== activityLabel) indicator.textContent = activityLabel;
      const result = button.querySelector(".session-item-unread");
      const resultLabel = unread.has(key) ? t("nav.unread", {}, "有新结果") : "";
      if (result.textContent !== resultLabel) result.textContent = resultLabel;
    }
  }

  function positionMenu() {
    if (!openMenuNode || !openMenuButton?.isConnected) return;
    const anchor = openMenuButton.getBoundingClientRect();
    const margin = 8;
    const below = Math.max(0, window.innerHeight - anchor.bottom - margin);
    const above = Math.max(0, anchor.top - margin);
    const placeAbove = below < Math.min(openMenuNode.scrollHeight, 320) && above > below;
    openMenuNode.style.maxHeight = `${Math.max(40, Math.min(320,
      placeAbove ? above : below))}px`;
    const box = openMenuNode.getBoundingClientRect();
    openMenuNode.style.top = `${placeAbove
      ? Math.max(margin, anchor.top - box.height + 3)
      : Math.min(anchor.bottom - 3, window.innerHeight - margin - box.height)}px`;
    openMenuNode.style.left = `${Math.max(margin, Math.min(anchor.right - box.width,
      window.innerWidth - margin - box.width))}px`;
  }

  function menuAction(action, session) {
    const button = element("button", { text: action.label, attrs: { type: "button", role: "menuitem", "data-tone": action.tone ?? "neutral" } });
    button.addEventListener("click", async (event) => {
      event.stopPropagation();
      focusRequest = { key: sessionKey(session), index: -1 };
      openMenu = "";
      render();
      const current = currentSession(sessionKey(session));
      if (!current) return;
      try { await onAction(action.name, current); }
      catch (error) { toast(errorMessage(error), "error"); }
    });
    return button;
  }

  function menuFor(session) {
    return sessionActionItems(session).map((action) => menuAction(action, session));
  }

  function quickProjectForm() {
    const input = element("input", { attrs: {
      type: "text", required: "", autocomplete: "off", spellcheck: "false",
      "data-quick-project-focus": "path",
      "aria-label": t("project.workspace", {}, "工作区目录"),
      placeholder: t("project.workspacePlaceholder", {}, "例如 /work/project"),
    } });
    input.value = quickProjectPath;
    const error = element("span", { className: "project-quick-error",
      text: quickProjectError, attrs: { role: "alert" } });
    error.hidden = !quickProjectError;
    input.addEventListener("input", () => {
      quickProjectPath = input.value;
      quickProjectError = "";
      error.hidden = true;
    });
    const submit = element("button", { className: "project-quick-submit",
      text: t("project.add", {}, "添加项目"), attrs: {
        type: "submit", "data-quick-project-focus": "submit",
      } });
    const cancel = element("button", { className: "project-quick-cancel",
      text: "×", attrs: { type: "button", "data-quick-project-focus": "cancel",
        "aria-label": t("project.cancel", {}, "取消") } });
    const browse = onBrowseProject ? element("button", { className: "project-quick-browse",
      text: t("directory.browse", {}, "选择目录"), attrs: {
        type: "button", "data-quick-project-focus": "browse",
      } }) : null;
    browse?.addEventListener("click", () => {
      if (quickProjectBusy) return;
      const epoch = quickProjectEpoch;
      void onBrowseProject(quickProjectPath, (path) => {
        if (epoch !== quickProjectEpoch || !quickProjectOpen || quickProjectBusy) return;
        quickProjectPath = path; quickProjectError = "";
        render(); container.querySelector(".project-quick-add input")?.focus();
      });
    });
    const form = element("form", { className: "project-quick-add" },
      [input, browse, submit, cancel, error]);
    input.disabled = submit.disabled = cancel.disabled = quickProjectBusy;
    if (browse) browse.disabled = quickProjectBusy;
    function close() {
      if (quickProjectBusy) return;
      quickProjectEpoch += 1;
      quickProjectOpen = false;
      quickProjectPath = "";
      quickProjectError = "";
      render();
      container.querySelector(".project-list-heading .session-group-new")
        ?.focus({ preventScroll: true });
    }
    cancel.addEventListener("click", close);
    form.addEventListener("keydown", (event) => {
      if (event.key !== "Escape" || isImeKey(event)) return;
      event.preventDefault();
      close();
    });
    form.addEventListener("submit", async (event) => {
      event.preventDefault();
      if (quickProjectBusy) return;
      const path = quickProjectPath.trim();
      if (!path) { input.focus(); return; }
      quickProjectBusy = true;
      quickProjectError = "";
      const epoch = quickProjectEpoch;
      render();
      let project;
      try { project = await onAddProject(path); }
      catch (cause) {
        quickProjectBusy = false;
        if (epoch !== quickProjectEpoch) { render(); return; }
        quickProjectError = errorMessage(cause);
        render();
        container.querySelector(".project-quick-add input")?.focus();
        return;
      }
      quickProjectBusy = false;
      if (epoch !== quickProjectEpoch) {
        render();
        toast(t("project.added", { name: project.name }, `已添加项目 ${project.name}`));
        return;
      }
      quickProjectOpen = false;
      quickProjectPath = "";
      render();
      onNewInProject(project.id);
      toast(t("project.added", { name: project.name }, `已添加项目 ${project.name}`));
    });
    return form;
  }

  function sidebarFocus(key) {
    return [...container.querySelectorAll("[data-sidebar-focus]")]
      .find((node) => node.dataset.sidebarFocus === key);
  }

  function updateTree(key, change) {
    focusRequest = { sidebar: key };
    openMenu = "";
    change();
    render();
  }

  function render() {
    const requestedFocus = focusRequest;
    focusRequest = null;
    const focused = document.activeElement;
    const scrollTop = container.scrollTop;
    const quickFocus = focused?.closest?.(".project-quick-add")
      ? focused.dataset.quickProjectFocus : "";
    const quickSelection = quickFocus === "path"
      ? [focused.selectionStart, focused.selectionEnd] : null;
    const retainedSidebarFocus = container.contains(focused)
      ? focused.dataset.sidebarFocus : "";
    const focusedMenu = focused?.closest?.(".session-menu");
    const retainedFocus = focusedMenu?.dataset.sessionKey === openMenu
      ? { key: openMenu, index: Number(focused.dataset.menuIndex) }
      : focused?.classList?.contains("session-more")
        ? { key: focused.dataset.sessionKey, index: -1 }
        : focused?.classList?.contains("session-item")
          ? { key: focused.dataset.sessionKey, index: -2 } : null;
    const menuScroll = openMenuNode?.scrollTop || 0;
    openMenuNode?.remove();
    openMenuNode = null;
    openMenuButton = null;
    let restoredFocus = false;
    const selected = navigation.get();
    const selectedKey = `${selected.projectId}/${selected.sessionId}`;
    const groups = sidebarGroups({ sessions: state.data?.items,
      projects: projectsStore.get().data?.items, status, query,
      locale: currentLocale(), projectSort, defaultProjectName: t("nav.defaultProject") });
    const searching = Boolean(query.trim());
    count.textContent = String(groups.total);
    container.setAttribute("aria-busy", String(state.status === "loading"));
    clear(container);

    function restoreQuickProjectFocus() {
      if (!quickFocus) return;
      const replacement = container.querySelector(`[data-quick-project-focus="${quickFocus}"]`)
        ?? container.querySelector(".project-quick-add input");
      replacement?.focus({ preventScroll: true });
      if (quickSelection && replacement?.setSelectionRange)
        replacement.setSelectionRange(...quickSelection);
    }

    if (state.status === "error") {
      openMenu = "";
      container.append(element("div", { className: "resource-error", text: errorMessage(state.error) }));
      return;
    }
    if (state.status === "loading" && !state.data?.items?.length) {
      openMenu = "";
      container.append(element("div", { className: "empty-state",
        text: t("nav.loading", {}, "正在载入会话…") }));
      return;
    }

    const icon = (name, className = "") => element("span", {
      className: `icon ${className}`, attrs: { "data-icon": name, "aria-hidden": "true" },
    });
    const toggleLabel = (name, expanded) => expanded
      ? t("nav.collapseGroup", { name }, `折叠 ${name}`)
      : t("nav.expandGroup", { name }, `展开 ${name}`);

    function toggleButton(key, name, body, className, iconName = "") {
      const expanded = searching || !collapsed.has(key);
      const button = element("button", { className, attrs: {
        type: "button", "data-sidebar-focus": key,
        "aria-label": toggleLabel(name, expanded), "aria-expanded": String(expanded),
        "aria-controls": body.id, title: name,
      } }, [iconName ? icon(iconName, "session-folder-icon") : null,
        element("span", { className: "session-group-name", text: name }),
        icon("chevron-down", "session-group-chevron")]);
      body.hidden = !expanded;
      button.disabled = searching;
      button.addEventListener("click", () => updateTree(key, () => {
        if (collapsed.has(key)) collapsed.delete(key); else collapsed.add(key);
      }));
      button.addEventListener("keydown", (event) => {
        if (isImeKey(event) || !["ArrowLeft", "ArrowRight"].includes(event.key)) return;
        event.preventDefault();
        if (event.key === "ArrowLeft" && expanded || event.key === "ArrowRight" && !expanded)
          button.click();
        else if (event.key === "ArrowRight")
          body.querySelector(".session-item, .session-group-main")?.focus();
      });
      return button;
    }

    function actionButton(label, iconName, focusKey, action, className) {
      const button = element("button", { className, attrs: {
        type: "button", "data-sidebar-focus": focusKey,
        "aria-label": label, title: label,
      } }, [icon(iconName)]);
      button.addEventListener("click", action);
      return button;
    }

    function attachMenu(key, button, entries) {
      const target = requestedFocus?.key === key ? requestedFocus :
        retainedFocus?.key === key ? retainedFocus : null;
      if (openMenu === key) {
        const menu = element("div", { className: "session-menu session-list-menu", attrs: {
          id: "sidebar-session-menu", role: "menu", "data-session-key": key,
          "aria-label": button.getAttribute("aria-label"),
        } }, entries());
        [...menu.children].forEach((item, index) => { item.dataset.menuIndex = String(index); });
        button.setAttribute("aria-controls", menu.id);
        document.body.append(menu);
        openMenuNode = menu;
        openMenuButton = button;
        positionMenu();
        menu.scrollTop = menuScroll;
      }
      if (target && target.index !== -2) {
        if (target.index < 0) button.focus({ preventScroll: true });
        else openMenuNode?.children[target.index]?.focus({ preventScroll: true });
        restoredFocus = true;
      }
    }

    function menuButton(key, label, focusKey, entries, className = "session-group-manage") {
      const button = actionButton(label, "more", focusKey, (event) => {
        event.stopPropagation();
        openMenu = openMenu === key ? "" : key;
        focusRequest = { key, index: openMenu && event.detail === 0 ? 0 : -1 };
        render();
      }, className);
      button.setAttribute("aria-haspopup", "menu");
      button.setAttribute("aria-expanded", String(openMenu === key));
      button.dataset.sessionKey = key;
      return { button, mount: () => attachMenu(key, button, entries) };
    }

    function option(label, focusKey, action, { tone = "neutral", checked } = {}) {
      const button = element("button", { attrs: { type: "button",
        role: checked === undefined ? "menuitem" : "menuitemradio",
        "aria-checked": checked === undefined ? null : String(checked), "data-tone": tone,
      } }, [element("span", { text: label }), checked === true
        ? element("span", { className: "session-menu-check", text: "✓", attrs: { "aria-hidden": "true" } }) : null]);
      button.addEventListener("click", async () => {
        focusRequest = { sidebar: focusKey };
        openMenu = "";
        render();
        try { await action(sidebarFocus(focusKey)); }
        catch (error) { toast(errorMessage(error), "error"); }
      });
      return button;
    }

    function section(key, label, controls = []) {
      const body = element("div", { className: "sidebar-section-body", attrs: { id: `sidebar-${key}` } });
      const toggle = toggleButton(`section:${key}`, label, body, "sidebar-section-toggle");
      const heading = element("div", { className: "session-group-heading sidebar-section-heading" +
        (key === "projects" ? " project-list-heading" : "") },
        [toggle, ...controls.map((control) => control.button ?? control)]);
      const wrapper = element("section", { className: `sidebar-section sidebar-${key}`,
        attrs: { "aria-label": label } }, [heading, body]);
      container.append(wrapper);
      for (const control of controls) control.mount?.();
      return body;
    }

    function appendSession(target, session, pinned = false) {
      const key = sessionKey(session);
      const project = projectsStore.get().data?.items?.find((item) => item.id === session.project_id);
      const label = session.title || t("nav.untitled", {}, "未命名任务");
      const button = element("button", { className: "session-item", attrs: {
        type: "button", "data-session-key": key,
        "aria-current": key === selectedKey ? "page" : null,
        title: `${label}\n${project?.name || session.project_id} · ${session.model_id || session.agent_id}`,
      } }, [element("span", { className: "session-item-label" }, [
        pinned ? icon("pin", "session-pin") : null,
        element("span", { className: "session-item-title", text: label }),
      ]), element("span", { className: "session-item-trailing" }, [
        element("span", { className: "session-run-indicator", attrs: { "aria-hidden": "true" } }),
        element("time", { className: "session-item-time", text: formatRelativeTime(session.updated_at),
          attrs: { "data-relative-time": session.updated_at } }),
      ]), element("span", { className: "sr-only session-item-status" }, [
        element("span", { className: "session-item-activity" }),
        element("span", { className: "session-item-unread" }),
      ])]);
      button.addEventListener("click", (event) => {
        const current = currentSession(key);
        if (current) onSelect(current, event);
      });
      const menu = menuButton(key, t("nav.actionsFor", { title: label }, `${label} 的操作`),
        `session-menu:${key}`, () => menuFor(session), "session-more");
      target.append(element("li", { className: "session-item-row", attrs: {
        "data-status": session.status,
      } }, [button, menu.button]));
      menu.mount();
      const targetFocus = requestedFocus?.key === key ? requestedFocus : retainedFocus?.key === key ? retainedFocus : null;
      if (targetFocus?.index === -2) { button.focus({ preventScroll: true }); restoredFocus = true; }
    }

    function appendSessions(body, sessions, key, initialLimit = 5, pinned = false) {
      const windowed = sidebarWindow(sessions, limits.get(key) ?? initialLimit, selectedKey, searching);
      const list = element("ul", { className: "session-group-children" });
      body.append(list);
      if (!sessions.length) list.append(element("li", { className: "session-group-empty",
        text: t("nav.emptyGroup", {}, "暂无任务") }));
      for (const session of windowed.items) appendSession(list, session, pinned);
      if (windowed.remaining > 0) {
        const more = element("button", { className: "session-show-more", text: t("nav.showMore", {}, "显示更多"), attrs: {
          type: "button", "data-sidebar-focus": `more:${key}`,
          "aria-label": t("nav.showMoreTasks", { count: windowed.remaining }, `显示其余 ${windowed.remaining} 个任务`),
        } });
        more.addEventListener("click", () => updateTree(`more:${key}`, () => {
          limits.set(key, (limits.get(key) ?? initialLimit) + initialLimit);
        }));
        body.append(more);
      }
    }

    if (groups.pinned.length) appendSessions(section("pinned", t("nav.pinned", {}, "已置顶")),
      groups.pinned, "pinned", groups.pinned.length, true);

    const listOptions = () => [
      ...["active", "archived", "trash", "all"].map((value) => option(
        t(`shell.status.${value}`), "list-options", () => {
          filter.value = value;
          onFilterChange();
        }, { checked: status === value })),
      option(t("nav.sortProjectsName", {}, "项目按名称排序"), "list-options", () => {
        projectSort = "name"; render();
      }, { checked: projectSort === "name" }),
      option(t("nav.sortProjectsRecent", {}, "项目按最近活动排序"), "list-options", () => {
        projectSort = "recent"; render();
      }, { checked: projectSort === "recent" }),
      option(t("nav.expandAll", {}, "展开所有项目"), "list-options", () => {
        collapsed.delete("section:projects");
        for (const group of groups.projects) collapsed.delete(`project:${group.id}`);
        render();
      }),
      option(t("nav.collapseAll", {}, "折叠所有项目"), "list-options", () => {
        for (const group of groups.projects) collapsed.add(`project:${group.id}`);
        render();
      }),
      option(t("nav.manageProjects", {}, "管理项目"), "list-options", () => onManageProjects?.()),
    ];
    const options = menuButton("list:options", t("nav.listOptions", {}, "列表选项"), "list-options", listOptions);
    const add = actionButton(t("nav.addProject", {}, "添加项目"), "plus", "add-project", () => {
      collapsed.delete("section:projects");
      quickProjectOpen = true; render();
      container.querySelector(".project-quick-add input")?.focus();
    }, "session-group-new");
    add.disabled = quickProjectBusy;
    const projectLabel = status === "active" ? t("nav.projects", {}, "项目") :
      `${t("nav.projects", {}, "项目")} · ${t(`shell.status.${status}`)}`;
    const projectBody = section("projects", projectLabel, status === "active" ? [options, add] : [options]);
    if (quickProjectOpen && status === "active") projectBody.append(quickProjectForm());
    for (const group of groups.projects) {
      const key = `project:${group.id}`;
      const body = element("div", { className: "session-project-body", attrs: { id: `sidebar-project-${group.id}` } });
      const toggle = toggleButton(key, group.name, body, "session-group-main",
        searching || !collapsed.has(key) ? "folder-open" : "folder");
      const newLabel = t("nav.newInProject", { name: group.name }, `在 ${group.name} 项目新建任务`);
      const create = actionButton(newLabel, "plus", `new:${key}`, () => onNewInProject(group.id), "session-group-new");
      const projectOptions = menuButton(`menu:${key}`, t("nav.manageProject", { name: group.name }, `管理 ${group.name} 项目`),
        `menu:${key}`, () => [
          option(t("project.newTask", {}, "新任务"), `menu:${key}`, () => onNewInProject(group.id)),
          ...(group.project?.managed && onProjectAction ? [
            option(t("nav.projectSettings", {}, "项目设置"), `menu:${key}`,
              (origin) => onProjectAction("edit", group.id, origin)),
            option(t("project.unregister", {}, "取消注册"), `menu:${key}`,
              (origin) => onProjectAction("unregister", group.id, origin), { tone: "danger" }),
          ] : []),
          option(t("nav.manageProjects", {}, "管理项目"), `menu:${key}`, () => onManageProject?.(group.id)),
        ]);
      const heading = element("div", { className: "session-group-heading session-project-heading" },
        [toggle, create, projectOptions.button]);
      projectBody.append(element("div", { className: "session-project", attrs: { "data-project-id": group.id } }, [heading, body]));
      projectOptions.mount();
      appendSessions(body, group.sessions, key);
    }
    if (!groups.projects.length && !quickProjectOpen) {
      const key = searching ? "nav.noMatch" : status === "trash" ? "nav.trashEmpty" :
        status === "archived" ? "nav.noArchivedSessions" : "nav.noProjects";
      projectBody.append(element("p", { className: "session-projects-empty", text: t(key) }));
    }

    const createDefault = actionButton(t("nav.newInDefaultProject", {}, "在默认项目新建任务"), "plus", "new:tasks",
      () => onNewInProject("default"), "session-group-new");
    const tasksBody = section("tasks", t("nav.tasks", {}, "任务"), status === "active" ? [createDefault] : []);
    if (!groups.tasks.length) {
      const key = searching ? "nav.noMatch" : status === "trash" ? "nav.trashEmpty" :
        status === "archived" ? "nav.noArchivedSessions" : "nav.emptyGroup";
      tasksBody.append(element("p", { className: "session-group-empty", text: t(key) }));
    } else appendSessions(tasksBody, groups.tasks, "tasks", 20);

    if (openMenu && !openMenuNode) openMenu = "";
    mountIcons(container);
    container.scrollTop = scrollTop;
    const controlKey = requestedFocus?.sidebar || retainedSidebarFocus;
    const control = controlKey && sidebarFocus(controlKey);
    if (!restoredFocus && control) { control.focus({ preventScroll: true }); restoredFocus = true; }
    if (!restoredFocus && (requestedFocus || retainedFocus)) {
      // Once the final page is expanded, its More button disappears. Keep
      // keyboard focus at the newly revealed end of that group.
      const expandedKey = controlKey?.startsWith("more:") ? controlKey.slice(5) : "";
      const expandedGroup = expandedKey && (sidebarFocus(expandedKey) ?? sidebarFocus(`section:${expandedKey}`));
      const expandedBody = expandedGroup && document.getElementById(expandedGroup.getAttribute("aria-controls"));
      const expandedTasks = [...(expandedBody?.querySelectorAll(".session-item") ?? [])];
      (expandedTasks.at(-1) ?? container.querySelector('.session-item[aria-current="page"]') ??
        container.querySelector(".session-item, .sidebar-section-toggle") ?? filter).focus();
    }
    restoreQuickProjectFocus();
    syncIndicators();
  }

  const unsubscribeStore = store.subscribe((next) => {
    state = next;
    const key = storeContentKey(next);
    if (key !== renderedStoreKey) {
      renderedStoreKey = key;
      render();
    } else {
      container.setAttribute("aria-busy", String(next.status === "loading"));
      syncTimes();
    }
  });
  const unsubscribeProjects = projectsStore.subscribe(render);
  const unsubscribeNavigation = navigation.subscribe(() => {
    const selected = navigation.get();
    const key = `${selected.projectId}/${selected.sessionId}`;
    if (key !== previousSelection && selected.projectId) {
      const section = selected.projectId === "default" ? "tasks" : "projects";
      collapsed.delete(`section:${section}`);
      collapsed.delete(`project:${selected.projectId}`);
      if (selected.sessionId) collapsed.delete("section:pinned");
      previousSelection = key;
    }
    openMenu = "";
    quickProjectEpoch += 1;
    quickProjectOpen = false;
    quickProjectPath = "";
    quickProjectError = "";
    render();
  });
  const unsubscribeLocale = subscribeLocale(render);
  const unsubscribeRuns = runsStore?.subscribe((snapshot) => {
    // An unreadable refresh is not evidence that a task has stopped.
    if (snapshot.status !== "ready") return;
    activities = sessionRunActivities(snapshot.data?.items);
    syncIndicators();
  });
  function onFilterChange() {
    status = filter.value;
    openMenu = "";
    quickProjectEpoch += 1;
    quickProjectOpen = false;
    quickProjectPath = "";
    quickProjectError = "";
    container.scrollTop = 0;
    render();
  }
  filter.addEventListener("change", onFilterChange);
  function sessionButtons() {
    return [...container.querySelectorAll(".session-item")]
      .filter((button) => button.getClientRects().length);
  }
  function onSearchKeyDown(event) {
    if (event.key !== "ArrowDown" || isImeKey(event)) return;
    const first = sessionButtons()[0];
    if (!first) return;
    event.preventDefault();
    first.focus();
  }
  function onSessionKeyDown(event) {
    if (event.defaultPrevented || isImeKey(event) || event.altKey ||
        event.ctrlKey || event.metaKey ||
        !event.target?.classList?.contains("session-item")) return;
    const buttons = sessionButtons();
    const index = buttons.indexOf(event.target);
    if (index < 0) return;
    if (event.key === "ArrowLeft") {
      const parent = event.target.closest(".session-project");
      if (parent) {
        event.preventDefault();
        parent.querySelector(".session-group-main")?.focus();
      }
      return;
    }
    if (event.key === "ArrowDown") {
      event.preventDefault();
      buttons[(index + 1) % buttons.length].focus();
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      if (index === 0) (searchInput ?? filter).focus();
      else buttons[index - 1].focus();
    }
  }
  searchInput?.addEventListener("keydown", onSearchKeyDown);
  container.addEventListener("keydown", onSessionKeyDown);
  container.addEventListener("scroll", onListScroll);
  sidebar?.addEventListener("scroll", onSidebarScroll);
  window.addEventListener("resize", positionMenu);
  document.addEventListener("pointerdown", onOutsidePointerDown);
  document.addEventListener("focusin", onFocusIn);
  document.addEventListener("keydown", onDismissKeyDown);

  function currentMenu() {
    return openMenuNode?.dataset.sessionKey === openMenu ? openMenuNode : null;
  }

  function closeMenu(restoreFocus = false) {
    const menu = currentMenu();
    const more = openMenuButton;
    openMenu = "";
    menu?.remove();
    openMenuNode = null;
    openMenuButton = null;
    if (more) {
      more.setAttribute("aria-expanded", "false");
      if (restoreFocus) more.focus();
    }
  }

  function onOutsidePointerDown(event) {
    const menu = currentMenu();
    if (menu && !menu.contains(event.target) && event.target !== openMenuButton)
      closeMenu();
  }

  function onFocusIn(event) {
    const menu = currentMenu();
    if (menu && !menu.contains(event.target) && event.target !== openMenuButton)
      closeMenu();
  }

  function onListScroll() {
    if (!openMenuButton) return;
    const anchor = openMenuButton.getBoundingClientRect();
    const bounds = container.getBoundingClientRect();
    if (anchor.bottom <= bounds.top || anchor.top >= bounds.bottom ||
        anchor.bottom <= 0 || anchor.top >= window.innerHeight) closeMenu();
    else positionMenu();
  }

  function onSidebarScroll() {
    // The drawer itself can scroll on short screens. A menu anchored to a
    // moving row should close rather than float over unrelated controls.
    if (currentMenu()) closeMenu();
  }

  function onDismissKeyDown(event) {
    const menu = currentMenu();
    if (!menu) return;
    if (event.key === "Escape") {
      if (isImeKey(event)) return;
      event.preventDefault();
      event.stopImmediatePropagation();
      closeMenu(true);
      return;
    }
    if (!menu.contains(event.target) && event.target !== openMenuButton)
      return;
    const items = [...menu.querySelectorAll('[role="menuitem"], [role="menuitemradio"]')];
    const index = items.indexOf(document.activeElement);
    let next = index;
    if (event.key === "ArrowDown") next = index < 0 ? 0 : (index + 1) % items.length;
    else if (event.key === "ArrowUp") next = index < 0 ? items.length - 1
      : (index - 1 + items.length) % items.length;
    else if (event.key === "Home") next = 0;
    else if (event.key === "End") next = items.length - 1;
    else return;
    event.preventDefault();
    items[next]?.focus();
  }

  return Object.freeze({
    setQuery(value) { query = value; container.scrollTop = 0; render(); },
    setUnread(keys) { unread = new Set(keys); syncIndicators(); },
    showActive() { status = "active"; query = ""; filter.value = status; openMenu = ""; render(); },
    destroy() {
      unsubscribeStore();
      unsubscribeProjects();
      unsubscribeNavigation();
      unsubscribeLocale();
      unsubscribeRuns?.();
      filter.removeEventListener("change", onFilterChange);
      openMenuNode?.remove();
      searchInput?.removeEventListener("keydown", onSearchKeyDown);
      container.removeEventListener("keydown", onSessionKeyDown);
      container.removeEventListener("scroll", onListScroll);
      sidebar?.removeEventListener("scroll", onSidebarScroll);
      window.removeEventListener("resize", positionMenu);
      document.removeEventListener("pointerdown", onOutsidePointerDown);
      document.removeEventListener("focusin", onFocusIn);
      document.removeEventListener("keydown", onDismissKeyDown);
    },
  });
}
