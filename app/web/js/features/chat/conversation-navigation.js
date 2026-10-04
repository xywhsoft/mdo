import { element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";

// The rail owns only the compact index. Selecting an unloaded turn asks the
// store for its event range, so jumping never downloads the intervening text.
export function createConversationNavigation({ scroller, container, onReveal, onLoadIndex }) {
  const workspace = scroller.parentElement;
  const rail = element("nav", { className: "conversation-history-rail", attrs: {
    "aria-label": t("timeline.historyNavigation", {}, "对话历史导航") } });
  const marks = element("div", { className: "conversation-history-marks" });
  const older = element("button", { className: "conversation-history-older", text: "⋯", attrs: {
    type: "button", "aria-label": t("timeline.olderSummaries", {}, "更早的对话摘要") } });
  const preview = element("div", { className: "conversation-history-preview", attrs: { role: "tooltip" } });
  preview.id = `history-preview-${Math.random().toString(36).slice(2)}`;
  preview.hidden = true;
  rail.append(older, marks, preview);
  workspace.append(rail);
  let data = null;
  let signature = "";
  let frame = 0;
  let hideTimer = 0;
  let selected = 0;
  let previewFor = 0;
  let emphasized = [];
  const touchScreen = window.matchMedia("(hover: none), (pointer: coarse)");
  const rows = new Map();
  const summaries = new Map();

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
  function hide() {
    window.clearTimeout(hideTimer);
    rows.get(previewFor)?.removeAttribute("aria-describedby");
    preview.hidden = true;
    previewFor = 0;
    emphasize(null);
  }
  function positionPreview(button) {
    const bounds = scroller.getBoundingClientRect();
    const origin = rail.getBoundingClientRect().top;
    const mark = button.getBoundingClientRect();
    // Clamp to the conversation, rather than the shorter, centred rail. A
    // single-turn tooltip can then stay centred on its mark without clipping.
    const top = mark.top + mark.height / 2 - preview.offsetHeight / 2;
    preview.style.top = `${Math.max(bounds.top + 12,
      Math.min(top, bounds.bottom - 12 - preview.offsetHeight)) - origin}px`;
  }
  function show(button, turn) {
    if (!turn || rail.hidden) return;
    rows.get(previewFor)?.removeAttribute("aria-describedby");
    previewFor = turn.first_event_id;
    window.clearTimeout(hideTimer);
    emphasize(button);
    preview.replaceChildren(element("strong", { text: turn.question ||
      t("timeline.emptyPrompt", {}, "附件任务") }), element("p", {
      text: turn.answer || t("timeline.noReplyYet", {}, "尚无回复") }));
    preview.hidden = false;
    positionPreview(button);
    button.setAttribute("aria-describedby", preview.id);
  }
  function delayHide() { hideTimer = window.setTimeout(hide, 160); }

  async function jump(button, turn) {
    if (button.disabled || !turn) return;
    selected = turn.first_event_id;
    button.disabled = true;
    rail.setAttribute("aria-busy", "true");
    try { await onReveal(turn.first_event_id); }
    catch (error) { toast(errorMessage(error), "error"); }
    finally { button.disabled = false; rail.removeAttribute("aria-busy"); syncActive(); }
  }
  function layout() {
    const box = scroller.getBoundingClientRect();
    const parent = workspace.getBoundingClientRect();
    // Leave enough room beside the 760px transcript, including its margins.
    // Observe the column itself: an open sidebar can narrow a desktop window.
    rail.hidden = !data?.sessionId || !data.turns?.length || box.width < 880
      || box.height <= 48 || touchScreen.matches;
    if (rail.hidden) { hide(); return; }
    const height = Math.min(marks.children.length * 10 + (older.hidden ? 0 : 26), box.height - 48);
    rail.style.top = `${box.top - parent.top + (box.height - height) / 2}px`;
    rail.style.height = `${height}px`;
    if (previewFor && rows.has(previewFor)) positionPreview(rows.get(previewFor));
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
  }
  function schedule() { if (!frame) frame = requestAnimationFrame(syncActive); }
  function update(next) {
    data = next;
    older.hidden = !data?.indexHasMore;
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
        button.setAttribute("aria-label", (turn.question || t("timeline.emptyPrompt", {}, "附件任务")).slice(0, 200));
        if (button !== cursor) marks.insertBefore(button, cursor);
        cursor = button.nextSibling;
      }
      while (cursor) { const next = cursor.nextSibling; cursor.remove(); cursor = next; }
      for (const id of rows.keys()) if (!retained.has(id)) { rows.delete(id); summaries.delete(id); }
      layout();
      marks.scrollTop = signature ? scroll + Math.max(0, marks.scrollHeight - oldHeight) : marks.scrollHeight;
      if (focused) marks.querySelector(`[data-turn-id="${focused}"]`)?.focus({ preventScroll: true });
      signature = key;
      if (previewFor && summaries.has(previewFor)) show(rows.get(previewFor), summaries.get(previewFor));
      else hide();
    }
    schedule();
  }
  older.addEventListener("click", async () => {
    older.disabled = true;
    try {
      await onLoadIndex?.();
      await new Promise(resolve => requestAnimationFrame(resolve));
      marks.scrollTop = 0;
    }
    catch (error) { toast(errorMessage(error), "error"); }
    finally { older.disabled = false; }
  });
  preview.addEventListener("pointerenter", () => window.clearTimeout(hideTimer));
  preview.addEventListener("pointerleave", delayHide);
  marks.addEventListener("scroll", hide, { passive: true });
  rail.addEventListener("keydown", event => { if (event.key === "Escape") hide(); });
  scroller.addEventListener("scroll", schedule, { passive: true });
  window.addEventListener("resize", schedule);
  touchScreen.addEventListener("change", schedule);
  const observer = typeof ResizeObserver === "function" ? new ResizeObserver(schedule) : null;
  observer?.observe(scroller);
  const locale = subscribeLocale(() => {
    rail.setAttribute("aria-label", t("timeline.historyNavigation", {}, "对话历史导航"));
    older.setAttribute("aria-label", t("timeline.olderSummaries", {}, "更早的对话摘要"));
    signature = ""; update(data);
  });
  return Object.freeze({ update, destroy() {
    locale(); observer?.disconnect(); rail.remove(); window.clearTimeout(hideTimer);
    scroller.removeEventListener("scroll", schedule); window.removeEventListener("resize", schedule);
    touchScreen.removeEventListener("change", schedule);
    if (frame) cancelAnimationFrame(frame);
  } });
}
