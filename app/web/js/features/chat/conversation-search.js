export function createConversationSearch({ bar, input, count, openButtons, closeButton,
  navigation, prompt, onQuery }) {
  let available = false;

  function close(restoreFocus = false) {
    if (bar.hidden) return;
    bar.hidden = true;
    input.value = "";
    onQuery("");
    count.textContent = "";
    for (const button of openButtons) button.setAttribute("aria-expanded", "false");
    if (restoreFocus) prompt.focus();
  }

  function open() {
    if (!available) return;
    bar.hidden = false;
    for (const button of openButtons) button.setAttribute("aria-expanded", "true");
    input.focus();
    input.select();
    if (!input.value) count.textContent = "仅搜索已加载的记录";
  }

  for (const button of openButtons) button.addEventListener("click", open);
  closeButton.addEventListener("click", () => close(true));
  input.addEventListener("input", () => onQuery(input.value));
  input.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
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

  return Object.freeze({
    open,
    close,
    isOpen: () => !bar.hidden,
    setCount(value, historyLost) {
      if (bar.hidden) return;
      if (!input.value.trim()) {
        count.textContent = "仅搜索已加载的记录";
        return;
      }
      count.textContent = historyLost
        ? `${value} 处 · 早期记录未载入` : `${value} 处 · 已加载记录`;
    },
  });
}
