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
  isRunReviewPending = () => false, onRetry, onRemoved }) {
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

  async function postItem(projectId, sessionId, text,
    { id = newId(), first = false, priority = false, attachments = [],
      stage = false } = {}) {
    const key = sessionKey(projectId, sessionId);
    if (!key || (!text.trim() && !attachments.length)) return null;
    await load(key);
    if ((queues.get(key) ?? []).length >= 20 &&
        !(queues.get(key) ?? []).some((item) => item.id === id)) return null;
    const body = { id, text: text.trim(), attachments, first, priority };
    if (stage) body.stage = true;
    const keepalive = new TextEncoder().encode(JSON.stringify(body)).length <= 60 * 1024;
    const reconcile = async () => {
      const response = await api.get(path(key));
      update(key, response);
      if ((response.data?.items ?? []).some((item) =>
        item.id === body.id && item.text === body.text &&
        item.priority === body.priority &&
        JSON.stringify(item.attachments ?? []) === JSON.stringify(body.attachments)))
        return true;
      return Boolean(await readReceipt(key, body.id));
    };
    try {
      update(key, await api.post(path(key), body, { keepalive }));
      return id;
    } catch (error) {
      if (!uncertainPost(error)) throw error;
      try { if (await reconcile()) return id; }
      catch { /* A different page may have consumed the item. */ }
      error.queueAdmissionUncertain = true;
      error.queueItemId = id;
      throw error;
    }
  }

  async function readReceipt(key, id) {
    try {
      const receipt = (await api.get(path(key, id))).data;
      return receipt?.id === id && receipt.state === "accepted" &&
        /^run-[A-Za-z0-9_.-]+$/.test(receipt.run_id ?? "")
        ? receipt : null;
    } catch (error) {
      if (error?.status === 404) return null;
      throw error;
    }
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
    const acceptedRun = uncertain && Boolean(saved[0]?.run_id);
    const stagedHead = saved[0]?.state === "staged";
    const reviewPending = isRunReviewPending(key);
    const [projectId, sessionId] = key.split("/");
    let open = expanded.get(key) ?? true;
    const toggle = element("button", { className: "prompt-queue-toggle",
      text: t("queue.count", { count: entries.length }),
      attrs: { type: "button", "aria-expanded": String(open),
        "aria-controls": "prompt-queue-list", "data-queue-focus": "toggle" },
    });
    const waitingForRun = !uncertain && !stagedHead && isRunActive();
    const retry = waitingForRun || !saved.length || reviewPending ||
      acceptedRun ? null : element("button", {
      text: t(uncertain ? "queue.retryUncertain" : stagedHead
        ? "queue.continueStaged" : "queue.sendNext"),
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
        text: t(acceptedRun ? "queue.runAccepted" : reviewPending
          ? "queue.reviewRun" : saved.length
          ? "queue.waitForRun" : entries[0]?.rejected
            ? "queue.rejected" : "queue.awaitingAdmission") }),
    ]));
    if (uncertain) container.append(element("p", {
      className: "prompt-queue-warning",
      text: t(acceptedRun ? "queue.runAcceptedWarning" :
        "queue.uncertainWarning"),
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
              text: t(entry.rejected ? "queue.rejected" :
                "queue.awaitingAdmission") }),
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
      if (entry.state === "sending" || entry.state === "staged")
        body.append(element("span", { className: "prompt-queue-state",
          text: t(entry.state === "staged" ? "queue.staged" :
            entry.run_id ? "queue.runAccepted" :
            "queue.sendingUncertain") }));
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
    await onRemoved?.(key, removed);
  }

  navigation.subscribe(render);
  subscribeLocale(render);
  return Object.freeze({
    newId,
    async select(projectId, sessionId) {
      const key = sessionKey(projectId, sessionId);
      await load(key, true);
      render();
    },
    async enqueue(projectId, sessionId, text,
      { first = false, priority = false, attachments = [] } = {}) {
      return Boolean(await postItem(projectId, sessionId, text,
        { first, priority, attachments }));
    },
    async stage(projectId, sessionId, submission,
      { first = false } = {}) {
      return postItem(projectId, sessionId, submission.text, {
        id: submission.id, first, priority: submission.interrupt,
        attachments: submission.attachments, stage: true,
      });
    },
    find(projectId, sessionId, id) {
      return queues.get(sessionKey(projectId, sessionId))?.find((item) =>
        item.id === id) ?? null;
    },
    receipt(projectId, sessionId, id) {
      return readReceipt(sessionKey(projectId, sessionId), id);
    },
    hasStaged(projectId, sessionId) {
      return queues.get(sessionKey(projectId, sessionId))?.some((item) =>
        item.state === "staged") ?? false;
    },
    async promote(projectId, sessionId, id) {
      const key = sessionKey(projectId, sessionId);
      try {
        update(key, await api.put(path(key, id), { state: "pending" }));
        return true;
      } catch (error) {
        if (error?.code !== "queue_state_conflict" && !uncertainPost(error))
          throw error;
        try {
          await load(key, true);
          if (["pending", "sending"].includes(
            queues.get(key)?.find((item) => item.id === id)?.state)) return true;
        } catch { /* Keep the uncertain state for review. */ }
        error.queueAdmissionUncertain = true;
        error.queueItemId = id;
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
