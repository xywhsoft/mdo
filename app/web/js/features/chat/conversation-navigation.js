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

  function hide() { preview.hidden = true; }
  function show(button, turn) {
    window.clearTimeout(hideTimer);
    preview.replaceChildren(element("strong", { text: turn.question ||
      t("timeline.emptyPrompt", {}, "附件任务") }), element("p", {
      text: turn.answer || t("timeline.noReplyYet", {}, "尚无回复") }));
    preview.hidden = false;
    const top = button.getBoundingClientRect().top - rail.getBoundingClientRect().top;
    preview.style.top = `${Math.max(0, Math.min(top - preview.offsetHeight / 2,
      rail.clientHeight - preview.offsetHeight))}px`;
    button.setAttribute("aria-describedby", preview.id);
  }
  function delayHide() { hideTimer = window.setTimeout(hide, 160); }

  async function jump(button, turn) {
    if (button.disabled) return;
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
    rail.hidden = !data?.sessionId || !data.turns?.length || box.height < 1;
    rail.style.top = `${box.top - parent.top + 8}px`;
    rail.style.height = `${Math.max(0, box.height - 16)}px`;
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
    rail.hidden = !data?.sessionId || !data.turns?.length;
    older.hidden = !data?.indexHasMore;
    const key = JSON.stringify([data?.projectId, data?.sessionId, data?.turns]);
    if (key !== signature) {
      const focused = marks.contains(document.activeElement) ? document.activeElement.dataset.turnId : "";
      const scroll = marks.scrollTop;
      const oldHeight = marks.scrollHeight;
      marks.replaceChildren();
      for (const turn of data?.turns ?? []) {
        const button = element("button", { className: "conversation-history-mark", attrs: {
          type: "button", "data-turn-id": turn.first_event_id,
          "aria-label": (turn.question || t("timeline.emptyPrompt", {}, "附件任务")).slice(0, 200) } },
        [element("span", { attrs: { "aria-hidden": "true" } })]);
        button.addEventListener("pointerenter", () => show(button, turn));
        button.addEventListener("pointerleave", delayHide);
        button.addEventListener("focus", () => show(button, turn));
        button.addEventListener("blur", delayHide);
        button.addEventListener("click", () => { hide(); void jump(button, turn); });
        marks.append(button);
      }
      marks.scrollTop = signature ? scroll + Math.max(0, marks.scrollHeight - oldHeight) : marks.scrollHeight;
      if (focused) marks.querySelector(`[data-turn-id="${focused}"]`)?.focus({ preventScroll: true });
      signature = key;
      hide();
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
  rail.addEventListener("keydown", event => { if (event.key === "Escape") hide(); });
  scroller.addEventListener("scroll", schedule, { passive: true });
  window.addEventListener("resize", schedule);
  const observer = typeof ResizeObserver === "function" ? new ResizeObserver(schedule) : null;
  observer?.observe(scroller);
  const locale = subscribeLocale(() => { signature = ""; update(data); });
  return Object.freeze({ update, destroy() {
    locale(); observer?.disconnect(); rail.remove(); window.clearTimeout(hideTimer);
    scroller.removeEventListener("scroll", schedule); window.removeEventListener("resize", schedule);
    if (frame) cancelAnimationFrame(frame);
  } });
}
