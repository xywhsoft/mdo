import { uiMessage, t } from "../../i18n.js";
import { element, isImeKey } from "../../utils/dom.js";

import { newPromptContext } from "./prompt-context.js";
import { positionAnchoredMenu } from "../../utils/anchored-menu.js";

function excerpt(range, container) {
  const blocks = new Set(["P", "DIV", "PRE", "LI", "UL", "OL", "BLOCKQUOTE", "H1", "H2", "H3", "H4", "TR"]);
  const parts = [];
  for (const body of container.querySelectorAll(".timeline-item > .timeline-body")) {
    if (!range.intersectsNode(body)) continue;
    const bounds = document.createRange(); bounds.selectNodeContents(body);
    const selected = range.cloneRange();
    if (range.compareBoundaryPoints(0, bounds) < 0) selected.setStart(body, 0);
    if (range.compareBoundaryPoints(2, bounds) > 0) selected.setEnd(body, body.childNodes.length);
    let text = "";
    function visit(node) {
      if (node.nodeType === 3) { text += node.nodeValue; return; }
      if (["BUTTON", "SCRIPT", "STYLE"].includes(node.nodeName)) return;
      if (node.nodeName === "BR") { text += "\n"; return; }
      const block = blocks.has(node.nodeName);
      if (block && text && !text.endsWith("\n")) text += "\n";
      for (const child of node.childNodes) visit(child);
      if (block && text && !text.endsWith("\n")) text += "\n";
    }
    visit(selected.cloneContents());
    text = text.replace(/^\n|\n$/g, ""); if (text.trim()) parts.push(text);
  }
  return parts.join("\n\n");
}

export function createSelectionQuote({ container, owner, add, writable = () => true }) {
  const button = element("button", { className: "selection-quote-button", text: uiMessage("context.quoteSelection"), attrs: { type: "button" } });
  button.hidden = true; document.body.append(button);
  let captured = null, pending = 0;
  const listeners = [];
  function listen(target, name, fn, options) { target.addEventListener(name, fn, options); listeners.push(() => target.removeEventListener(name, fn, options)); }
  function hide() { clearTimeout(pending); pending = 0; button.hidden = true; captured = null; }
  const asElement = node => node?.nodeType === 1 ? node : node?.parentElement;
  function capture() {
    pending = 0;
    const selection = document.getSelection();
    if (!writable() || selection?.rangeCount !== 1 || selection.isCollapsed) { hide(); return; }
    const range = selection.getRangeAt(0);
    const first = asElement(range.startContainer), last = asElement(range.endContainer);
    const firstBody = first?.closest(".timeline-body"), lastBody = last?.closest(".timeline-body");
    // A range can span several messages, whose common ancestor is the whole
    // timeline. Validate both endpoints instead of requiring a single item.
    if (!firstBody || !lastBody || !container.contains(firstBody) || !container.contains(lastBody) ||
        first.closest("input,textarea,[contenteditable],button") || last.closest("input,textarea,[contenteditable],button")) { hide(); return; }
    const raw = selection.toString();
    const text = firstBody === lastBody || raw.length > 65535 ? raw : excerpt(range, container);
    if (!text.trim()) { hide(); return; }
    const rect = range.getBoundingClientRect(), scope = owner();
    if (!scope?.projectId || !scope?.sessionId || rect.bottom <= 0 || rect.top >= innerHeight) { hide(); return; }
    const source = first.closest("[data-source-event-id]")?.dataset.sourceEventId;
    captured = { text, scope: JSON.stringify(scope), context: newPromptContext("quote", t("context.quoteLabel"), {
      content: text, project: scope.projectId, session: scope.sessionId,
      ...(source && Number(source) > 0 ? { event: Number(source) } : {}) }) };
    button.hidden = false;
    positionAnchoredMenu(button, { getBoundingClientRect: () => ({ ...rect.toJSON(),
      top: Math.max(rect.top, 8), bottom: Math.min(rect.bottom, innerHeight - 8) }) },
    { side: "above", align: "start", gap: 6 });
  }
  function schedule() { clearTimeout(pending); pending = setTimeout(capture, 0); }
  button.addEventListener("pointerdown", event => event.preventDefault());
  button.addEventListener("click", () => {
    if (captured && writable() && captured.scope === JSON.stringify(owner()) && add(captured.context) !== false)
      document.getSelection()?.removeAllRanges();
    hide();
  });
  listen(document, "pointerup", event => { if (!button.contains(event.target)) schedule(); });
  listen(container, "keyup", event => { if (!isImeKey(event) && event.shiftKey) schedule(); });
  listen(document, "selectionchange", () => { if (document.getSelection()?.isCollapsed) hide(); else schedule(); });
  listen(document, "pointerdown", event => { if (!button.contains(event.target)) hide(); });
  listen(window, "scroll", hide, true); listen(window, "resize", hide);
  return { hide, destroy() { hide(); listeners.forEach(remove => remove()); button.remove(); } };
}
