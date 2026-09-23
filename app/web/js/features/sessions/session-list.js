import { element, clear, formatRelativeTime, errorMessage } from "../../utils/dom.js";

export function createSessionList({ container, count, store, navigation, onSelect }) {
  let query = "";
  let state = store.get();

  function render() {
    const selected = navigation.get().sessionId;
    const items = state.data?.items ?? [];
    const needle = query.trim().toLocaleLowerCase("zh-CN");
    const visible = needle
      ? items.filter((item) => `${item.title} ${item.project_id} ${item.agent_id}`.toLocaleLowerCase("zh-CN").includes(needle))
      : items;
    count.textContent = String(items.length);
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
        element("span", { className: "session-item-meta", text: `${session.project_id} · ${session.model_id || session.agent_id}` }),
      ]);
      button.addEventListener("click", () => onSelect(session));
      container.append(button);
    }
  }

  const unsubscribeStore = store.subscribe((next) => { state = next; render(); });
  const unsubscribeNavigation = navigation.subscribe(render);

  return Object.freeze({
    setQuery(value) { query = value; render(); },
    destroy() { unsubscribeStore(); unsubscribeNavigation(); },
  });
}
