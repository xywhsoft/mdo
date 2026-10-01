// Track the actual editor, rather than a page-wide flag. A removed editor may
// never emit compositionend; its state must not suppress another field's keys.
export function createCompositionTracker(scope) {
  let targets = new WeakSet();
  const start = (event) => { if (event.target) targets.add(event.target); };
  const end = (event) => { targets.delete(event.target); };
  const reset = () => { targets = new WeakSet(); };
  const view = scope.defaultView ?? scope.ownerDocument?.defaultView;
  const capture = { capture: true };
  // Capture blur because it does not bubble. Capture composition too, before
  // a child widget can stop propagation while managing its own candidates.
  scope.addEventListener("compositionstart", start, capture);
  scope.addEventListener("compositionend", end, capture);
  scope.addEventListener("blur", end, capture);
  view?.addEventListener("blur", reset);
  return Object.freeze({
    isComposing: (target) => targets.has(target),
    dispose() {
      scope.removeEventListener("compositionstart", start, capture);
      scope.removeEventListener("compositionend", end, capture);
      scope.removeEventListener("blur", end, capture);
      view?.removeEventListener("blur", reset);
      reset();
    },
  });
}
