// Scroll state belongs to a visible content pane, not its replaced DOM node.
// Two pixels cover fractional/rounded scroll metrics in embedded WebViews.
export function captureStreamScroll(node) {
  if (!visiblePane(node)) return null;
  return { top: node.scrollTop, left: node.scrollLeft,
    followTail: node.scrollHeight - node.clientHeight - node.scrollTop <= 2 };
}
function visiblePane(node) {
  // Chromium may retain geometry for descendants of a closed <details>.
  // Their zeroed scroll offsets are not a new reading position.
  return node.clientHeight > 0 && !node.closest?.("details:not([open]),[hidden],[inert]");
}

export function createStreamScroll() {
  let positions = new Map();
  function captureNode(node) {
    const key = node?.dataset?.streamScroll;
    if (!key) return;
    const position = captureStreamScroll(node);
    if (position) positions.set(key, position);
  }
  return {
    captureNode,
    capture(root) { for (const node of root.querySelectorAll("[data-stream-scroll]")) captureNode(node); },
    restore(root) {
      for (const node of root.querySelectorAll("[data-stream-scroll]")) {
        if (!visiblePane(node)) continue;
        const position = positions.get(node.dataset.streamScroll);
        if (!position) { captureNode(node); continue; }
        node.scrollTop = position.followTail ? node.scrollHeight : position.top;
        node.scrollLeft = position.left;
      }
    },
    retain(keys) { for (const key of positions.keys()) if (!keys.has(key)) positions.delete(key); },
    snapshot() { return new Map(positions); },
    reset(saved) { positions = new Map(saved); },
  };
}
