// Consume a keyed activation once. Routine reports and content edits preserve
// the user's selected tab; a presentation request does not invalidate mounts.
export function panelActivationRequested(row, previous) {
  return row?.reveal === "activate" && (!previous ||
    Number.isSafeInteger(row.reveal_revision) && row.reveal_revision > 0 &&
    row.reveal_revision > (Number.isSafeInteger(previous.reveal_revision) ? previous.reveal_revision : 0));
}
