// Keep completion menus below the workspace header even when the composer
// grows or a software keyboard changes the visible viewport.
export function trackComposerMenuRoom(composer, win = window) {
  const sync = () => {
    const header = Number.parseFloat(win.getComputedStyle(win.document.documentElement)
      .getPropertyValue("--header-height")) || 0;
    const room = Math.max(0, Math.floor(composer.getBoundingClientRect().top - header - 16));
    composer.style.setProperty("--composer-menu-room", `${room}px`);
  };
  const observer = typeof win.ResizeObserver === "function"
    ? new win.ResizeObserver(sync) : null;
  observer?.observe(composer);
  if (composer.parentElement) observer?.observe(composer.parentElement);
  const shell = composer.closest(".app-shell");
  if (shell) observer?.observe(shell);
  win.addEventListener("resize", sync);
  win.visualViewport?.addEventListener("resize", sync);
  win.visualViewport?.addEventListener("scroll", sync);
  sync();
}
