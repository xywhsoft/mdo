// Call after the anchor is mounted and its scroll container has been restored.
// Both the anchor rectangle and fixed menu coordinates use the layout viewport.
export function positionAnchoredMenu(menu, anchor, { side = "below", align = "end",
  gap = 4, margin = 8, maxHeight = 480, width } = {}) {
  const win = menu.ownerDocument.defaultView, view = win.visualViewport;
  const left = view?.offsetLeft ?? 0, top = view?.offsetTop ?? 0;
  const viewWidth = view?.width ?? win.innerWidth, viewHeight = view?.height ?? win.innerHeight;
  const availableWidth = Math.max(0, viewWidth - margin * 2);
  const cap = Math.max(0, Math.min(maxHeight, viewHeight - margin * 2));
  const scrollTop = menu.scrollTop;
  Object.assign(menu.style, { right: "auto", bottom: "auto", left: `${left + margin}px`,
    top: `${top + margin}px`, maxWidth: `${availableWidth}px`, maxHeight: `${cap}px`, overflowY: "auto" });
  if (width !== undefined) menu.style.width = `${Math.min(width, availableWidth)}px`;
  const rect = anchor.getBoundingClientRect(), natural = menu.getBoundingClientRect();
  const above = Math.max(0, rect.top - top - margin - gap);
  const below = Math.max(0, top + viewHeight - margin - rect.bottom - gap);
  const preferredAbove = side === "above";
  const placeAbove = preferredAbove
    ? above >= natural.height || above >= below
    : below < natural.height && above > below;
  menu.style.maxHeight = `${Math.min(cap, placeAbove ? above : below)}px`;
  const box = menu.getBoundingClientRect();
  const clamp = (value, start, end) => Math.max(start, Math.min(value, Math.max(start, end)));
  menu.style.left = `${clamp(align === "start" ? rect.left : rect.right - box.width,
    left + margin, left + viewWidth - margin - box.width)}px`;
  menu.style.top = `${clamp(placeAbove ? rect.top - gap - box.height : rect.bottom + gap,
    top + margin, top + viewHeight - margin - box.height)}px`;
  menu.scrollTop = scrollTop;
}

// Details menus can be removed by a list refresh without a closing toggle.
// Observe removal so viewport listeners never retain detached rows.
export function trackAnchoredMenu(menu, anchor, options) {
  const win = menu.ownerDocument.defaultView;
  const update = () => positionAnchoredMenu(menu, anchor, options);
  const onScroll = event => { if (!menu.contains(event.target)) update(); };
  const observer = new win.MutationObserver(() => {
    if (!menu.isConnected || !anchor.isConnected) stop();
  });
  function stop() {
    observer.disconnect();
    win.removeEventListener("resize", update);
    win.removeEventListener("scroll", onScroll, true);
    win.visualViewport?.removeEventListener("resize", update);
    win.visualViewport?.removeEventListener("scroll", update);
  }
  win.addEventListener("resize", update);
  win.addEventListener("scroll", onScroll, true);
  win.visualViewport?.addEventListener("resize", update);
  win.visualViewport?.addEventListener("scroll", update);
  observer.observe(menu.ownerDocument.documentElement, { childList: true, subtree: true });
  update();
  return stop;
}
