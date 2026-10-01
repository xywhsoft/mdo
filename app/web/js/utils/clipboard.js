// WebViews do not all expose the asynchronous Clipboard API. Keep copying
// inside the click handler so the legacy path still has a user gesture.
export async function copyText(value) {
  const text = String(value);
  if (navigator.clipboard?.writeText) {
    try {
      await navigator.clipboard.writeText(text);
      return;
    } catch { /* Try the legacy clipboard path below. */ }
  }

  const previousFocus = document.activeElement;
  const selection = document.getSelection?.();
  const ranges = selection ? Array.from({ length: selection.rangeCount }, (_, index) => {
    const range = selection.getRangeAt(index);
    // Cloned Ranges are live too: removing text relocates them to its parent.
    // Retain the original endpoints so that relocation is not mistaken for
    // a surviving reader selection.
    return { range: range.cloneRange(), start: range.startContainer,
      end: range.endContainer };
  }) : [];
  const anchor = selection?.anchorNode;
  const anchorOffset = selection?.anchorOffset;
  const focus = selection?.focusNode;
  const focusOffset = selection?.focusOffset;
  const fieldSelection = typeof previousFocus?.selectionStart === "number"
    ? { start: previousFocus.selectionStart, end: previousFocus.selectionEnd,
        direction: previousFocus.selectionDirection,
        top: previousFocus.scrollTop, left: previousFocus.scrollLeft } : null;
  const input = document.createElement("textarea");
  input.value = text;
  input.readOnly = true;
  input.style.position = "fixed";
  input.style.opacity = "0";
  document.body.append(input);
  let copied = false;
  try {
    input.select();
    copied = document.execCommand("copy");
  } finally {
    input.remove();
    if (previousFocus?.isConnected) {
      try { previousFocus.focus({ preventScroll: true }); }
      catch { try { previousFocus.focus(); } catch { /* detached */ } }
    }
    // Selecting the temporary textarea replaces the reader's DOM selection.
    // Restore after focus so a WebView focus change cannot collapse it again.
    // This is synchronous; the snapshot is taken after any native API await.
    try {
      if (fieldSelection && previousFocus.isConnected) {
        previousFocus.setSelectionRange(fieldSelection.start, fieldSelection.end,
          fieldSelection.direction);
        previousFocus.scrollTop = fieldSelection.top;
        previousFocus.scrollLeft = fieldSelection.left;
      } else if (selection && ranges.length) {
        const connected = ranges.filter((entry) =>
          entry.start.isConnected && entry.end.isConnected);
        if (connected.length === 1 && ranges.length === 1 &&
            anchor?.isConnected && focus?.isConnected && selection.setBaseAndExtent) {
          // A Range alone loses the direction of a backwards selection.
          selection.setBaseAndExtent(anchor, anchorOffset, focus, focusOffset);
        } else if (connected.length) {
          selection.removeAllRanges();
          for (const entry of connected) selection.addRange(entry.range);
        }
      }
    } catch { /* A copy event may replace the selected nodes. */ }
  }
  if (!copied) throw new Error("clipboard unavailable");
}
