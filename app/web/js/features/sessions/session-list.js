import { element, clear, formatRelativeTime, errorMessage, isImeKey, toast } from "../../utils/dom.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";
import { sessionActionItems } from "./session-actions.js";
import { mountIcons } from "../../components/icons.js";
import { recentSessions, sidebarGroups, sidebarWindow, matchingSessions } from "./sidebar-tree.js";
import { createSidebarLayoutStore, organizedSidebar, timelineSidebar } from "./sidebar-layout.js";
import { createSidebarOrganizer } from "./sidebar-organizer.js";
import { isTransientReadError } from "../../api/read-recovery.js";

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

export function createSessionList({ container, count, filter, searchButton, store, projectsStore, runsStore,
  navigation, onSelect, onAction, onNewInProject, onAddProject,
  onRefreshProjects, onProjectAction, onRetrySessions }) {
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
  const collapsed = new Set();
  const limits = new Map();
  let projectSort = "name";
  let previousSelection = "";
  const sidebar = container.closest(".sidebar");
  let organizer = null;
  const layoutStore = createSidebarLayoutStore({ changed() {
    collapsed.clear(); for (const key of layoutStore.get().collapsed) collapsed.add(key);
    projectSort = layoutStore.get().project_sort; render();
  }, failed(error) { toast(errorMessage(error), "error"); } });
  const sessionKey = (session) => `${session.project_id}/${session.id}`;
  const currentSession = (key) => (state.data?.items ?? [])
    .find((session) => sessionKey(session) === key);
  let readError = null;
  const readMessage = element("p");
  const readRetry = element("button", { className: "text-button session-read-retry",
    attrs: { type: "button" } });
  const readNotice = element("div", { className: "resource-error session-read-notice",
    attrs: { role: "status" } }, [readMessage, readRetry]);
  readRetry.addEventListener("click", () => {
    if (store.isPending?.()) return;
    void onRetrySessions?.();
    syncReadNotice();
  });

  function syncReadNotice() {
    if (state.status === "error") readError = state.error;
    else if (state.status === "ready" || state.status === "idle") readError = null;
    if (!readError) {
      const hadFocus = readNotice.contains(document.activeElement);
      readNotice.remove();
      if (hadFocus) (container.querySelector('.session-item[aria-current="page"]') ??
        searchButton ?? filter).focus({ preventScroll: true });
      return;
    }
    readMessage.textContent = isTransientReadError(readError)
      ? t("sessionList.readFailed", {}, "暂时无法读取会话列表，请检查连接后重试。")
      : errorMessage(readError);
    const pending = Boolean(store.isPending?.());
    readRetry.textContent = pending ? t("sessionLoad.retrying") : t("sessionLoad.retry");
    readRetry.setAttribute("aria-disabled", String(pending));
    readRetry.hidden = !onRetrySessions;
    if (!readNotice.isConnected) container.prepend(readNotice);
  }

  function coldRead(snapshot) {
    return !snapshot.updatedAt && !snapshot.data?.items?.length &&
      ["loading", "refreshing"].includes(snapshot.status);
  }

  function storeContentKey(snapshot) {
    const items = snapshot.data?.items ?? [];
    if (snapshot.status === "error" && !snapshot.updatedAt && !items.length) return '"unavailable"';
    if (coldRead(snapshot)) return '"loading"';
    // A revision or timestamp can change without changing any visible row.
    // Keep the row mounted so polling cannot clear selection or menu focus.
    const timeline = layoutStore.get().view === "timeline";
    return JSON.stringify([timeline ? new Date().toDateString() : "", recentSessions(items).map(({ project_id, id, title, status, pinned,
      model_id, agent_id, updated_at }) => [project_id, id, title, status, pinned, model_id, agent_id,
        timeline ? new Date(Number(updated_at || 0) * 1000).toDateString() : ""])]);
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
    // and scroll intact while another task finishes.
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
    const placeAbove = below < Math.min(openMenuNode.scrollHeight, 480) && above > below;
    openMenuNode.style.maxHeight = `${Math.max(40, Math.min(480,
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
      try { if (action.name === "organize") organizer.move(`s:${sessionKey(current)}`); else await onAction(action.name, current); }
      catch (error) { if (error?.name !== "AbortError") toast(errorMessage(error), "error"); }
    });
    return button;
  }

  function menuFor(session) {
    const actions = sessionActionItems(session);
    if (session.status !== "trash" && layoutStore.writable()) actions.push({ name: "organize", label: t("sidebar.moveToGroup") });
    return actions.map((action) => menuAction(action, session));
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
    if (organizer?.dragging()) return;
    const requestedFocus = focusRequest;
    focusRequest = null;
    const focused = document.activeElement;
    const retainedReadFocus = readNotice.contains(focused);
    const scrollTop = container.scrollTop;
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
    const baseGroups = sidebarGroups({ sessions: state.data?.items,
      projects: projectsStore.get().data?.items, status, query,
      locale: currentLocale(), projectSort, defaultProjectName: t("nav.defaultProject") });
    const groups = organizedSidebar(baseGroups, layoutStore.get());
    const workspaceView = layoutStore.get().view === "workspace";
    const searching = Boolean(query.trim());
    count.textContent = String(groups.total);
    container.setAttribute("aria-busy", String(coldRead(state) || layoutStore.busy()));
    clear(container);
    syncReadNotice();

    if (state.status === "error" && !state.updatedAt && !state.data?.items?.length) {
      openMenu = "";
      if (retainedReadFocus) readRetry.focus({ preventScroll: true });
      return;
    }
    if (coldRead(state)) {
      openMenu = "";
      container.append(element("div", { className: "empty-state",
        text: t("nav.loading", {}, "正在载入会话…") }));
      if (retainedReadFocus && readNotice.isConnected) readRetry.focus({ preventScroll: true });
      return;
    }

    const toolbar = element("div", { className: "sidebar-organize-toolbar", attrs: { role: "group", "aria-label": t("sidebar.groupHint") } });
    for (const view of ["workspace", "timeline"]) {
      const button = element("button", { text: t(`sidebar.view.${view}`), attrs: { type: "button", title: t(`sidebar.view.${view}`),
        "data-sidebar-focus": `view:${view}`, "aria-pressed": String(layoutStore.get().view === view) } });
      button.disabled = !layoutStore.writable();
      button.addEventListener("click", () => { focusRequest = { sidebar: `view:${view}` }; void organizer.save(value => ({ ...value, view })); });
      toolbar.append(button);
    }
    const addGroup = element("button", { text: "+", attrs: { type: "button", title: t("sidebar.newGroup"),
      "aria-label": t("sidebar.newGroup"), "data-sidebar-focus": "add-group" } });
    addGroup.disabled = !layoutStore.writable() || layoutStore.get().groups.length >= 32;
    addGroup.addEventListener("click", () => organizer.groupForm()); toolbar.append(addGroup); container.append(toolbar);

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
        const next = new Set(collapsed);
        if (next.has(key)) next.delete(key); else next.add(key);
        if (layoutStore.writable()) void organizer.save(value => ({ ...value, collapsed: [...next] }));
        else { collapsed.clear(); for (const item of next) collapsed.add(item); }
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

    function option(label, focusKey, action, { tone = "neutral", checked, disabled = false } = {}) {
      const button = element("button", { attrs: { type: "button",
        role: checked === undefined ? "menuitem" : "menuitemradio",
        "aria-checked": checked === undefined ? null : String(checked), "data-tone": tone,
      } }, [element("span", { text: label }), checked === true
        ? element("span", { className: "session-menu-check", text: "✓", attrs: { "aria-hidden": "true" } }) : null]);
      button.disabled = disabled;
      button.addEventListener("click", async () => {
        focusRequest = { sidebar: focusKey };
        openMenu = "";
        render();
        try { await action(sidebarFocus(focusKey)); }
        catch (error) { toast(errorMessage(error), "error"); }
      });
      return button;
    }

    function section(key, label, controls = [], group = "") {
      const body = element("div", { className: "sidebar-section-body", attrs: { id: `sidebar-${key}` } });
      body.dataset.sidebarDropZone = ""; body.dataset.sidebarKey = group ? `g:${group}` : `zone:${key}`;
      body.dataset.sidebarGroup = group; body.dataset.sidebarParent = group ? `custom:${group}` : key;
      const toggle = toggleButton(`section:${key}`, label, body, "sidebar-section-toggle");
      const heading = element("div", { className: "session-group-heading sidebar-section-heading" +
        (key === "projects" ? " project-list-heading" : "") },
        [toggle, ...controls.map((control) => control.button ?? control)]);
      const wrapper = element("section", { className: `sidebar-section sidebar-${key}`,
        attrs: { "aria-label": label } }, [heading, body]);
      if (group) {
        wrapper.dataset.sidebarKey = `g:${group}`; wrapper.dataset.sidebarParent = "groups";
        wrapper.dataset.sidebarGroup = group; wrapper.dataset.sidebarDropZone = "";
        wrapper.dataset.sidebarSectionId = group;
        heading.prepend(dragHandle(`g:${group}`, label));
        // The body is a drop zone, but only the section header owns its ID.
        delete body.dataset.sidebarKey;
      }
      container.append(wrapper);
      for (const control of controls) control.mount?.();
      return body;
    }

    function dragHandle(key, label) {
      const button = element("button", { className: "sidebar-drag-handle", text: "⠿", attrs: {
        type: "button", "data-sidebar-drag": key, "data-sidebar-focus": `drag:${key}`,
        title: t("sidebar.dragHint"), "aria-label": t("sidebar.dragItem", { name: label }) } });
      button.disabled = !layoutStore.writable() || searching || !workspaceView;
      return button;
    }
    function appendSession(target, session, pinned = false, parent = "") {
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
      const assignment = layoutStore.get().placements.find(item => item.key === `s:${key}`)?.group || "";
      const row = element("li", { className: "session-item-row", attrs: {
        "data-status": session.status,
        "data-sidebar-key": `s:${key}`, "data-sidebar-parent": pinned ? "pinned" : parent,
        "data-sidebar-group": pinned ? assignment : parent.startsWith("custom:") ? parent.slice(7) : "",
      } }, [dragHandle(`s:${key}`, label), button, menu.button]);
      target.append(row);
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
      for (const session of windowed.items) appendSession(list, session, pinned, key);
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

    const manualProjects = layoutStore.get().placements.some(item => item.key.startsWith("p:") && !item.group);
    const listOptions = () => [
      ...["active", "archived", "trash", "all"].map((value) => option(
        t(`shell.status.${value}`), "list-options", () => {
          filter.value = value;
          onFilterChange();
        }, { checked: status === value })),
      option(t("nav.sortProjectsName", {}, "项目按名称排序"), "list-options", () => {
        if (!layoutStore.writable()) { projectSort = "name"; render(); return; }
        return organizer.save(value => ({ ...value, project_sort: "name", placements: value.placements.filter(item => !item.key.startsWith("p:") || item.group) }));
      }, { checked: !manualProjects && projectSort === "name" }),
      option(t("nav.sortProjectsRecent", {}, "项目按最近活动排序"), "list-options", () => {
        if (!layoutStore.writable()) { projectSort = "recent"; render(); return; }
        return organizer.save(value => ({ ...value, project_sort: "recent", placements: value.placements.filter(item => !item.key.startsWith("p:") || item.group) }));
      }, { checked: !manualProjects && projectSort === "recent" }),
      option(t("nav.expandAll", {}, "展开所有项目"), "list-options", () => {
        collapsed.delete("section:projects");
        for (const group of baseGroups.projects) collapsed.delete(`project:${group.id}`);
        const next = [...collapsed];
        if (layoutStore.writable()) return organizer.save(value => ({ ...value, collapsed: next }));
        render();
      }),
      option(t("nav.collapseAll", {}, "折叠所有项目"), "list-options", () => {
        for (const group of baseGroups.projects) collapsed.add(`project:${group.id}`);
        const next = [...collapsed];
        if (layoutStore.writable()) return organizer.save(value => ({ ...value, collapsed: next }));
        render();
      }),
      option(t("project.refresh", {}, "刷新"), "list-options", () => onRefreshProjects?.()),
      ...["workspace", "timeline"].map(view => option(t(`sidebar.view.${view}`), "list-options", () =>
        organizer.save(value => ({ ...value, view })), { checked: layoutStore.get().view === view, disabled: !layoutStore.writable() })),
      option(t("sidebar.newGroup"), "list-options", () => organizer.groupForm(), { disabled: !layoutStore.writable() || layoutStore.get().groups.length >= 32 }),
      option(t("sidebar.reload"), "list-options", () => layoutStore.load()),
      option(t("sidebar.reset"), "list-options", () => organizer.reset(), { disabled: !layoutStore.writable() }),
    ];
    const options = menuButton("list:options", t("nav.listOptions", {}, "列表选项"), "list-options", listOptions);
    const add = actionButton(t("nav.addProject", {}, "添加项目"), "plus", "add-project", (event) => {
      collapsed.delete("section:projects");
      void onAddProject?.(event.currentTarget);
    }, "session-group-new");
    const projectLabel = status === "active" ? t("nav.projects", {}, "项目") :
      `${t("nav.projects", {}, "项目")} · ${t(`shell.status.${status}`)}`;
    function appendProject(target, group, parent = "projects", assignment = "") {
      const key = `project:${group.id}`;
      const body = element("div", { className: "session-project-body", attrs: { id: `sidebar-project-${group.id}` } });
      const toggle = toggleButton(key, group.name, body, "session-group-main",
        searching || !collapsed.has(key) ? "folder-open" : "folder");
      const newLabel = t("nav.newInProject", { name: group.name }, `在 ${group.name} 项目新建任务`);
      const create = actionButton(newLabel, "plus", `new:${key}`, () => onNewInProject(group.id), "session-group-new");
      const projectOptions = menuButton(`menu:${key}`, t("nav.manageProject", { name: group.name }, `管理 ${group.name} 项目`),
        `menu:${key}`, () => [
          option(t("project.newTask", {}, "新任务"), `menu:${key}`, () => onNewInProject(group.id)),
          option(t("sidebar.moveToGroup"), `menu:${key}`, () => organizer.move(`p:${group.id}`), { disabled: !layoutStore.writable() }),
          ...(onProjectAction ? [option(t("transfer.sendProject", {}, "发送完整项目到设备"), `menu:${key}`,
            (origin) => onProjectAction("send_device", group.id, origin))] : []),
          ...(group.project?.managed && onProjectAction ? [
            option(t("nav.projectSettings", {}, "项目设置"), `menu:${key}`,
              (origin) => onProjectAction("edit", group.id, origin)),
            option(t("project.unregister", {}, "取消注册"), `menu:${key}`,
              (origin) => onProjectAction("unregister", group.id, origin), { tone: "danger" }),
            option(t("project.preview", {}, "核对清除范围"), `menu:${key}`,
              (origin) => onProjectAction("purge", group.id, origin)),
          ] : []),
          ...(onProjectAction ? [option(t("project.memory", {}, "项目记忆"), `menu:${key}`,
            () => onProjectAction("memory", group.id))] : []),
        ]);
      const heading = element("div", { className: "session-group-heading session-project-heading" },
        [dragHandle(`p:${group.id}`, group.name), toggle, create, projectOptions.button]);
      target.append(element("div", { className: "session-project", attrs: { "data-project-id": group.id,
        "data-sidebar-key": `p:${group.id}`, "data-sidebar-parent": parent, "data-sidebar-group": assignment } }, [heading, body]));
      projectOptions.mount();
      appendSessions(body, group.sessions, key);
    }
    if (workspaceView) for (const group of groups.custom) {
      if (searching && !group.entries.length && !group.name.toLocaleLowerCase(currentLocale()).includes(query.trim().toLocaleLowerCase(currentLocale()))) continue;
      const menu = menuButton(`custom:${group.id}`, t("sidebar.manageGroup"), `custom-menu:${group.id}`, () => [
        option(t("sidebar.renameGroup"), `custom-menu:${group.id}`, () => organizer.groupForm(group), { disabled: !layoutStore.writable() }),
        option(t("sidebar.deleteGroup"), `custom-menu:${group.id}`, () => organizer.remove(group), { disabled: !layoutStore.writable() }),
      ]);
      const body = section(`custom-${group.id}`, group.name, [menu], group.id);
      if (!group.entries.length) body.append(element("p", { className: "session-group-empty", text: t("sidebar.dropHint") }));
      const key = `custom:${group.id}`, limit = limits.get(key) ?? 20;
      const entries = searching ? [...group.entries] : group.entries.slice(0, limit);
      const current = group.entries.find(entry => entry.kind === "project" ? entry.item.id === selected.projectId :
        sessionKey(entry.item) === selectedKey);
      if (current && !entries.includes(current)) entries.push(current);
      for (const entry of entries) {
        if (entry.kind === "project") appendProject(body, entry.item, `custom:${group.id}`, group.id);
        else { const list = element("ul", { className: "session-group-children" }); body.append(list); appendSession(list, entry.item, false, `custom:${group.id}`); }
      }
      if (entries.length < group.entries.length) {
        const more = element("button", { className: "session-show-more", text: t("nav.showMore"), attrs: {
          type: "button", "data-sidebar-focus": `more:${key}` } });
        more.addEventListener("click", () => updateTree(`more:${key}`, () => limits.set(key, limit + 20))); body.append(more);
      }
    }
    const projectBody = section("projects", workspaceView ? projectLabel : t("sidebar.view.timeline"), status === "active" ? [options, add] : [options]);
    if (workspaceView) for (const group of groups.projects) appendProject(projectBody, group);
    else {
      const sessions = matchingSessions({ sessions: state.data?.items, projects: projectsStore.get().data?.items,
        status, query, locale: currentLocale(), defaultProjectName: t("nav.defaultProject") })
        .filter(session => !(session.status === "active" && session.pinned));
      for (const group of timelineSidebar(sessions)) {
        const label = element("p", { className: "sidebar-time-heading", text: t(`sidebar.date.${group.id}`) });
        projectBody.append(label); appendSessions(projectBody, group.sessions, `date:${group.id}`, 20);
      }
    }
    if (workspaceView && !groups.projects.length) {
      const key = searching ? "nav.noMatch" : status === "trash" ? "nav.trashEmpty" :
        status === "archived" ? "nav.noArchivedSessions" : "nav.noProjects";
      projectBody.append(element("p", { className: "session-projects-empty", text: t(key) }));
    }

    if (workspaceView) {
    const createDefault = actionButton(t("nav.newInDefaultProject", {}, "在默认项目新建任务"), "plus", "new:tasks",
      () => onNewInProject("default"), "session-group-new");
    const tasksBody = section("tasks", t("nav.tasks", {}, "任务"), status === "active" ? [createDefault] : []);
    if (!groups.tasks.length) {
      const key = searching ? "nav.noMatch" : status === "trash" ? "nav.trashEmpty" :
        status === "archived" ? "nav.noArchivedSessions" : "nav.emptyGroup";
      tasksBody.append(element("p", { className: "session-group-empty", text: t(key) }));
    } else appendSessions(tasksBody, groups.tasks, "tasks", 20);
    }

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
      const expandedGroup = expandedKey ? (sidebarFocus(expandedKey) ?? sidebarFocus(`section:${expandedKey}`) ??
        (expandedKey.startsWith("custom:") ? sidebarFocus(`section:custom-${expandedKey.slice(7)}`) : null)) : null;
      const expandedBody = expandedGroup ? document.getElementById(expandedGroup.getAttribute("aria-controls")) : null;
      const expandedTasks = [...(expandedBody?.querySelectorAll(".session-item") ?? [])];
      (expandedTasks.at(-1) ?? container.querySelector('.session-item[aria-current="page"]') ??
        container.querySelector(".session-item, .sidebar-section-toggle") ?? filter).focus();
    }
    syncIndicators();
    if (retainedReadFocus && !restoredFocus)
      (readNotice.isConnected ? readRetry :
        container.querySelector('.session-item[aria-current="page"]') ?? searchButton ?? filter)
        .focus({ preventScroll: true });
  }

  organizer = createSidebarOrganizer({ container, store: layoutStore, render });
  const unsubscribeStore = store.subscribe((next) => {
    state = next;
    const key = storeContentKey(next);
    if (key !== renderedStoreKey) {
      renderedStoreKey = key;
      render();
    } else {
      container.setAttribute("aria-busy", String(coldRead(next) || layoutStore.busy()));
      syncTimes();
    }
    syncReadNotice();
  });
  const unsubscribeProjects = projectsStore.subscribe(render);
  const unsubscribeNavigation = navigation.subscribe(() => {
    organizer.cancel(); organizer.close();
    const selected = navigation.get();
    const key = `${selected.projectId}/${selected.sessionId}`;
    if (key !== previousSelection && selected.projectId) {
      const section = selected.projectId === "default" ? "tasks" : "projects";
      collapsed.delete(`section:${section}`);
      collapsed.delete(`project:${selected.projectId}`);
      if (selected.sessionId) collapsed.delete("section:pinned");
      const placement = layoutStore.get().placements.find(item => item.key === `s:${key}`)?.group ||
        layoutStore.get().placements.find(item => item.key === `p:${selected.projectId}`)?.group;
      if (placement) collapsed.delete(`section:custom-${placement}`);
      previousSelection = key;
    }
    openMenu = "";
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
    container.scrollTop = 0;
    render();
  }
  filter.addEventListener("change", onFilterChange);
  function sessionButtons() {
    return [...container.querySelectorAll(".session-item")]
      .filter((button) => button.getClientRects().length);
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
      if (index === 0) (searchButton ?? filter).focus();
      else buttons[index - 1].focus();
    }
  }
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
    focusProjects(projectId) {
      status = "active"; filter.value = status; query = "";
      openMenu = "";
      collapsed.delete("section:projects");
      if (projectId) collapsed.delete(`project:${projectId}`);
      render();
      const control = sidebarFocus(projectId ? `menu:project:${projectId}` : "section:projects") ?? sidebarFocus("section:projects");
      control?.focus();
      control?.scrollIntoView({ block: "nearest" });
    },
    showActive() { status = "active"; query = ""; filter.value = status; openMenu = ""; render(); },
    destroy() {
      layoutStore.destroy(); organizer.destroy();
      unsubscribeStore();
      unsubscribeProjects();
      unsubscribeNavigation();
      unsubscribeLocale();
      unsubscribeRuns?.();
      filter.removeEventListener("change", onFilterChange);
      openMenuNode?.remove();
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
