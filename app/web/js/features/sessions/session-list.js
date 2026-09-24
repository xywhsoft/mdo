import { element, clear, formatRelativeTime, errorMessage, toast } from "../../utils/dom.js";

export function createSessionList({ container, count, filter, store, navigation, onSelect, onAction }) {
  let query = "";
  let status = filter.value;
  let openMenu = "";
  let focusRequest = null;
  let state = store.get();
  let unread = new Set();

  function menuAction(label, name, session, tone = "neutral") {
    const button = element("button", { text: label, attrs: { type: "button", role: "menuitem", "data-tone": tone } });
    button.addEventListener("click", async (event) => {
      event.stopPropagation();
      focusRequest = { sessionId: session.id, index: -1 };
      openMenu = "";
      render();
      try { await onAction(name, session); }
      catch (error) { toast(errorMessage(error), "error"); }
    });
    return button;
  }

  function menuFor(session) {
    if (session.status === "trash") return [menuAction("恢复", "restore", session)];
    const actions = [menuAction("重命名", "rename", session)];
    if (session.status === "active") {
      actions.push(menuAction(session.pinned ? "取消置顶" : "置顶", "pin", session));
      actions.push(menuAction("归档", "archive", session));
      actions.push(menuAction("创建分支", "fork", session));
      actions.push(menuAction("截断历史", "truncate", session, "danger"));
      actions.push(menuAction("清空历史", "clear", session, "danger"));
      actions.push(menuAction("导出", "export", session));
    } else actions.push(menuAction("移回进行中", "unarchive", session));
    actions.push(menuAction("移到回收站", "trash", session, "danger"));
    return actions;
  }

  function render() {
    const requestedFocus = focusRequest;
    focusRequest = null;
    const focused = document.activeElement;
    const focusedMenu = focused?.closest?.(".session-menu");
    const retainedFocus = focusedMenu?.dataset.sessionId === openMenu
      ? { sessionId: openMenu, index: Number(focused.dataset.menuIndex) }
      : focused?.classList?.contains("session-more")
        ? { sessionId: focused.dataset.sessionId, index: -1 }
        : focused?.classList?.contains("session-item")
          ? { sessionId: focused.dataset.sessionId, index: -2 } : null;
    let restoredFocus = false;
    const selected = navigation.get().sessionId;
    const items = state.data?.items ?? [];
    const needle = query.trim().toLocaleLowerCase("zh-CN");
    const matchingStatus = status === "all" ? items : items.filter((item) => item.status === status);
    const visible = needle
      ? matchingStatus.filter((item) => `${item.title} ${item.project_id} ${item.agent_id}`.toLocaleLowerCase("zh-CN").includes(needle))
      : matchingStatus;
    count.textContent = String(visible.length);
    container.setAttribute("aria-busy", String(state.status === "loading"));
    clear(container);

    if (state.status === "error") {
      container.append(element("div", { className: "resource-error", text: errorMessage(state.error) }));
      return;
    }
    if (state.status === "loading" && items.length === 0) {
      container.append(element("div", { className: "empty-state", text: "正在载入会话…" }));
      return;
    }
    if (visible.length === 0) {
      container.append(element("div", {
        className: "empty-state",
        text: needle ? "没有匹配的会话" : "还没有会话，创建一个任务开始使用。",
      }));
      if (requestedFocus || retainedFocus) filter.focus();
      return;
    }

    for (const session of visible) {
      const hasUnread = unread.has(`${session.project_id}/${session.id}`);
      const button = element("button", {
        className: "session-item",
        attrs: {
          type: "button",
          "data-session-id": session.id,
          "aria-current": session.id === selected ? "page" : null,
          title: session.title || session.id,
        },
      }, [
        element("span", { className: "session-item-title", text: session.title || "未命名任务" }),
        element("time", { className: "session-item-time", text: formatRelativeTime(session.updated_at) }),
        element("span", { className: "session-item-meta", text: `${session.project_id} · ${session.model_id || session.agent_id}${session.pinned ? " · 已置顶" : ""}${hasUnread ? " · 有新结果" : ""}` }),
      ]);
      button.addEventListener("click", () => onSelect(session));
      const more = element("button", { className: "session-more", text: "•••", attrs: { type: "button", "data-session-id": session.id, "aria-label": `${session.title || "未命名任务"} 的操作`, "aria-haspopup": "menu", "aria-expanded": String(openMenu === session.id) } });
      more.addEventListener("click", (event) => {
        event.stopPropagation();
        openMenu = openMenu === session.id ? "" : session.id;
        focusRequest = { sessionId: session.id,
          index: openMenu && event.detail === 0 ? 0 : -1 };
        render();
      });
      const menu = element("div", { className: "session-menu", attrs: {
        role: "menu", "data-session-id": session.id,
      } }, menuFor(session));
      [...menu.children].forEach((item, index) => {
        item.dataset.menuIndex = String(index);
      });
      menu.hidden = openMenu !== session.id;
      container.append(element("div", { className: "session-item-row", attrs: {
        "data-status": session.status, "data-unread": hasUnread ? "true" : null,
      } }, [button, more, menu]));
      const target = requestedFocus?.sessionId === session.id
        ? requestedFocus : retainedFocus?.sessionId === session.id
          ? retainedFocus : null;
      if (target) {
        if (target.index === -2) button.focus();
        else if (target.index < 0) more.focus();
        else menu.children[target.index]?.focus();
        restoredFocus = true;
      }
    }
    if (!restoredFocus && (requestedFocus || retainedFocus))
      container.querySelector(".session-item")?.focus();
  }

  const unsubscribeStore = store.subscribe((next) => { state = next; render(); });
  const unsubscribeNavigation = navigation.subscribe(() => { openMenu = ""; render(); });
  filter.addEventListener("change", () => { status = filter.value; openMenu = ""; render(); });
  document.addEventListener("pointerdown", onOutsidePointerDown);
  document.addEventListener("focusin", onFocusIn);
  document.addEventListener("keydown", onDismissKeyDown);

  function currentMenu() {
    return [...container.querySelectorAll(".session-menu")].find((menu) =>
      !menu.hidden && menu.dataset.sessionId === openMenu) ?? null;
  }

  function closeMenu(restoreFocus = false) {
    const menu = currentMenu();
    const more = menu?.previousElementSibling;
    openMenu = "";
    if (menu) menu.hidden = true;
    if (more) {
      more.setAttribute("aria-expanded", "false");
      if (restoreFocus) more.focus();
    }
  }

  function onOutsidePointerDown(event) {
    const menu = currentMenu();
    if (menu && !menu.parentElement.contains(event.target)) closeMenu();
  }

  function onFocusIn(event) {
    const menu = currentMenu();
    if (menu && !menu.parentElement.contains(event.target)) closeMenu();
  }

  function onDismissKeyDown(event) {
    const menu = currentMenu();
    if (!menu) return;
    if (event.key === "Escape") {
      event.preventDefault();
      event.stopImmediatePropagation();
      closeMenu(true);
      return;
    }
    if (!menu.contains(event.target) && event.target !== menu.previousElementSibling)
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
    destroy() {
      unsubscribeStore();
      unsubscribeNavigation();
      document.removeEventListener("pointerdown", onOutsidePointerDown);
      document.removeEventListener("focusin", onFocusIn);
      document.removeEventListener("keydown", onDismissKeyDown);
    },
  });
}
