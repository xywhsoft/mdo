// Some mobile WebViews keep the layout viewport unchanged while the software
// keyboard shrinks the visual viewport. In that case 100dvh leaves the composer
// behind the keyboard; use the visible bottom edge for the app shell instead.
export function visibleViewportBottom(layoutHeight, viewport) {
  const height = Number(viewport?.height);
  const top = Number(viewport?.offsetTop);
  const scale = Number(viewport?.scale);
  if (!Number.isFinite(layoutHeight) || !Number.isFinite(height) ||
      !Number.isFinite(top) || !Number.isFinite(scale) ||
      Math.abs(scale - 1) > 0.01 || height <= 0 || top < 0) return null;
  const bottom = Math.max(0, Math.min(layoutHeight, top + height));
  return layoutHeight - bottom >= 40 ? Math.round(bottom) : null;
}

export function trackMobileViewport(shell, mobileLayout, win = window) {
  const viewport = win.visualViewport;
  function sync() {
    const bottom = mobileLayout.matches
      ? visibleViewportBottom(win.innerHeight, viewport) : null;
    if (bottom === null) shell.style.removeProperty("--app-visible-height");
    else shell.style.setProperty("--app-visible-height", `${bottom}px`);
    // CSS height media queries still see the layout viewport in these WebViews.
    // Match the compact composer used by truly short windows when the keyboard
    // leaves only a short visual viewport.
    shell.toggleAttribute("data-compact-visual-viewport",
      bottom !== null && bottom <= 300);
  }
  viewport?.addEventListener("resize", sync);
  viewport?.addEventListener("scroll", sync);
  win.addEventListener("resize", sync);
  mobileLayout.addEventListener("change", sync);
  sync();
}
