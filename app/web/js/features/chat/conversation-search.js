import { subscribeLocale, t } from "../../i18n.js";
import { isImeKey } from "../../utils/dom.js";

export function createConversationSearch({ bar, input, count, openButtons, closeButton,
  navigation, prompt, onQuery, onOpen, onClose }) {
  let available = false;
  let matchCount = 0;
  let olderHistoryMissing = false;
  let composing = false;

  function renderCount() {
    if (bar.hidden) return;
    count.textContent = !input.value.trim() ? t("search.loadedOnly") :
      t(olderHistoryMissing ? "search.resultsTrimmed" : "search.resultsLoaded",
        { count: matchCount });
  }

  function close(restoreFocus = false) {
    if (bar.hidden) return;
    bar.hidden = true;
    composing = false;
    input.value = "";
    onQuery("");
    count.textContent = "";
    for (const button of openButtons) button.setAttribute("aria-expanded", "false");
    onClose?.();
    if (restoreFocus) prompt.focus();
  }

  function open() {
    if (!available) return;
    // Drawers can cover the search field and retain the keyboard focus loop.
    // Reveal the conversation before moving focus into it.
    onOpen?.();
    bar.hidden = false;
    for (const button of openButtons) button.setAttribute("aria-expanded", "true");
    input.focus();
    input.select();
    renderCount();
  }

  for (const button of openButtons) button.addEventListener("click", open);
  closeButton.addEventListener("click", () => close(true));
  input.addEventListener("input", () => onQuery(input.value));
  input.addEventListener("compositionstart", () => { composing = true; });
  input.addEventListener("compositionend", () => { composing = false; });
  input.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && !event.defaultPrevented &&
        !isImeKey(event, composing)) {
      event.preventDefault();
      event.stopPropagation();
      close(true);
    }
  });
  navigation.subscribe(({ view, sessionId }) => {
    available = view === "workspace" && Boolean(sessionId);
    for (const button of openButtons) button.disabled = !available;
    close();
  });
  subscribeLocale(renderCount);

  return Object.freeze({
    open,
    close,
    isOpen: () => !bar.hidden,
    setCount(value, historyLost) {
      matchCount = value;
      olderHistoryMissing = Boolean(historyLost);
      renderCount();
    },
  });
}
