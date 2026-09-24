export function createKeyboardShortcuts({ dialog, navigation, search, onNew,
  onExport, onSettings, onStop, isRunning, isDrawerOpen, closeDrawers }) {
  let previousFocus = null;

  function openHelp() {
    if (document.querySelector("dialog[open]")) return;
    previousFocus = document.activeElement;
    dialog.showModal();
    dialog.querySelector("button")?.focus();
  }

  dialog.addEventListener("close", () => {
    if (previousFocus?.isConnected) previousFocus.focus();
    previousFocus = null;
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      if (document.querySelector("dialog[open]")) return;
      if (search.isOpen()) { search.close(true); return; }
      if (navigation.get().view === "settings") { onSettings(false); return; }
      if (isDrawerOpen()) { closeDrawers(); return; }
      if (isRunning()) { event.preventDefault(); onStop(); }
      return;
    }

    if (document.querySelector("dialog[open]")) return;
    const modifier = event.ctrlKey || event.metaKey;
    const key = event.key.toLowerCase();
    if (modifier && !event.altKey && !event.shiftKey) {
      if (key === "k" || key === "n") {
        event.preventDefault();
        onNew();
      } else if (key === "f" && navigation.get().view === "workspace" &&
          navigation.get().sessionId) {
        event.preventDefault();
        search.open();
      } else if (key === "e" && navigation.get().view === "workspace" &&
          navigation.get().sessionId) {
        event.preventDefault();
        void onExport();
      } else if (event.key === ",") {
        event.preventDefault();
        onSettings(true);
      }
      return;
    }

    const editable = event.target instanceof Element &&
      event.target.closest("input, textarea, select, [contenteditable]");
    if (event.key === "?" && !editable && !event.altKey && !modifier) {
      event.preventDefault();
      openHelp();
    }
  });

  return Object.freeze({ openHelp });
}
