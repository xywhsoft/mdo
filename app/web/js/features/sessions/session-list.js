import { element, clear, formatRelativeTime, errorMessage, isImeKey, toast } from "../../utils/dom.js";
import { currentLocale, subscribeLocale, t } from "../../i18n.js";
import { sessionActionItems } from "./session-actions.js";

export function createSessionList({ container, count, filter, searchInput, store, projectsStore,
  navigation, onSelect, onAction, onNewInProject, onAddProject,
  onManageProject }) {
  let query = "";
  let status = filter.value;
  let openMenu = "";
  let focusRequest = null;
  let state = store.get();
  let unread = new Set();
  let openMenuNode = null;
  let openMenuButton = null;
  let quickProjectOpen = false;
  let quickProjectPath = "";
  let quickProjectBusy = false;
  let quickProjectError = "";
  let quickProjectEpoch = 0;
  const sidebar = container.closest(".sidebar");
  const sessionKey = (session) => `${session.project_id}/${session.id}`;

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
      try { await onAction(action.name, session); }
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
    const form = element("form", { className: "project-quick-add" },
      [input, submit, cancel, error]);
    input.disabled = submit.disabled = cancel.disabled = quickProjectBusy;
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

  function render() {
    const requestedFocus = focusRequest;
    focusRequest = null;
    const focused = document.activeElement;
    const quickFocus = focused?.closest?.(".project-quick-add")
      ? focused.dataset.quickProjectFocus : "";
    const quickSelection = quickFocus === "path"
      ? [focused.selectionStart, focused.selectionEnd] : null;
    const focusedMenu = focused?.closest?.(".session-menu");
    const retainedFocus = focusedMenu?.dataset.sessionKey === openMenu
      ? { key: openMenu, index: Number(focused.dataset.menuIndex) }
      : focused?.classList?.contains("session-more")
        ? { key: focused.dataset.sessionKey, index: -1 }
        : focused?.classList?.contains("session-item")
          ? { key: focused.dataset.sessionKey, index: -2 } : null;
    openMenuNode?.remove();
    openMenuNode = null;
    openMenuButton = null;
    let restoredFocus = false;
    const selected = navigation.get();
    const items = state.data?.items ?? [];
    const locale = currentLocale();
    const projects = new Map((projectsStore.get().data?.items ?? [])
      .map((project) => [project.id, project]));
    const needle = query.trim().toLocaleLowerCase(locale);
    const defaultProjectName = t("nav.defaultProject", {}, "默认项目");
    const matchingStatus = status === "all" ? items : items.filter((item) => item.status === status);
    const visible = needle
      ? matchingStatus.filter((item) => {
        const projectName = item.project_id === "default" ? defaultProjectName :
          (projects.get(item.project_id)?.name ?? "");
        return `${item.title} ${item.project_id} ${projectName} ${item.agent_id}`
          .toLocaleLowerCase(locale).includes(needle);
      })
      : matchingStatus;
    const pinned = visible.filter((item) => item.status === "active" && item.pinned);
    const unpinned = visible.filter((item) => item.status !== "active" || !item.pinned);
    const groupIds = new Set(unpinned.map((item) => item.project_id));
    if (status === "active") {
      groupIds.add("default");
      for (const project of projectsStore.get().data?.items ?? [])
        groupIds.add(project.id);
    }
    const orderedGroups = [...groupIds].filter(Boolean).sort((a, b) =>
      a === "default" ? -1 : b === "default" ? 1 : a.localeCompare(b, locale));
    count.textContent = String(visible.length);
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
    if (state.status === "loading" && items.length === 0) {
      openMenu = "";
      container.append(element("div", { className: "empty-state",
        text: t("nav.loading", {}, "正在载入会话…") }));
      return;
    }
    if (status === "active") {
      const add = element("button", { className: "session-group-new", text: "+",
        attrs: { type: "button", "aria-label": t("nav.addProject", {}, "添加项目"),
          title: t("nav.addProject", {}, "添加项目") } });
      add.disabled = quickProjectBusy;
      add.addEventListener("click", () => {
        if (!quickProjectOpen) { quickProjectOpen = true; render(); }
        container.querySelector(".project-quick-add input")?.focus();
      });
      container.append(element("div", { className: "session-group-heading project-list-heading" }, [
        element("span", { className: "session-group-name", text: t("nav.projects", {}, "项目") }), add,
      ]));
      if (quickProjectOpen) container.append(quickProjectForm());
    }
    if (visible.length === 0 && orderedGroups.length === 0) {
      openMenu = "";
      let emptyText;
      if (needle) {
        emptyText = t("nav.noMatch", {}, "没有匹配的会话");
      } else if (status === "archived") {
        emptyText = t("nav.noArchivedSessions", {}, "暂无归档会话");
      } else if (status === "trash") {
        emptyText = t("nav.trashEmpty", {}, "回收站为空");
      } else {
        emptyText = t("nav.noSessions", {}, "还没有会话，创建一个任务开始使用。");
      }
      container.append(element("div", {
        className: "empty-state",
        text: emptyText,
      }));
      if (requestedFocus || retainedFocus) filter.focus();
      restoreQuickProjectFocus();
      return;
    }

    function appendHeading(label, size, projectId = "") {
      const newTaskLabel = projectId === "default"
        ? t("nav.newInDefaultProject", {}, "在默认项目新建任务")
        : t("nav.newInProject", { name: label }, `在 ${label} 项目新建任务`);
      const name = element("span", { className: "session-group-name", text: label });
      const count = element("span", { className: "session-group-count", text: String(size) });
      const children = projectId ? [element("button", {
        className: "session-group-main", attrs: { type: "button",
          "aria-label": newTaskLabel },
      }, [name, count])] : [name, count];
      if (projectId) {
        children[0].addEventListener("click", () => onNewInProject(projectId));
        if (projectId !== "default") {
          const manage = element("button", { className: "session-group-manage",
            text: "•••", attrs: { type: "button",
              "aria-label": t("nav.manageProject", { name: label }, `管理 ${label} 项目`),
              title: t("nav.manageProject", { name: label }, `管理 ${label} 项目`) } });
          manage.addEventListener("click", () => onManageProject(projectId));
          children.push(manage);
        }
        const create = element("button", { className: "session-group-new", text: "+",
          attrs: { type: "button", "aria-label": newTaskLabel,
            title: newTaskLabel } });
        create.addEventListener("click", () => onNewInProject(projectId));
        children.push(create);
      }
      container.append(element("div", { className: "session-group-heading" }, children));
    }

    function appendSession(session, showProject = false) {
      const key = sessionKey(session);
      const hasUnread = unread.has(`${session.project_id}/${session.id}`);
      const button = element("button", {
        className: "session-item",
        attrs: {
          type: "button",
          "data-session-key": key,
          "aria-current": key === `${selected.projectId}/${selected.sessionId}` ? "page" : null,
          title: session.title || session.id,
        },
      }, [
        element("span", { className: "session-item-title", text: session.title || t("nav.untitled", {}, "未命名任务") }),
        element("time", { className: "session-item-time", text: formatRelativeTime(session.updated_at),
          attrs: { "data-relative-time": session.updated_at } }),
        element("span", { className: "session-item-meta", text: `${showProject ? `${session.project_id} · ` : ""}${session.model_id || session.agent_id}${hasUnread ? ` · ${t("nav.unread", {}, "有新结果")}` : ""}` }),
      ]);
      button.addEventListener("click", (event) => onSelect(session, event));
      const title = session.title || t("nav.untitled", {}, "未命名任务");
      const more = element("button", { className: "session-more", text: "•••", attrs: { type: "button", "data-session-key": key, "aria-label": t("nav.actionsFor", { title }, `${title} 的操作`), "aria-haspopup": "menu", "aria-expanded": String(openMenu === key) } });
      more.addEventListener("click", (event) => {
        event.stopPropagation();
        openMenu = openMenu === key ? "" : key;
        focusRequest = { key,
          index: openMenu && event.detail === 0 ? 0 : -1 };
        render();
      });
      const menu = openMenu === key ? element("div", {
        className: "session-menu session-list-menu", attrs: {
          id: "sidebar-session-menu", role: "menu", "data-session-key": key,
        },
      }, menuFor(session)) : null;
      if (menu) {
        [...menu.children].forEach((item, index) => {
          item.dataset.menuIndex = String(index);
        });
        more.setAttribute("aria-controls", menu.id);
      }
      container.append(element("div", { className: "session-item-row", attrs: {
        "data-status": session.status, "data-unread": hasUnread ? "true" : null,
      } }, [button, more]));
      if (menu) {
        document.body.append(menu);
        openMenuNode = menu;
        openMenuButton = more;
        positionMenu();
      }
      const target = requestedFocus?.key === key
        ? requestedFocus : retainedFocus?.key === key
          ? retainedFocus : null;
      if (target) {
        if (target.index === -2) button.focus();
        else if (target.index < 0) more.focus();
        else menu?.children[target.index]?.focus();
        restoredFocus = true;
      }
    }
    if (pinned.length) {
      appendHeading(t("nav.pinned", {}, "置顶"), pinned.length);
      for (const session of pinned) appendSession(session, true);
    }
    for (const projectId of orderedGroups) {
      const sessions = unpinned.filter((item) => item.project_id === projectId);
      appendHeading(projectId === "default" ? defaultProjectName :
        projects.get(projectId)?.name || projectId,
        sessions.length, projectId);
      if (!sessions.length) container.append(element("div", {
        className: "session-group-empty", text: needle
          ? t("nav.noMatch", {}, "没有匹配的会话")
          : t("nav.emptyGroup", {}, "暂无会话"),
      }));
      for (const session of sessions) appendSession(session);
    }
    if (openMenu && !openMenuNode) openMenu = "";
    if (!restoredFocus && (requestedFocus || retainedFocus))
      (container.querySelector(".session-item, .session-group-new") ?? filter).focus();
    restoreQuickProjectFocus();
  }

  const unsubscribeStore = store.subscribe((next) => { state = next; render(); });
  const unsubscribeProjects = projectsStore.subscribe(render);
  const unsubscribeNavigation = navigation.subscribe(() => {
    openMenu = "";
    quickProjectEpoch += 1;
    quickProjectOpen = false;
    quickProjectPath = "";
    quickProjectError = "";
    render();
  });
  const unsubscribeLocale = subscribeLocale(render);
  filter.addEventListener("change", () => {
    status = filter.value;
    openMenu = "";
    quickProjectEpoch += 1;
    quickProjectOpen = false;
    quickProjectPath = "";
    quickProjectError = "";
    render();
  });
  function sessionButtons() {
    return [...container.querySelectorAll(".session-item")];
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
    const items = [...menu.querySelectorAll('[role="menuitem"]')];
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
    setQuery(value) { query = value; render(); },
    setUnread(keys) { unread = keys; render(); },
    showActive() { status = "active"; query = ""; filter.value = status; openMenu = ""; render(); },
    destroy() {
      unsubscribeStore();
      unsubscribeProjects();
      unsubscribeNavigation();
      unsubscribeLocale();
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
