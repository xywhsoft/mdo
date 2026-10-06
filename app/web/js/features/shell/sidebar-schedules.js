// A sidebar mode, not a workspace route: the conversation and its draft stay
// selected while schedules replace the project and session navigation.
export function createSidebarSchedules({ panel, button, closeButton,
  sessionNavigation, search, schedulePanel, showSidebar }) {
  function open() {
    showSidebar();
    search.hidden = true;
    sessionNavigation.hidden = true;
    panel.hidden = false;
    button.setAttribute("aria-expanded", "true");
    schedulePanel.setActive(true);
    panel.querySelector("h2").focus({ preventScroll: true });
  }
  function close({ focus = true } = {}) {
    const restoreFocus = panel.contains(document.activeElement);
    panel.hidden = true;
    search.hidden = false;
    sessionNavigation.hidden = false;
    button.setAttribute("aria-expanded", "false");
    schedulePanel.setActive(false);
    if (focus && restoreFocus) button.focus({ preventScroll: true });
  }
  button.addEventListener("click", () => panel.hidden ? open() : close());
  closeButton.addEventListener("click", () => close());
  panel.addEventListener("keydown", (event) => {
    if (event.key !== "Escape" || event.isComposing ||
        event.target.closest("dialog")) return;
    event.preventDefault();
    close();
  });
  return Object.freeze({ open, close, isOpen: () => !panel.hidden });
}
