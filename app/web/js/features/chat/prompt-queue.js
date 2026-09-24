import { clear, element } from "../../utils/dom.js";

function sessionKey(projectId, sessionId) {
  return projectId && sessionId ? `${projectId}/${sessionId}` : "";
}

export function createPromptQueue({ container, navigation, onRetry }) {
  const queues = new Map();
  let nextId = 1;
  let busy = false;

  function selectedKey() {
    const { projectId, sessionId } = navigation.get();
    return sessionKey(projectId, sessionId);
  }

  function render() {
    const entries = queues.get(selectedKey()) ?? [];
    clear(container);
    container.hidden = entries.length === 0;
    if (!entries.length) return;
    const retry = element("button", { text: "发送下一条", attrs: { type: "button" } });
    retry.addEventListener("click", onRetry);
    container.append(element("div", { className: "prompt-queue-header" }, [
      element("h3", { text: `待发送 · ${entries.length}` }), retry,
    ]));
    const list = element("ol", { className: "prompt-queue-list" });
    for (const entry of entries) {
      const remove = element("button", { text: "移除", attrs: { type: "button", "aria-label": "移除待发送消息" } });
      remove.addEventListener("click", () => {
        const index = entries.findIndex((item) => item.id === entry.id);
        if (index >= 0) entries.splice(index, 1);
        render();
      });
      list.append(element("li", {}, [
        element("span", { text: entry.text }), remove,
      ]));
    }
    container.append(list);
  }

  navigation.subscribe(render);
  return Object.freeze({
    enqueue(projectId, sessionId, text, { first = false } = {}) {
      const key = sessionKey(projectId, sessionId);
      if (!key || !text.trim()) return false;
      const entries = queues.get(key) ?? [];
      if (entries.length >= 20) return false;
      const entry = { id: nextId++, text: text.trim() };
      if (first) entries.unshift(entry);
      else entries.push(entry);
      queues.set(key, entries);
      render();
      return true;
    },
    peek(projectId, sessionId) {
      return queues.get(sessionKey(projectId, sessionId))?.[0] ?? null;
    },
    remove(projectId, sessionId, id) {
      const entries = queues.get(sessionKey(projectId, sessionId));
      const index = entries?.findIndex((entry) => entry.id === id) ?? -1;
      if (index >= 0) entries.splice(index, 1);
      render();
    },
    async exclusive(callback) {
      if (busy) return false;
      busy = true;
      try { await callback(); return true; }
      finally { busy = false; }
    },
    render,
  });
}
