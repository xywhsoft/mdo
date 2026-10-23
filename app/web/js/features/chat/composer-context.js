import { canAddPromptContext, newPromptContext, parsePromptContext, serializePromptContext } from "./prompt-context.js";
import { promptContextChips } from "./context-chips.js";
import { t } from "../../i18n.js";
import { toast } from "../../utils/dom.js";

export function createComposerContext({ input, strip, owner = () => "", onChange = () => {} }) {
  let contexts = [];
  function render() {
    const origin = owner();
    strip.replaceChildren(promptContextChips(contexts, { disabled: input.disabled || input.readOnly,
      onRemove(id) {
        if (input.disabled || input.readOnly || origin !== owner()) return;
        contexts = contexts.filter(item => item.id !== id); render(); onChange(); input.focus();
      } }));
    strip.hidden = contexts.length === 0;
  }
  function add(item, { start, end } = {}) {
    // Async reference loaders check their captured owner before calling add.
    // Direct user input belongs to the visible editor; its first restore may
    // predate initialization of the service's write token.
    if (input.disabled || input.readOnly) return false;
    const text = start === undefined ? input.value : input.value.slice(0, start) + input.value.slice(end);
    if (!canAddPromptContext(text, contexts, item)) { toast(t("context.limit"), "error"); return false; }
    if (start !== undefined) { input.value = text; input.setSelectionRange(start, start); }
    contexts = [...contexts, item]; render(); onChange(); return true;
  }
  input.addEventListener("paste", event => {
    if (event.defaultPrevented || input.disabled || input.readOnly || event.clipboardData?.files?.length) return;
    const text = event.clipboardData?.getData("text/plain") || "";
    if (text.length < 2000 && text.split("\n").length < 30) return;
    const item = newPromptContext("text", t("context.pastedText", { count: text.length }), { content: text });
    // Preserve native paste on overflow; the ordinary draft limit then keeps
    // the user's full text available instead of silently losing the paste.
    const range = { start: input.selectionStart, end: input.selectionEnd };
    const replaced = input.value.slice(0, range.start) + input.value.slice(range.end);
    if (!canAddPromptContext(replaced, contexts, item)) return;
    event.preventDefault(); add(item, range);
  });
  return Object.freeze({
    text: () => serializePromptContext(input.value, contexts),
    hasContexts: () => contexts.length > 0,
    contexts: () => [...contexts],
    restore(value) {
      const parsed = parsePromptContext(value); input.value = parsed.text;
      contexts = parsed.contexts; render();
    },
    add, render,
    clear() { input.value = ""; contexts = []; render(); },
  });
}
