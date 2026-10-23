import { element, isImeKey } from "../../utils/dom.js";
import { t } from "../../i18n.js";
import { newPromptContext } from "./prompt-context.js";

export function createSelectionQuote({ container, owner, add, writable = () => true }) {
  const button = element("button", { className: "selection-quote-button", text: t("context.quoteSelection"), attrs: { type: "button" } });
  button.hidden = true; document.body.append(button);
  let captured = null;
  function hide() { button.hidden = true; captured = null; }
  function capture() {
    const selection = document.getSelection();
    if (!writable() || selection?.rangeCount !== 1 || selection.isCollapsed) { hide(); return; }
    const range = selection.getRangeAt(0), node = range.commonAncestorContainer;
    const elementNode = node.nodeType === 1 ? node : node.parentElement;
    const item = elementNode?.closest(".timeline-item");
    if (!item || !container.contains(item) || elementNode.closest("input,textarea,[contenteditable],button")) { hide(); return; }
    const text = selection.toString();
    if (!text.trim() || new TextEncoder().encode(text).length > 16384) { hide(); return; }
    const rect = range.getBoundingClientRect(), scope = owner();
    const source = elementNode.closest("[data-source-event-id]")?.dataset.sourceEventId;
    captured = { text, scope: JSON.stringify(scope), context: newPromptContext("quote", t("context.quoteLabel"), {
      content: text, project: scope.projectId, session: scope.sessionId,
      ...(source && Number(source) > 0 ? { event: Number(source) } : {}) }) };
    button.hidden = false;
    button.style.left = `${Math.max(8, Math.min(rect.left, innerWidth - button.offsetWidth - 8))}px`;
    button.style.top = `${Math.max(8, Math.min(rect.top - button.offsetHeight - 6, innerHeight - button.offsetHeight - 8))}px`;
  }
  button.addEventListener("pointerdown", event => event.preventDefault());
  button.addEventListener("click", () => {
    if (captured && writable() && captured.scope === JSON.stringify(owner())) add(captured.context);
    hide();
  });
  container.addEventListener("pointerup", () => setTimeout(capture, 0));
  container.addEventListener("keyup", event => { if (!isImeKey(event) && event.shiftKey) capture(); });
  document.addEventListener("selectionchange", () => { if (document.getSelection()?.isCollapsed) hide(); });
  document.addEventListener("pointerdown", event => { if (!button.contains(event.target)) hide(); });
  window.addEventListener("scroll", hide, true); window.addEventListener("resize", hide);
  return { hide };
}
