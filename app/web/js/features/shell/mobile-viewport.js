// Some mobile WebViews keep the layout viewport unchanged while the software
// keyboard shrinks the visual viewport. In that case 100dvh leaves the composer
// behind the keyboard; use the visible bottom edge for the app shell instead.
// Keep the full visual frame for top-layer dialogs, even if the viewport has
// panned so its bottom coincides with the layout viewport's bottom.
function visualViewportFrame(layoutHeight, viewport) {
  const height = Number(viewport?.height);
  const top = Number(viewport?.offsetTop);
  const scale = Number(viewport?.scale);
  if (!Number.isFinite(layoutHeight) || !Number.isFinite(height) ||
      !Number.isFinite(top) || !Number.isFinite(scale) ||
      Math.abs(scale - 1) > 0.01 || height <= 0 || top < 0 ||
      top >= layoutHeight) return null;
  const bottom = Math.min(layoutHeight, top + height);
  return layoutHeight - (bottom - top) >= 40
    ? { top: Math.round(top), height: Math.round(bottom - top),
      bottom: Math.round(bottom) } : null;
}

export function visibleViewportBottom(layoutHeight, viewport) {
  const frame = visualViewportFrame(layoutHeight, viewport);
  return frame && layoutHeight - frame.bottom >= 40 ? frame.bottom : null;
}

export function trackMobileViewport(shell, mobileLayout, win = window) {
  const viewport = win.visualViewport;
  const root = shell.ownerDocument?.documentElement;
  function sync() {
    const frame = mobileLayout.matches
      ? visualViewportFrame(win.innerHeight, viewport) : null;
    const bottom = frame && win.innerHeight - frame.bottom >= 40
      ? frame.bottom : null;
    if (bottom === null) shell.style.removeProperty("--app-visible-height");
    else shell.style.setProperty("--app-visible-height", `${bottom}px`);
    // CSS height media queries still see the layout viewport in these WebViews.
    // Match the compact composer used by truly short windows when the keyboard
    // leaves only a short visual viewport.
    shell.toggleAttribute("data-compact-visual-viewport",
      bottom !== null && bottom <= 300);
    // Native dialogs are placed in the document top layer, outside .app-shell.
    // Give them the same visible bounds so their actions stay above the keyboard.
    if (root) {
      root.toggleAttribute("data-visual-viewport-reduced", frame !== null);
      if (frame === null) {
        root.style.removeProperty("--app-visual-top");
        root.style.removeProperty("--app-visual-height");
      } else {
        root.style.setProperty("--app-visual-top", `${frame.top}px`);
        root.style.setProperty("--app-visual-height", `${frame.height}px`);
      }
    }
  }
  viewport?.addEventListener("resize", sync);
  viewport?.addEventListener("scroll", sync);
  win.addEventListener("resize", sync);
  mobileLayout.addEventListener("change", sync);
  sync();
}
