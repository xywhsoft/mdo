import { api, attachmentUrl, resourceId } from "../../api/client.js";
import { subscribeLocale, t } from "../../i18n.js";
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

function uncertainPost(error) {
  return ["network_error", "invalid_response", "queue_unavailable"]
    .includes(error?.code);
}

export function createPromptQueue({ container, navigation, isRunActive, stagedEntries,
  onRetry, onRemoved }) {
  const queues = new Map();
  const loads = new Map();
  const versions = new Map();
  const expanded = new Map();
  const busy = new Set();
  const actionBusy = new Set();

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
    const saved = queues.get(key) ?? [];
    const staged = stagedEntries?.() ?? [];
    const entries = [...saved, ...staged];
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
    const uncertain = saved[0]?.state === "sending";
    const [projectId, sessionId] = key.split("/");
    let open = expanded.get(key) ?? true;
    const toggle = element("button", { className: "prompt-queue-toggle",
      text: t("queue.count", { count: entries.length }),
      attrs: { type: "button", "aria-expanded": String(open),
        "aria-controls": "prompt-queue-list", "data-queue-focus": "toggle" },
    });
    const waitingForRun = !uncertain && isRunActive();
    const retry = waitingForRun || !saved.length ? null : element("button", {
      text: t(uncertain ? "queue.retryUncertain" : "queue.sendNext"),
      attrs: { type: "button", "data-queue-focus": "retry" },
    });
    if (retry) {
      retry.disabled = busy.has(key) || actionBusy.has(key);
      retry.addEventListener("click", async () => {
        if (busy.has(key) || actionBusy.has(key)) return;
        actionBusy.add(key);
        render();
        try { await onRetry(); }
        catch (error) { toast(errorMessage(error), "error"); }
        finally { actionBusy.delete(key); render(); }
      });
    }
    container.append(element("div", { className: "prompt-queue-header" }, [
      toggle, retry ?? element("span", { className: "prompt-queue-waiting",
        text: t(saved.length ? "queue.waitForRun" : "queue.awaitingAdmission") }),
    ]));
    if (uncertain) container.append(element("p", {
      className: "prompt-queue-warning",
      text: t("queue.uncertainWarning"),
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
      if (entry.staged) {
        list.append(element("li", {}, [
          element("span", { className: "prompt-queue-index",
            text: String(index + 1) }),
          element("div", { className: "prompt-queue-item-body" }, [
            element("span", { className: "prompt-queue-text",
              text: entry.text || t("queue.imageMessage") }),
            element("span", { className: "prompt-queue-state",
              text: t("queue.awaitingAdmission") }),
          ]),
        ]));
        continue;
      }
      const remove = element("button", {
        text: t("queue.remove"),
        attrs: { type: "button", "aria-label": t("queue.removeNumber", { number: index + 1 }),
          "data-queue-focus": entry.id, "data-queue-index": String(index) },
      });
      remove.disabled = busy.has(key) || actionBusy.has(key);
      remove.addEventListener("click", async () => {
        if (busy.has(key) || actionBusy.has(key)) return;
        actionBusy.add(key);
        render();
        try { await removeItem(entry.id); }
        catch (error) { toast(errorMessage(error), "error"); }
        finally { actionBusy.delete(key); render(); }
      });
      const body = element("div", { className: "prompt-queue-item-body" }, [
        element("span", { className: "prompt-queue-text",
          text: entry.text || t("queue.imageMessage") }),
      ]);
      if (entry.state === "sending") body.append(element("span", {
        className: "prompt-queue-state", text: t("queue.sendingUncertain"),
      }));
      if (entry.priority) body.append(element("span", {
        className: "prompt-queue-state", text: t("queue.priority"),
      }));
      if (entry.attachments?.length) {
        const images = element("div", { className: "prompt-queue-images" });
        for (const [imageIndex, id] of entry.attachments.entries()) {
          if (typeof id !== "string" || !/^[0-9a-f]{32}$/.test(id)) continue;
          images.append(element("button", { attrs: {
            type: "button", "aria-label": t("queue.viewImage", { number: imageIndex + 1 }),
            "data-image-preview": "",
            "data-image-ref": `queue:${key}/${entry.id}/${id}/${imageIndex}`,
          } }, [element("img", { attrs: {
            src: attachmentUrl(projectId, sessionId, id),
            alt: t("queue.imageAlt", { number: imageIndex + 1 }), loading: "lazy",
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
        focusKey === "retry" ? retry ?? toggle :
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
  subscribeLocale(render);
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
      const body = { id: newId(), text: text.trim(), attachments, first, priority };
      const keepalive = new TextEncoder().encode(JSON.stringify(body)).length <= 60 * 1024;
      const submit = async () => {
        update(key, await api.post(path(key), body, { keepalive }));
        return true;
      };
      const reconcile = async () => {
        const response = await api.get(path(key));
        update(key, response);
        return (response.data?.items ?? []).some((item) =>
          item.id === body.id && item.text === body.text &&
          item.priority === body.priority &&
          JSON.stringify(item.attachments ?? []) === JSON.stringify(body.attachments));
      };
      try { return await submit(); }
      catch (error) {
        if (!uncertainPost(error)) throw error;
        try { if (await reconcile()) return true; }
        catch { /* The queue may have accepted and already consumed the item. */ }
        // A missing item is also inconclusive: dispatch removes it from the
        // queue, so resubmitting even the same ID could execute it twice.
        error.queueAdmissionUncertain = true;
        throw error;
      }
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
    async exclusive(projectId, sessionId, callback) {
      const key = sessionKey(projectId, sessionId);
      if (!key || busy.has(key)) return false;
      busy.add(key);
      render();
      try { await callback(); return true; }
      finally { busy.delete(key); render(); }
    },
    render,
  });
}
