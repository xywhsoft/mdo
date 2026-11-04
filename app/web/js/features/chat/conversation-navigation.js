import { setUiAttribute as writeUiAttribute, uiMessage, subscribeLocale, t } from "../../i18n.js";

import { element, errorMessage, toast } from "../../utils/dom.js";

// The rail owns only the compact index. Selecting an unloaded turn asks the
// store for its event range, so jumping never downloads the intervening text.
export function createConversationNavigation({ scroller, container, onReveal, onLoadIndex }) {
  const workspace = scroller.parentElement;
  const rail = element("nav", { className: "conversation-history-rail", attrs: {
    "aria-label": uiMessage("timeline.historyNavigation", {}, "对话历史导航") } });
  const marks = element("div", { className: "conversation-history-marks" });
  const preview = element("div", { className: "conversation-history-preview", attrs: { role: "tooltip" } });
  preview.id = `history-preview-${Math.random().toString(36).slice(2)}`;
  preview.hidden = true;
  rail.hidden = true;
  rail.append(marks, preview);
  workspace.append(rail);
  let data = null;
  let signature = "";
  let frame = 0;
  let hideTimer = 0;
  let selected = 0;
  let previewFor = 0;
  let emphasized = [];
  let owner = "";
  let ownerVersion = 0;
  let disposed = false;
  let indexPending = false;
  let blockedIndex = "";
  let capacity = 0;
  let eligible = false;
  const touchScreen = window.matchMedia("(hover: none), (pointer: coarse)");
  const rows = new Map();
  const summaries = new Map();

  function visibleBounds() {
    const box = scroller.getBoundingClientRect();
    const overlap = Math.max(0, parseFloat(getComputedStyle(scroller)
      .getPropertyValue("--conversation-dock-overlap")) || 0);
    return { top: box.top, left: box.left, width: box.width,
      bottom: Math.max(box.top, box.bottom - overlap),
      height: Math.max(0, box.height - overlap) };
  }

  // Only the hovered/focused mark and its three neighbours on each side grow.
  // Scroll position changes brightness, never the resting width of a mark.
  function emphasize(button) {
    for (const row of emphasized) row.removeAttribute("data-proximity");
    emphasized = [];
    if (!button) return;
    button.dataset.proximity = "0";
    emphasized.push(button);
    let before = button.previousElementSibling;
    let after = button.nextElementSibling;
    for (let distance = 1; distance <= 3; distance++) {
      for (const row of [before, after]) if (row) {
        row.dataset.proximity = String(distance);
        emphasized.push(row);
      }
      before = before?.previousElementSibling;
      after = after?.nextElementSibling;
    }
  }
  function cancelHide() {
    window.clearTimeout(hideTimer);
    hideTimer = 0;
  }
  function hide() {
    cancelHide();
    rows.get(previewFor)?.removeAttribute("aria-describedby");
    preview.hidden = true;
    previewFor = 0;
    emphasize(null);
  }
  function positionPreview(button) {
    const bounds = visibleBounds();
    const origin = rail.getBoundingClientRect().top;
    const mark = button.getBoundingClientRect();
    // Clamp to the conversation, rather than the shorter, centred rail. A
    // single-turn tooltip can then stay centred on its mark without clipping.
    const top = mark.top + mark.height / 2 - preview.offsetHeight / 2;
    preview.style.top = `${Math.max(bounds.top + 12,
      Math.min(top, bounds.bottom - 12 - preview.offsetHeight)) - origin}px`;
  }
  function refreshPreview(button, turn) {
    if (!button || !turn || rail.hidden) return;
    rows.get(previewFor)?.removeAttribute("aria-describedby");
    previewFor = turn.first_event_id;
    emphasize(button);
    preview.replaceChildren(element("strong", { text: turn.question ||
      uiMessage("timeline.emptyPrompt", {}, "附件任务") }), element("p", {
      text: turn.answer || uiMessage("timeline.noReplyYet", {}, "尚无回复") }));
    preview.hidden = false;
    positionPreview(button);
    button.setAttribute("aria-describedby", preview.id);
    schedule();
  }
  function show(button, turn) {
    if (!button || !turn || rail.hidden) return;
    cancelHide();
    refreshPreview(button, turn);
  }
  function delayHide() {
    cancelHide();
    hideTimer = window.setTimeout(hide, 160);
  }

  async function jump(button, turn) {
    if (button.disabled || !turn) return;
    const version = ownerVersion;
    selected = turn.first_event_id;
    button.disabled = true;
    rail.setAttribute("aria-busy", "true");
    try { await onReveal(turn.first_event_id); }
    catch (error) { if (version === ownerVersion && !disposed && error?.name !== "AbortError") toast(errorMessage(error), "error"); }
    finally { if (version === ownerVersion && !disposed) { button.disabled = false; rail.removeAttribute("aria-busy"); schedule(); } }
  }
  function layout() {
    const box = visibleBounds();
    const parent = workspace.getBoundingClientRect();
    const step = parseFloat(getComputedStyle(rail).getPropertyValue("--history-mark-height")) || 10;
    // Capacity follows the visible conversation, including floating task and
    // approval docks. Short histories fit in full; longer indexes scroll.
    capacity = Math.max(0, Math.floor((box.height - 24) / step));
    eligible = Boolean(data?.sessionId && box.width >= 240 && window.innerWidth > 760
      && capacity > 0 && !touchScreen.matches && !document.hidden);
    rail.hidden = !eligible || !data.turns?.length;
    const shown = !rail.hidden;
    if (scroller.classList.contains("has-history-navigation") !== shown)
      scroller.classList.toggle("has-history-navigation", shown);
    if (rail.hidden) { hide(); return; }
    const height = Math.min(marks.children.length, capacity) * step;
    rail.style.top = `${box.top - parent.top + (box.height - height) / 2}px`;
    rail.style.height = `${height}px`;
    if (previewFor && rows.has(previewFor)) positionPreview(rows.get(previewFor));
  }
  function indexCursor() {
    return JSON.stringify([ownerVersion, data?.indexInitialized, data?.indexBefore,
      data?.turns?.length]);
  }
  function fillIndex(earlier = false) {
    if (disposed || !eligible || data?.initializing || !data?.indexHasMore || !onLoadIndex || indexPending
      || (!earlier && data.indexInitialized !== false && data.turns.length >= capacity)) return;
    const cursor = indexCursor();
    if (blockedIndex === cursor) return;
    const version = ownerVersion;
    indexPending = true;
    // This never waits for transcript nodes or downloads intervening text.
    // One compact page at a time also bounds work after a viewport resize.
    void Promise.resolve().then(() => {
      if (!disposed && version === ownerVersion) return onLoadIndex();
    }).catch(error => {
      if (version !== ownerVersion || disposed) return;
      if (error?.name === "AbortError") return;
      blockedIndex = cursor;
      toast(errorMessage(error), "error");
    }).finally(() => {
      if (version !== ownerVersion || disposed) return;
      indexPending = false;
      if (cursor === indexCursor()) blockedIndex = cursor;
      schedule();
    });
  }
  function syncActive() {
    frame = 0;
    layout();
    const top = scroller.getBoundingClientRect().top;
    const users = [...container.querySelectorAll("[data-turn-id]")];
    const current = users.filter(node => node.getBoundingClientRect().top <= top + 100).at(-1)
      ?? users.find(node => node.getBoundingClientRect().bottom > top);
    if (current) selected = Number(current.dataset.turnId);
    for (const button of marks.children)
      button.setAttribute("aria-current", String(Number(button.dataset.turnId) === selected));
    fillIndex();
  }
  function schedule() { if (!disposed && !frame) frame = requestAnimationFrame(syncActive); }
  function update(next) {
    data = next;
    const identity = JSON.stringify([data?.projectId, data?.sessionId, data?.epoch]);
    if (owner !== identity) {
      owner = identity; ++ownerVersion; indexPending = false; blockedIndex = "";
      signature = ""; selected = 0; hide(); rail.removeAttribute("aria-busy");
      marks.replaceChildren(); rows.clear(); summaries.clear();
    }
    const key = JSON.stringify([data?.projectId, data?.sessionId, data?.turns]);
    if (key !== signature) {
      const focused = marks.contains(document.activeElement) ? document.activeElement.dataset.turnId : "";
      const scroll = marks.scrollTop;
      const oldHeight = marks.scrollHeight;
      const retained = new Set();
      let cursor = marks.firstChild;
      for (const turn of data?.turns ?? []) {
        const id = turn.first_event_id;
        retained.add(id);
        summaries.set(id, turn);
        let button = rows.get(id);
        if (!button) {
          button = element("button", { className: "conversation-history-mark", attrs: {
            type: "button", "data-turn-id": id } },
          [element("span", { attrs: { "aria-hidden": "true" } })]);
          button.addEventListener("pointerenter", () => show(button, summaries.get(id)));
          button.addEventListener("pointerleave", delayHide);
          button.addEventListener("focus", () => show(button, summaries.get(id)));
          button.addEventListener("blur", delayHide);
          button.addEventListener("click", () => { hide(); void jump(button, summaries.get(id)); });
          rows.set(id, button);
        }
        writeUiAttribute(button, "aria-label", (turn.question || t("timeline.emptyPrompt", {}, "附件任务")).slice(0, 200));
        if (button !== cursor) marks.insertBefore(button, cursor);
        cursor = button.nextSibling;
      }
      while (cursor) { const next = cursor.nextSibling; cursor.remove(); cursor = next; }
      for (const id of rows.keys()) if (!retained.has(id)) { rows.delete(id); summaries.delete(id); }
      if (!selected && data?.turns?.length) selected = data.turns.at(-1).first_event_id;
      layout();
      marks.scrollTop = signature ? scroll + Math.max(0, marks.scrollHeight - oldHeight) : marks.scrollHeight;
      if (focused) marks.querySelector(`[data-turn-id="${focused}"]`)?.focus({ preventScroll: true });
      signature = key;
      // Reply/index refreshes update content only. Treating them as another
      // pointer entry cancels dismissal on every streamed reply fragment.
      if (previewFor && summaries.has(previewFor)) refreshPreview(rows.get(previewFor), summaries.get(previewFor));
      else hide();
    }
    schedule();
  }
  preview.addEventListener("pointerenter", cancelHide);
  preview.addEventListener("pointerleave", delayHide);
  rail.addEventListener("pointerleave", delayHide);
  rail.addEventListener("pointercancel", hide);
  marks.addEventListener("scroll", () => {
    hide();
    if (marks.scrollTop <= 20) fillIndex(true);
  }, { passive: true });
  marks.addEventListener("wheel", event => {
    if (event.deltaY < 0 && marks.scrollTop <= 20) { blockedIndex = ""; fillIndex(true); }
  }, { passive: true });
  rail.addEventListener("keydown", event => { if (event.key === "Escape") hide(); });
  scroller.addEventListener("scroll", schedule, { passive: true });
  window.addEventListener("resize", schedule);
  function windowBlur(event) { if (event.target === window) hide(); }
  window.addEventListener("blur", windowBlur);
  function resume() {
    if (document.hidden) hide();
    else { blockedIndex = ""; schedule(); }
  }
  window.addEventListener("pageshow", resume);
  document.addEventListener("visibilitychange", resume);
  touchScreen.addEventListener("change", schedule);
  const observer = typeof ResizeObserver === "function" ? new ResizeObserver(schedule) : null;
  observer?.observe(scroller);
  const overlapObserver = typeof MutationObserver === "function" ? new MutationObserver(schedule) : null;
  overlapObserver?.observe(scroller, { attributes: true, attributeFilter: ["style"] });
  const locale = subscribeLocale(() => {
    writeUiAttribute(rail, "aria-label", t("timeline.historyNavigation", {}, "对话历史导航"));
    signature = ""; update(data);
  });
  return Object.freeze({ update, destroy() {
    disposed = true; ++ownerVersion;
    locale(); observer?.disconnect(); overlapObserver?.disconnect(); rail.remove(); cancelHide();
    scroller.classList.remove("has-history-navigation");
    scroller.removeEventListener("scroll", schedule); window.removeEventListener("resize", schedule);
    window.removeEventListener("blur", windowBlur);
    window.removeEventListener("pageshow", resume); document.removeEventListener("visibilitychange", resume);
    touchScreen.removeEventListener("change", schedule);
    if (frame) cancelAnimationFrame(frame);
  } });
}
