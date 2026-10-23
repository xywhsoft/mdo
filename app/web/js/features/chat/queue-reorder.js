// Stable ID anchors survive polling and avoid sending a stale full queue array.
export function queueMoveAnchor(ids, id, target, after = false) {
  if (id === target || !ids.includes(id) || !ids.includes(target)) return undefined;
  const rest = ids.filter(value => value !== id);
  const index = rest.indexOf(target) + (after ? 1 : 0);
  const before = rest[index] ?? "";
  const old = ids.indexOf(id), next = [...rest];
  next.splice(index, 0, id);
  return next[old] === id && next.every((value, at) => value === ids[at]) ? undefined : before;
}

export function createQueueReorder({ container, snapshot, onMove, onEnd }) {
  let drag = null;
  function end(commit = false) {
    if (!drag) return;
    const current = drag; drag = null;
    current.row.classList.remove("queue-dragging");
    container.querySelectorAll?.(".queue-drop-before, .queue-drop-after")
      .forEach(row => row.classList.remove("queue-drop-before", "queue-drop-after"));
    try { current.handle.releasePointerCapture?.(current.pointer); } catch { /* Pointer already released. */ }
    if (commit && current.moved && current.before !== undefined)
      void onMove(current.id, current.before, current.owner);
    onEnd?.();
  }
  container.addEventListener?.("pointerdown", event => {
    const handle = event.target.closest?.("[data-queue-drag]");
    if (!handle || handle.disabled || (event.button !== undefined && event.button !== 0)) return;
    const row = handle.closest("li"), owner = snapshot();
    if (!owner.ids.includes(row.dataset.queueItemId)) return;
    drag = { id: row.dataset.queueItemId, row, handle, owner,
      pointer: event.pointerId, start: event.clientY, moved: false };
    try { handle.setPointerCapture?.(event.pointerId); } catch { /* Synthetic or already cancelled pointer. */ }
    event.preventDefault();
  });
  container.addEventListener?.("pointermove", event => {
    if (!drag || event.pointerId !== drag.pointer) return;
    if (Math.abs(event.clientY - drag.start) < 6 && !drag.moved) return;
    drag.moved = true; drag.row.classList.add("queue-dragging");
    const rows = [...container.querySelectorAll("li[data-queue-item-id]")]
      .filter(row => drag.owner.ids.includes(row.dataset.queueItemId));
    const target = rows.reduce((nearest, row) => {
      const bounds = row.getBoundingClientRect();
      const distance = Math.abs(event.clientY - (bounds.top + bounds.height / 2));
      return !nearest || distance < nearest.distance ? { row, bounds, distance } : nearest;
    }, null);
    for (const row of rows) row.classList.remove("queue-drop-before", "queue-drop-after");
    if (target) {
      const after = event.clientY > target.bounds.top + target.bounds.height / 2;
      drag.before = queueMoveAnchor(drag.owner.ids, drag.id, target.row.dataset.queueItemId, after);
      if (drag.before !== undefined) target.row.classList.add(after ? "queue-drop-after" : "queue-drop-before");
    }
    const list = container.querySelector("ol"), bounds = list.getBoundingClientRect();
    if (event.clientY < bounds.top + 25) list.scrollTop -= 12;
    else if (event.clientY > bounds.bottom - 25) list.scrollTop += 12;
    event.preventDefault();
  });
  container.addEventListener?.("pointerup", event => { if (event.pointerId === drag?.pointer) end(true); });
  container.addEventListener?.("pointercancel", () => end());
  container.addEventListener?.("lostpointercapture", () => end());
  container.addEventListener?.("keydown", event => {
    const handle = event.target.closest?.("[data-queue-drag]");
    if (event.key === "Escape" && drag) { event.preventDefault(); event.stopPropagation(); end(); return; }
    if (!handle || handle.disabled || !["ArrowUp", "ArrowDown"].includes(event.key)) return;
    event.preventDefault(); event.stopPropagation();
    const owner = snapshot(), id = handle.closest("li").dataset.queueItemId;
    const index = owner.ids.indexOf(id), target = owner.ids[index + (event.key === "ArrowUp" ? -1 : 1)];
    const before = queueMoveAnchor(owner.ids, id, target, event.key === "ArrowDown");
    if (before !== undefined) void onMove(id, before, owner);
  });
  return { active: () => Boolean(drag), cancel: () => end() };
}
