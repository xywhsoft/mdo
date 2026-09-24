import { element, clear, formatRelativeTime, errorMessage, toast } from "../../utils/dom.js";

export function createSessionList({ container, count, filter, store, navigation, onSelect, onAction }) {
  let query = "";
  let status = filter.value;
  let openMenu = "";
  let focusMenuButton = "";
  let state = store.get();
  let unread = new Set();

  function menuAction(label, name, session, tone = "neutral") {
    const button = element("button", { text: label, attrs: { type: "button", role: "menuitem", "data-tone": tone } });
    button.addEventListener("click", async (event) => {
      event.stopPropagation();
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
    const focusTarget = focusMenuButton;
    focusMenuButton = "";
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
      return;
    }

    for (const session of visible) {
      const hasUnread = unread.has(`${session.project_id}/${session.id}`);
      const button = element("button", {
        className: "session-item",
        attrs: {
          type: "button",
          "aria-current": session.id === selected ? "page" : null,
          title: session.title || session.id,
        },
      }, [
        element("span", { className: "session-item-title", text: session.title || "未命名任务" }),
        element("time", { className: "session-item-time", text: formatRelativeTime(session.updated_at) }),
        element("span", { className: "session-item-meta", text: `${session.project_id} · ${session.model_id || session.agent_id}${session.pinned ? " · 已置顶" : ""}${hasUnread ? " · 有新结果" : ""}` }),
      ]);
      button.addEventListener("click", () => onSelect(session));
      const more = element("button", { className: "session-more", text: "•••", attrs: { type: "button", "aria-label": `${session.title || "未命名任务"} 的操作`, "aria-haspopup": "menu", "aria-expanded": String(openMenu === session.id) } });
      more.addEventListener("click", (event) => {
        event.stopPropagation();
        openMenu = openMenu === session.id ? "" : session.id;
        focusMenuButton = session.id;
        render();
      });
      const menu = element("div", { className: "session-menu", attrs: { role: "menu" } }, menuFor(session));
      menu.hidden = openMenu !== session.id;
      container.append(element("div", { className: "session-item-row", attrs: {
        "data-status": session.status, "data-unread": hasUnread ? "true" : null,
      } }, [button, more, menu]));
      if (focusTarget === session.id) {
        more.focus();
      }
    }
  }

  const unsubscribeStore = store.subscribe((next) => { state = next; render(); });
  const unsubscribeNavigation = navigation.subscribe(() => { openMenu = ""; render(); });
  filter.addEventListener("change", () => { status = filter.value; openMenu = ""; render(); });
  document.addEventListener("pointerdown", onOutsidePointerDown);
  document.addEventListener("keydown", onDismissKeyDown);

  function onOutsidePointerDown(event) {
    if (openMenu && !container.contains(event.target)) {
      openMenu = "";
      render();
    }
  }

  function onDismissKeyDown(event) {
    if (openMenu && event.key === "Escape") {
      event.preventDefault();
      focusMenuButton = openMenu;
      openMenu = "";
      render();
    }
  }

  return Object.freeze({
    setQuery(value) { query = value; render(); },
    setUnread(keys) { unread = keys; render(); },
    destroy() {
      unsubscribeStore();
      unsubscribeNavigation();
      document.removeEventListener("pointerdown", onOutsidePointerDown);
      document.removeEventListener("keydown", onDismissKeyDown);
    },
  });
}
