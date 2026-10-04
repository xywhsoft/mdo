import { clearSelectedTask, loadTasks } from "../../state/tasks.js";
import { createTaskPanel } from "./task-panel.js";

// Keep operational task inspection out of the persistent chat layout.
// Native dialog supplies focus trapping and Escape dismissal.
export function createTaskDialog({ dialog, closeButton, navigation, fallbackFocus,
  ...panelOptions }) {
  const destroyPanel = createTaskPanel(panelOptions);
  let owner = "";

  function routeKey(route) {
    return `${route.view}/${route.projectId || ""}/${route.sessionId || ""}`;
  }
  function close() {
    if (dialog.open) dialog.close();
  }
  function onClose() {
    // A queued close event from a quick reopen must not clear the new view.
    if (dialog.open) return;
    clearSelectedTask();
    const active = document.activeElement;
    if (active === document.body || !active?.isConnected ||
        active.closest("[hidden], [inert]") || !active.getClientRects().length)
      fallbackFocus()?.focus({ preventScroll: true });
  }
  closeButton.addEventListener("click", close);
  dialog.addEventListener("close", onClose);
  const unsubscribe = navigation.subscribe((route) => {
    if (dialog.open && routeKey(route) !== owner) close();
  });

  return Object.freeze({
    open() {
      owner = routeKey(navigation.get());
      if (!dialog.open) dialog.showModal();
      closeButton.focus({ preventScroll: true });
      void loadTasks();
    },
    destroy() {
      close();
      clearSelectedTask();
      unsubscribe();
      destroyPanel();
      closeButton.removeEventListener("click", close);
      dialog.removeEventListener("close", onClose);
    },
  });
}
