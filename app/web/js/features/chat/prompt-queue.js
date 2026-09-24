import { api, resourceId } from "../../api/client.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";

function sessionKey(projectId, sessionId) {
  return projectId && sessionId
    ? `${resourceId(projectId, "project")}/${resourceId(sessionId, "session")}` : "";
}

function path(key, id = "") {
  const [projectId, sessionId] = key.split("/");
  return `/projects/${projectId}/sessions/${sessionId}/queue${id ? `/${id}` : ""}`;
}

function newId() {
  return Array.from(crypto.getRandomValues(new Uint8Array(16)),
    (byte) => byte.toString(16).padStart(2, "0")).join("");
}

export function createPromptQueue({ container, navigation, onRetry, onRemoved }) {
  const queues = new Map();
  const loads = new Map();
  const versions = new Map();
  let busy = false;

  function selectedKey() {
    const { projectId, sessionId } = navigation.get();
    return sessionKey(projectId, sessionId);
  }

  function update(key, response) {
    versions.set(key, (versions.get(key) ?? 0) + 1);
    queues.set(key, response.data?.items ?? []);
    render();
  }

  async function load(key, force = false) {
    if (!key) return;
    if (!force && queues.has(key)) return;
    if (loads.has(key)) return loads.get(key);
    const version = versions.get(key) ?? 0;
    const request = (async () => {
      const response = await api.get(path(key));
      if ((versions.get(key) ?? 0) === version) update(key, response);
    })();
    loads.set(key, request);
    try { await request; }
    finally { loads.delete(key); }
  }

  function render() {
    const key = selectedKey();
    const entries = loads.has(key) ? [] : (queues.get(key) ?? []);
    clear(container);
    container.hidden = entries.length === 0;
    if (!entries.length) return;
    const uncertain = entries[0].state === "sending";
    const retry = element("button", {
      text: uncertain ? "确认未发送后重试" : "发送下一条",
      attrs: { type: "button" },
    });
    retry.disabled = busy;
    retry.addEventListener("click", async () => {
      retry.disabled = true;
      try { await onRetry(); }
      catch (error) { toast(errorMessage(error), "error"); }
      finally { retry.disabled = false; }
    });
    container.append(element("div", { className: "prompt-queue-header" }, [
      element("h3", { text: `待发送 · ${entries.length}` }), retry,
    ]));
    if (uncertain) container.append(element("p", {
      className: "prompt-queue-warning",
      text: "首条消息可能已被服务端接收。请先核对对话和运行记录，再决定移除或重试。",
    }));
    const list = element("ol", { className: "prompt-queue-list" });
    for (const entry of entries) {
      const remove = element("button", {
        text: "移除",
        attrs: { type: "button", "aria-label": "移除待发送消息" },
      });
      remove.disabled = busy;
      remove.addEventListener("click", async () => {
        remove.disabled = true;
        try { await removeItem(entry.id); }
        catch (error) { toast(errorMessage(error), "error"); remove.disabled = false; }
      });
      list.append(element("li", {}, [
        element("span", { text: entry.text || "图片消息" }),
        entry.attachments?.length ? element("span", {
          className: "prompt-queue-images",
          text: `${entry.attachments.length} 张图片`,
        }) : null,
        entry.state === "sending" ? element("span", {
          className: "prompt-queue-state", text: "发送状态待确认",
        }) : null,
        remove,
      ].filter(Boolean)));
    }
    container.append(list);
  }

  async function removeItem(id) {
    const key = selectedKey();
    if (!key) return;
    update(key, await api.delete(path(key, id)));
    await onRemoved?.();
  }

  navigation.subscribe(render);
  return Object.freeze({
    async select(projectId, sessionId) {
      const key = sessionKey(projectId, sessionId);
      await load(key, true);
      render();
    },
    async enqueue(projectId, sessionId, text, { first = false, attachments = [] } = {}) {
      const key = sessionKey(projectId, sessionId);
      if (!key || (!text.trim() && !attachments.length)) return false;
      await load(key);
      if ((queues.get(key) ?? []).length >= 20) return false;
      const response = await api.post(path(key),
        { id: newId(), text: text.trim(), attachments, first });
      update(key, response);
      return true;
    },
    peek(projectId, sessionId) {
      const key = sessionKey(projectId, sessionId);
      return loads.has(key) ? null : (queues.get(key)?.[0] ?? null);
    },
    async markSending(projectId, sessionId, id) {
      const key = sessionKey(projectId, sessionId);
      update(key, await api.put(path(key, id), { state: "sending" }));
    },
    async retry(projectId, sessionId, id) {
      const key = sessionKey(projectId, sessionId);
      update(key, await api.put(path(key, id), { state: "pending" }));
    },
    async remove(projectId, sessionId, id) {
      const key = sessionKey(projectId, sessionId);
      update(key, await api.delete(path(key, id)));
    },
    async exclusive(callback) {
      if (busy) return false;
      busy = true;
      render();
      try { await callback(); return true; }
      finally { busy = false; render(); }
    },
    render,
  });
}
