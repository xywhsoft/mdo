import { api, attachmentUrl, resourceId } from "../../api/client.js";
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
  const expanded = new Map();
  let busy = false;

  function selectedKey() {
    const { projectId, sessionId } = navigation.get();
    return sessionKey(projectId, sessionId);
  }

  function update(key, response) {
    versions.set(key, (versions.get(key) ?? 0) + 1);
    const items = response.data?.items ?? [];
    if (!(queues.get(key)?.length) && items.length) expanded.set(key, true);
    queues.set(key, items);
    render();
  }

  async function discardUnusedImages(key, attachments = []) {
    const [projectId, sessionId] = key.split("/");
    await Promise.allSettled(attachments.map((id) =>
      api.deleteImage(projectId, sessionId, id)));
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
    const entries = queues.get(key) ?? [];
    const focused = container.dataset.queueKey === key &&
      container.contains(document.activeElement) ? document.activeElement : null;
    const focusKey = focused?.dataset.queueFocus;
    const imageRef = focused?.dataset.imageRef;
    const focusIndex = Number(focused?.dataset.queueIndex ?? 0);
    clear(container);
    container.dataset.queueKey = key;
    container.hidden = entries.length === 0;
    if (!entries.length) {
      if (focused) document.querySelector("#prompt")?.focus({ preventScroll: true });
      return;
    }
    const uncertain = entries[0].state === "sending";
    const [projectId, sessionId] = key.split("/");
    let open = expanded.get(key) ?? true;
    const toggle = element("button", { className: "prompt-queue-toggle",
      text: `待发送 · ${entries.length}`,
      attrs: { type: "button", "aria-expanded": String(open),
        "aria-controls": "prompt-queue-list", "data-queue-focus": "toggle" },
    });
    const retry = element("button", {
      text: uncertain ? "确认未发送后重试" : "发送下一条",
      attrs: { type: "button", "data-queue-focus": "retry" },
    });
    retry.disabled = busy;
    retry.addEventListener("click", async () => {
      retry.disabled = true;
      try { await onRetry(); }
      catch (error) { toast(errorMessage(error), "error"); }
      finally { retry.disabled = false; }
    });
    container.append(element("div", { className: "prompt-queue-header" }, [
      toggle, retry,
    ]));
    if (uncertain) container.append(element("p", {
      className: "prompt-queue-warning",
      text: "首条消息可能已被服务端接收。请先核对对话和运行记录，再决定移除或重试。",
    }));
    const list = element("ol", { className: "prompt-queue-list",
      attrs: { id: "prompt-queue-list" } });
    list.hidden = !open;
    toggle.addEventListener("click", () => {
      open = !open;
      expanded.set(key, open);
      toggle.setAttribute("aria-expanded", String(open));
      list.hidden = !open;
    });
    for (const [index, entry] of entries.entries()) {
      const remove = element("button", {
        text: "移除",
        attrs: { type: "button", "aria-label": `移除待发送消息 ${index + 1}`,
          "data-queue-focus": entry.id, "data-queue-index": String(index) },
      });
      remove.disabled = busy;
      remove.addEventListener("click", async () => {
        remove.disabled = true;
        try { await removeItem(entry.id); }
        catch (error) { toast(errorMessage(error), "error"); remove.disabled = false; }
      });
      const body = element("div", { className: "prompt-queue-item-body" }, [
        element("span", { className: "prompt-queue-text",
          text: entry.text || "图片消息" }),
      ]);
      if (entry.state === "sending") body.append(element("span", {
        className: "prompt-queue-state", text: "发送状态待确认",
      }));
      if (entry.priority) body.append(element("span", {
        className: "prompt-queue-state", text: "中断后优先发送",
      }));
      if (entry.attachments?.length) {
        const images = element("div", { className: "prompt-queue-images" });
        for (const [imageIndex, id] of entry.attachments.entries()) {
          if (typeof id !== "string" || !/^[0-9a-f]{32}$/.test(id)) continue;
          images.append(element("button", { attrs: {
            type: "button", "aria-label": `查看待发送图片 ${imageIndex + 1}`,
            "data-image-preview": "",
            "data-image-ref": `queue:${key}/${entry.id}/${id}/${imageIndex}`,
          } }, [element("img", { attrs: {
            src: attachmentUrl(projectId, sessionId, id),
            alt: `待发送图片 ${imageIndex + 1}`, loading: "lazy",
          } })]));
        }
        body.append(images);
      }
      list.append(element("li", {}, [
        element("span", { className: "prompt-queue-index",
          text: String(index + 1) }),
        body,
        remove,
      ]));
    }
    container.append(list);
    if (focused) {
      const removes = [...list.querySelectorAll("button[data-queue-index]")];
      const target = focusKey === "toggle" ? toggle :
        focusKey === "retry" ? retry :
        imageRef ? [...list.querySelectorAll("button[data-image-ref]")]
          .find((item) => item.dataset.imageRef === imageRef) :
        removes.find((item) => item.dataset.queueFocus === focusKey) ??
          removes[Math.min(focusIndex, removes.length - 1)];
      (target?.disabled ? toggle : target ?? toggle)
        .focus({ preventScroll: true });
    }
  }

  async function removeItem(id) {
    const key = selectedKey();
    if (!key) return;
    const removed = queues.get(key)?.find((entry) => entry.id === id);
    update(key, await api.delete(path(key, id)));
    void discardUnusedImages(key, removed?.attachments);
    await onRemoved?.();
  }

  navigation.subscribe(render);
  return Object.freeze({
    async select(projectId, sessionId) {
      const key = sessionKey(projectId, sessionId);
      await load(key, true);
      render();
    },
    async enqueue(projectId, sessionId, text,
      { first = false, priority = false, attachments = [] } = {}) {
      const key = sessionKey(projectId, sessionId);
      if (!key || (!text.trim() && !attachments.length)) return false;
      await load(key);
      if ((queues.get(key) ?? []).length >= 20) return false;
      const response = await api.post(path(key),
        { id: newId(), text: text.trim(), attachments, first, priority });
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
      const removed = queues.get(key)?.find((entry) => entry.id === id);
      update(key, await api.delete(path(key, id)));
      void discardUnusedImages(key, removed?.attachments);
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
