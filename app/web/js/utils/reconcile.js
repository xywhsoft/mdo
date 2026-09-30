export function reconcileCards(root, nodes) {
  const kept = new Set(nodes);
  for (const child of [...root.children]) {
    if (!kept.has(child)) child.remove();
  }
  const focused = nodes.find((node) =>
    node.parentElement === root && node.contains(document.activeElement));
  const selection = document.getSelection();
  const selected = selection && !selection.isCollapsed ? nodes.find((node) =>
    node.parentElement === root && node.contains(selection.anchorNode) &&
      node.contains(selection.focusNode)) : null;
  const anchor = focused ?? selected;
  if (anchor) {
    // Move surrounding nodes instead of detaching an active editor or the
    // text row the user is reading.
    const index = nodes.indexOf(anchor);
    let next = anchor;
    for (let i = index - 1; i >= 0; --i) {
      const node = nodes[i];
      if (node.nextElementSibling !== next) root.insertBefore(node, next);
      next = node;
    }
    let previous = anchor;
    for (let i = index + 1; i < nodes.length; ++i) {
      const node = nodes[i];
      if (previous.nextElementSibling !== node)
        root.insertBefore(node, previous.nextElementSibling);
      previous = node;
    }
    return;
  }
  for (const [index, node] of nodes.entries()) {
    const current = root.children[index];
    if (current === node) continue;
    // A surviving card stays mounted when an earlier card disappears.
    root.insertBefore(node, current ?? null);
  }
}
