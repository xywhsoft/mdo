import { setUiAttribute as writeUiAttribute, setUiText as writeUiText, uiMessage, t as uiText, subscribeLocale, t } from "../../i18n.js";

import { api, resourceId } from "../../api/client.js";
import { createPromptQueueTransport, queueResponse } from "./prompt-queue-transport.js";

import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { createUnusedImageCleanup } from "./unused-image-cleanup.js";
import { findModel } from "../../utils/models.js";
import { createQueueReorder } from "./queue-reorder.js";
import { promptContextSummary, promptContextText } from "./prompt-context.js";

function sessionKey(projectId, sessionId) {
  return projectId && sessionId
    ? `${resourceId(projectId, "project")}/${resourceId(sessionId, "session")}` : "";
}

function newId() {
  return Array.from(crypto.getRandomValues(new Uint8Array(16)),
    (byte) => byte.toString(16).padStart(2, "0")).join("");
}

export function createPromptQueue({ container, navigation, isRunActive, stagedEntries,
  createRecovery, stamp,
  isRunFinishing = () => false, onUpdated = () => {},
  isMessagePresented = () => false,
  isProfileBusy = () => false,
  isRunReviewPending = () => false, isSessionWritable = () => true,
  isSessionRunActive = () => false, modelsStore,
  onRetry, onRemoved, onWithdraw, onChanged = () => {} }) {
  const queues = new Map();
  const loads = new Map();
  const versions = new Map();
  const expanded = new Map();
  const busy = new Set();
  const actionBusy = new Set();
  const metadata = new Map();
  const transport = createPromptQueueTransport({ createRecovery, stamp });
  const unusedImages = createUnusedImageCleanup({
    deleteImage: api.deleteImage, isRunActive: isSessionRunActive,
  });
  const reorder = createQueueReorder({ container,
    snapshot: () => { const key = selectedKey(); return { key,
      origin: transport.stamp(),
      revision: metadata.get(key)?.revision ?? 0,
      ids: (queues.get(key) ?? []).filter(item => canChange(item) && !isMessagePresented(item.id)).map(item => item.id) }; },
    onMove: (id, before, owner) => runAction(owner.key,
      () => performAction(owner.key, "move", id, before, 0, owner.revision), owner.origin),
    onEnd: () => render(),
  });

  function canChange(item) {
    return ["pending", "cancelled"].includes(item.state) && (!item.priority || metadata.get(selectedKey())?.paused) &&
      !item.start_claimed && !item.run_id;
  }

  async function performAction(key, kind, item = "", before = "", draftRevision = 0,
    revision = metadata.get(key)?.revision ?? 0) {
    const body = { id: newId(), kind, item, before, revision, draft_revision: draftRevision };
    update(key, await transport.action(key, body));
    await onChanged(key);
  }

  async function runAction(key, operation, origin = transport.stamp()) {
    if (transport.stamp() !== origin || !isSessionWritable() || actionBusy.has(key) || busy.has(key)) return;
    actionBusy.add(key); render();
    try { await operation(); }
    catch (error) {
      toast(errorMessage(error), "error");
      try { await load(key, true); } catch { /* Retain the original error and saved messages. */ }
    } finally { actionBusy.delete(key); render(); }
  }

  function selectedKey() {
    const { projectId, sessionId } = navigation.get();
    return sessionKey(projectId, sessionId);
  }

  function profileLabel(profile) {
    if (!profile) return null;
    const model = findModel(modelsStore?.get().data?.models ?? [], profile.model_id);
    const permission = {
      "read-only": ["shell.permission.readOnly", uiText("shell.permission.readOnly", {}, "只读")],
      balanced: ["shell.permission.balanced", uiText("shell.permission.balanced", {}, "询问")],
      "full-access": ["shell.permission.fullAccess", uiText("shell.permission.fullAccess", {}, "完全访问")],
    }[profile.permission_profile];
    const reasoning = t(`reasoning.${profile.reasoning_effort}`, {},
      profile.reasoning_effort);
    const permissionText = permission ? t(permission[0], {}, permission[1]) :
      profile.permission_profile;
    return t("queue.profileSnapshot", {
        model: model?.name || profile.model_id, effort: reasoning,
        permission: permissionText,
      }, `已固定：${model?.name || profile.model_id} · ${reasoning} · ${permissionText}`);
  }

  function update(key, response) {
    queueResponse(response);
    const data = response.data;
    if (data.revision !== undefined && metadata.get(key)?.revision > data.revision) return;
    metadata.set(key, data);
    versions.set(key, (versions.get(key) ?? 0) + 1);
    const items = response.data?.items ?? [];
    if (!(queues.get(key)?.length) && items.length) expanded.set(key, true);
    queues.set(key, items);
    render();
    onUpdated();
  }

  async function load(key, force = false) {
    if (!key) return;
    if (!force && queues.has(key)) return;
    if (loads.has(key)) return loads.get(key);
    const version = versions.get(key) ?? 0;
    const request = (async () => {
      const response = await transport.readQueue(key);
      if ((versions.get(key) ?? 0) === version) {
        update(key, response);
        unusedImages.remember(key, response.data?.discard_images);
      }
    })();
    loads.set(key, request);
    try { await request; }
    finally { loads.delete(key); }
  }

  async function postItem(projectId, sessionId, text,
    { id = newId(), first = false, priority = false, attachments = [],
      stage = false, profile = null } = {}) {
    const key = sessionKey(projectId, sessionId);
    if (!key || (!text.trim() && !attachments.length)) return null;
    const origin = transport.stamp();
    await load(key);
    if ((queues.get(key) ?? []).length >= 20 &&
        !(queues.get(key) ?? []).some((item) => item.id === id)) return null;
    const body = { id, text: text.trim(), attachments, first, priority, managed: true };
    if (stage) body.stage = true;
    if (profile) body.profile = profile;
    const keepalive = new TextEncoder().encode(JSON.stringify(body)).length <= 60 * 1024;
    update(key, await transport.post(key, body, { keepalive, origin }));
    return id;
  }

  function render() {
    const key = selectedKey();
    if (reorder?.active()) {
      if (container.dataset.queueKey !== key) reorder.cancel();
      else return;
    }
    const saved = queues.get(key) ?? [];
    const staged = stagedEntries?.() ?? [];
    // Display ownership is independent of durable queue ownership. Retain
    // every item for dispatch/receipts, but never repeat a conversation row.
    const entries = [...saved, ...staged].filter(item => !isMessagePresented(item.id));
    const meta = metadata.get(key) ?? {};
    const uncertain = saved[0]?.state === "sending";
    // This page still owns the dispatch while exclusive() is running. A
    // persisted sending item without that owner needs review after reload.
    const sendingLocally = uncertain && (busy.has(key) || meta.managed && !meta.paused);
    const acceptedRun = uncertain && Boolean(saved[0]?.run_id);
    const claimedRun = uncertain && saved[0]?.start_claimed === true;
    const stagedHead = saved[0]?.state === "staged";
    const reviewHead = saved.length && isMessagePresented(saved[0].id) &&
      (meta.paused || uncertain && !sendingLocally || stagedHead && !meta.managed);
    const reviewOnly = reviewHead && !entries.length;
    const focused = container.dataset.queueKey === key &&
      container.contains(document.activeElement) ? document.activeElement : null;
    const previousList = container.dataset.queueKey === key &&
      !container.hidden ? container.querySelector("#prompt-queue-list") : null;
    const scroll = previousList && !previousList.hidden ? {
      top: previousList.scrollTop,
      atBottom: previousList.scrollHeight - previousList.clientHeight -
        previousList.scrollTop <= 8,
    } : null;
    if (scroll && !scroll.atBottom) {
      const viewport = previousList.getBoundingClientRect();
      const previousItems = [...previousList.children];
      const anchorIndex = previousItems.findIndex((item) => {
        const bounds = item.getBoundingClientRect();
        return item.dataset.queueItemId && bounds.bottom > viewport.top + 1 &&
          bounds.top < viewport.bottom - 1;
      });
      if (anchorIndex >= 0) {
        const anchor = previousItems[anchorIndex];
        scroll.anchorId = anchor.dataset.queueItemId;
        scroll.anchorTop = anchor.getBoundingClientRect().top - viewport.top;
        scroll.nextIds = previousItems.slice(anchorIndex + 1)
          .map((item) => item.dataset.queueItemId).filter(Boolean);
      }
    }
    const focusKey = focused?.dataset.queueFocus;
    const imageRef = focused?.dataset.imageRef;
    const focusIndex = Number(focused?.dataset.queueIndex ?? 0);
    if (previousList && (entries.length || reviewOnly)) {
      for (const child of [...container.children]) {
        if (child !== previousList) child.remove();
      }
    } else clear(container);
    container.dataset.queueKey = key;
    container.hidden = !entries.length && !reviewOnly;
    if (container.hidden) {
      if (focused) document.querySelector("#prompt")?.focus({ preventScroll: true });
      return;
    }
    const reviewPending = isRunReviewPending(key);
    const writable = isSessionWritable();
    let open = expanded.get(key) ?? true;
    const toggle = element(reviewOnly ? "span" : "button", {
      className: reviewOnly ? "prompt-queue-waiting" : "prompt-queue-toggle",
      text: reviewOnly ? uiMessage(meta.paused ? "queue.paused" : "queue.reviewRun") :
        uiMessage("queue.count", { count: entries.length }),
      attrs: reviewOnly ? { tabindex: "-1", "data-queue-focus": "toggle" } : {
        type: "button", "aria-expanded": String(open),
        "aria-controls": "prompt-queue-list", "data-queue-focus": "toggle" },
    });
    const waitingForRun = !meta.paused && !uncertain && !stagedHead && isRunActive();
    const waitingForProfile = !waitingForRun && isProfileBusy();
    const retry = !writable || waitingForRun || waitingForProfile || !saved.length || reviewPending ||
      sendingLocally ||
      acceptedRun || claimedRun || saved[0]?.hold_for_edit || (meta.managed && !meta.paused) || saved[0]?.state === "editing" ? null : element("button", {
      text: uiMessage(uncertain ? "queue.retryUncertain" : stagedHead
        ? "queue.continueStaged" : "queue.sendNext"),
      attrs: { type: "button", "data-queue-focus": "retry" },
    });
    if (retry) {
      retry.disabled = busy.has(key) || actionBusy.has(key);
      retry.addEventListener("click", async () => {
        if (!isSessionWritable() || busy.has(key) || actionBusy.has(key)) return;
        actionBusy.add(key);
        render();
        try { await onRetry(); }
        catch (error) { toast(errorMessage(error), "error"); }
        finally { actionBusy.delete(key); render(); }
      });
    }
    const header = element("div", { className: "prompt-queue-header" }, [
      toggle, retry ?? element("span", { className: "prompt-queue-waiting",
        text: meta.paused ? uiMessage("queue.paused", {}, "队列已暂停") : uiMessage(!writable ? "queue.restoreToSend" :
          saved[0]?.priority && !stagedHead ? "submission.steering" :
          entries.length && !reviewHead && saved[0] && isMessagePresented(saved[0].id) ? "queue.waitForRun" : sendingLocally ? "queue.sending" :
          acceptedRun ? "queue.runAccepted" : claimedRun
          ? "queue.runStarting" : reviewPending
          ? "queue.reviewRun" : waitingForProfile ? "queue.waitForProfile" : saved.length
          ? (waitingForRun && isRunFinishing() ? "queue.waitForRelease" : "queue.waitForRun") : entries[0]?.rejected
            ? "queue.rejected" : "queue.awaitingAdmission") }),
    ]);
    let reviewRemove = null;
    if (reviewHead) {
      const entry = saved[0], origin = transport.stamp();
      reviewRemove = element("button", { text: uiMessage("queue.removeNumber", { number: 1 }), attrs: {
        type: "button", "data-queue-focus": "review-remove" } });
      reviewRemove.disabled = !writable || busy.has(key) || actionBusy.has(key) ||
        (!canChange(entry) && !(meta.paused && entry.state === "sending"));
      reviewRemove.addEventListener("click", () => void runAction(key,
        () => removeItem(entry.id, key), origin));
      header.append(reviewRemove);
    }
    container.insertBefore(header, previousList);
    if (meta.paused && meta.error) container.insertBefore(element("p", {
      className: "prompt-queue-warning", text: errorMessage({ code: meta.error }),
    }), previousList);
    if (uncertain && !sendingLocally) container.insertBefore(element("p", {
      className: "prompt-queue-warning",
      text: uiMessage(acceptedRun ? "queue.runAcceptedWarning" : claimedRun
        ? "queue.runStartingWarning" :
        "queue.uncertainWarning"),
    }), previousList);
    const list = previousList ?? element("ol", { className: "prompt-queue-list",
      attrs: { id: "prompt-queue-list" } });
    const nextCards = [];
    list.hidden = !open || !entries.length;
    if (!reviewOnly) toggle.addEventListener("click", () => {
      open = !open;
      expanded.set(key, open);
      toggle.setAttribute("aria-expanded", String(open));
      list.hidden = !open;
    });
    for (const [index, entry] of entries.entries()) {
      const origin = transport.stamp();
      const itemAttrs = typeof entry.id === "string" && entry.id
        ? { "data-queue-item-id": entry.id } : {};
      if (entry.staged) {
        nextCards.push(element("li", { attrs: itemAttrs }, [
          element("span", { className: "prompt-queue-index",
            text: String(index + 1) }),
          element("div", { className: "prompt-queue-item-body" }, [
            element("span", { className: "prompt-queue-text",
              text: promptContextSummary(entry.text) || uiMessage("queue.imageMessage"),
              attrs: { title: promptContextText(entry.text) || uiMessage("queue.imageMessage") } }),
            element("span", { className: "prompt-queue-state",
              text: uiMessage(entry.rejected ? "queue.rejected" :
                "queue.awaitingAdmission") }),
          ]),
        ]));
        continue;
      }
      const locked = !canChange(entry) || !writable || busy.has(key) || actionBusy.has(key);
      const handle = element("button", { className: "prompt-queue-drag", text: "⠿",
        attrs: { type: "button", "data-queue-drag": "", "data-queue-focus": `${entry.id}:drag`,
          "aria-label": uiMessage("queue.drag", {}, "拖动排序，或按上下方向键调整顺序"),
          title: uiMessage("queue.drag", {}, "拖动排序，或按上下方向键调整顺序") } });
      handle.disabled = locked;
      if (entry.state === "cancelled") handle.disabled = true;
      const remove = element("button", {
        text: uiMessage("queue.remove"),
        attrs: { type: "button", "aria-label": uiMessage("queue.removeNumber", { number: index + 1 }),
          title: uiMessage("queue.remove"),
          "data-queue-focus": entry.id, "data-queue-index": String(index) },
      });
      // Keep the focused remove button focusable while its request is in flight.
      // The click guard below blocks repeats; aria-disabled exposes that state.
      remove.disabled = (!canChange(entry) && !(meta.paused && entry.state === "sending")) || busy.has(key);
      if (actionBusy.has(key)) remove.setAttribute("aria-disabled", "true");
      remove.addEventListener("click", async () => {
        if (transport.stamp() !== origin || busy.has(key) || actionBusy.has(key)) return;
        actionBusy.add(key);
        render();
        try { await removeItem(entry.id, key); }
        catch (error) { toast(errorMessage(error), "error"); }
        finally { actionBusy.delete(key); render(); }
      });
      const body = element("div", { className: "prompt-queue-item-body" }, [
        element("span", { className: "prompt-queue-text",
          text: promptContextSummary(entry.text) || uiMessage("queue.imageMessage"), attrs: {
            title: [promptContextText(entry.text) || t("queue.imageMessage"), profileLabel(entry.profile)].filter(Boolean).join("\n\n") } }),
      ]);
      if (entry.attachments?.length) {
        body.append(element("span", { className: "prompt-queue-attachments",
          text: `▧ ${entry.attachments.length}`,
          attrs: { "aria-label": uiMessage("queue.imageMessage") } }));
      }
      const actions = element("div", { className: "prompt-queue-actions" });
      function action(label, fallback, suffix, callback, text = null) {
        const button = element("button", { text: text ?? uiMessage(label, {}, fallback), attrs: {
          type: "button", "aria-label": uiMessage(label, {}, fallback), title: uiMessage(label, {}, fallback),
          "data-queue-focus": `${entry.id}:${suffix}` } });
        button.disabled = locked;
        button.addEventListener("click", () => void runAction(key, callback, origin));
        actions.append(button);
        return button;
      }
      const steer = action("queue.steer", uiMessage("queue.steer", {}, "引导"), "steer", () => performAction(key, "steer", entry.id));
      if (entry.state === "cancelled") steer.hidden = true;
      writeUiAttribute(steer, "title", uiMessage("queue.steerHint", {}, "停止当前执行并优先发送此消息"));
      if (onWithdraw) action("queue.edit", uiMessage("queue.edit", {}, "撤回编辑"), "edit", () =>
        onWithdraw(key, entry, draftRevision => performAction(key, "withdraw", entry.id, "", draftRevision)), "✎");
      writeUiText(remove, "×");
      actions.append(remove);
      nextCards.push(element("li", { attrs: itemAttrs }, [
        handle, element("span", { className: "prompt-queue-index",
          text: String(index + 1) }),
        body,
        actions,
      ]));
    }
    // Keep an unchanged card connected while queue polls or siblings change.
    // Replacing it discards the user's text selection, even when scroll is restored.
    const oldCards = new Map([...list.children]
      .filter((card) => card.dataset.queueItemId)
      .map((card) => [card.dataset.queueItemId, card]));
    const nextIds = new Set(nextCards.map((card) =>
      card.dataset.queueItemId).filter(Boolean));
    for (const old of [...list.children]) {
      if (!old.dataset.queueItemId || !nextIds.has(old.dataset.queueItemId))
        old.remove();
    }
    for (const [index, card] of nextCards.entries()) {
      const previous = card.dataset.queueItemId &&
        oldCards.get(card.dataset.queueItemId);
      if (previous) {
        const oldIndex = previous.querySelector(".prompt-queue-index");
        const newIndex = card.querySelector(".prompt-queue-index");
        if (oldIndex.textContent !== newIndex.textContent)
          writeUiText(oldIndex, newIndex.textContent);
        const oldRemove = previous.querySelector("button[data-queue-index]");
        const newRemove = card.querySelector("button[data-queue-index]");
        if (oldRemove && newRemove) {
          for (const name of ["aria-label", "data-queue-index"]) {
            if (oldRemove.getAttribute(name) !== newRemove.getAttribute(name))
              oldRemove.setAttribute(name, newRemove.getAttribute(name));
          }
        }
      }
      const current = previous?.outerHTML === card.outerHTML ? previous : card;
      if (list.children[index] !== current)
        list.insertBefore(current, list.children[index] ?? null);
    }
    while (list.children.length > nextCards.length) list.lastElementChild.remove();
    if (list.parentElement !== container) container.append(list);
    if (scroll && !list.hidden) {
      list.scrollTop = scroll.atBottom ? list.scrollHeight : scroll.top;
      if (!scroll.atBottom && scroll.anchorId) {
        const items = [...list.children];
        const anchor = items.find((item) =>
          item.dataset.queueItemId === scroll.anchorId);
        if (anchor) list.scrollTop += anchor.getBoundingClientRect().top -
          list.getBoundingClientRect().top - scroll.anchorTop;
        else {
          // The item being read was removed; show the next item from its start.
          const next = scroll.nextIds.map((id) => items.find((item) =>
            item.dataset.queueItemId === id)).find(Boolean);
          if (next) list.scrollTop += next.getBoundingClientRect().top -
            list.getBoundingClientRect().top;
          else list.scrollTop = list.scrollHeight;
        }
      }
    }
    if (focused) {
      const removes = [...list.querySelectorAll("button[data-queue-index]")];
      const target = focusKey === "toggle" ? toggle :
        focusKey === "retry" ? retry ?? toggle :
        focusKey === "review-remove" ? reviewRemove ?? toggle :
        imageRef ? [...list.querySelectorAll("button[data-image-ref]")]
          .find((item) => item.dataset.imageRef === imageRef) :
        [...list.querySelectorAll("button[data-queue-focus]")].find(item => item.dataset.queueFocus === focusKey) ??
          removes[Math.min(focusIndex, removes.length - 1)];
      const restored = target?.disabled ? toggle : target ?? toggle;
      restored.focus({ preventScroll: true });
      if (!list.hidden && list.contains(restored)) {
        const viewport = list.getBoundingClientRect();
        const bounds = restored.getBoundingClientRect();
        if (bounds.top < viewport.top)
          list.scrollTop += bounds.top - viewport.top;
        else if (bounds.bottom > viewport.bottom)
          list.scrollTop += bounds.bottom - viewport.bottom;
      }
    }
  }

  async function removeItem(id, key = selectedKey()) {
    if (!key) return;
    const removed = queues.get(key)?.find((entry) => entry.id === id);
    update(key, await transport.remove(key, id));
    try { await onRemoved?.(key, removed); }
    finally { unusedImages.remember(key, removed?.attachments); }
  }

  navigation.subscribe(render);
  modelsStore?.subscribe(render);
  subscribeLocale(render);
  return Object.freeze({
    newId,
    stamp: () => transport.stamp(),
    isManaged(projectId, sessionId) { return metadata.get(sessionKey(projectId, sessionId))?.managed === true; },
    async resume(projectId, sessionId) { return performAction(sessionKey(projectId, sessionId), "resume"); },
    async select(projectId, sessionId) {
      const key = sessionKey(projectId, sessionId);
      await load(key, true);
      render();
    },
    async enqueue(projectId, sessionId, text,
      { first = false, priority = false, attachments = [],
        profile = null } = {}) {
      return Boolean(await postItem(projectId, sessionId, text,
        { first, priority, attachments, profile }));
    },
    async stage(projectId, sessionId, submission,
      { first = false } = {}) {
      return postItem(projectId, sessionId, submission.text, {
        id: submission.id, first, priority: submission.interrupt,
        attachments: submission.attachments, stage: true,
        profile: submission.profile,
      });
    },
    find(projectId, sessionId, id) {
      return queues.get(sessionKey(projectId, sessionId))?.find((item) =>
        item.id === id) ?? null;
    },
    snapshot(projectId, sessionId) {
      const key = sessionKey(projectId, sessionId);
      return { ...(metadata.get(key) ?? {}), items: [...(queues.get(key) ?? [])] };
    },
    receipt(projectId, sessionId, id) {
      return transport.readReceipt(sessionKey(projectId, sessionId), id);
    },
    status(projectId, sessionId, id) { return transport.readStatus(sessionKey(projectId, sessionId), id); },
    async cancel(projectId, sessionId, id) {
      const key = sessionKey(projectId, sessionId);
      const result = await transport.cancel(key, id);
      await load(key, true); return result;
    },
    async withdraw(projectId, sessionId, id) {
      const key = sessionKey(projectId, sessionId);
      const item = queues.get(key)?.find(entry => entry.id === id);
      if (!item) return false;
      await onWithdraw(key, item, revision => performAction(key, "withdraw", id, "", revision));
      return true;
    },
    hasStaged(projectId, sessionId) {
      return queues.get(sessionKey(projectId, sessionId))?.some((item) =>
        item.state === "staged") ?? false;
    },
    hasUnsettled(projectId, sessionId) {
      const key = sessionKey(projectId, sessionId);
      return Boolean(key && (loads.has(key) || busy.has(key) ||
        queues.get(key)?.length));
    },
    async promote(projectId, sessionId, id) {
      const key = sessionKey(projectId, sessionId);
      update(key, await transport.promote(key, id));
      return true;
    },
    peek(projectId, sessionId) {
      const key = sessionKey(projectId, sessionId);
      return loads.has(key) ? null : (queues.get(key)?.[0] ?? null);
    },
    async markSending(projectId, sessionId, id) {
      const key = sessionKey(projectId, sessionId);
      update(key, await transport.markSending(key, id));
    },
    async retry(projectId, sessionId, id) {
      const key = sessionKey(projectId, sessionId);
      update(key, await transport.retry(key, id));
    },
    async remove(projectId, sessionId, id) {
      const key = sessionKey(projectId, sessionId);
      update(key, await transport.remove(key, id));
      // Dispatch has transferred any images to the run and its history.
    },
    async exclusive(projectId, sessionId, callback) {
      const key = sessionKey(projectId, sessionId);
      if (!key || busy.has(key)) return false;
      busy.add(key);
      render();
      try { await callback(); return true; }
      finally { busy.delete(key); render(); }
    },
    hasInFlight: () => Boolean(busy.size || actionBusy.size || reorder.active()),
    render,
    rememberUnusedImages(projectId, sessionId, ids) {
      unusedImages.remember(sessionKey(projectId, sessionId), ids);
    },
    flushUnusedImages: () => unusedImages.flush(),
  });
}
