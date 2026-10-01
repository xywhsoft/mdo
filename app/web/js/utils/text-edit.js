// Native editor commands keep a completion in the textarea's undo history.
// Unsupported WebViews still receive the replacement through setRangeText.
export function replaceInputText(input, start, end, text) {
  if (input.disabled || input.readOnly) return false;
  const before = input.value;
  input.focus({ preventScroll: true });
  if (before.slice(start, end) === text) {
    input.setSelectionRange(start + text.length, start + text.length);
    return true;
  }
  const previous = [input.selectionStart, input.selectionEnd, input.selectionDirection];
  input.setSelectionRange(start, end);
  let notified = false;
  let beforeInput = null;
  const observe = () => { notified = true; };
  const observeBefore = (event) => { beforeInput = event; };
  input.addEventListener("input", observe);
  input.addEventListener("beforeinput", observeBefore);
  try {
    try { document.execCommand?.("insertText", false, text); }
    catch { /* Some WebViews expose the command but reject textarea edits. */ }
    if (beforeInput?.defaultPrevented) {
      input.setSelectionRange(...previous);
      return false;
    }
    // A command can report failure after making the edit. Only fall back
    // when it left the text unchanged, so the reference is never inserted twice.
    if (input.value === before)
      input.setRangeText(text, start, end, "end");
  } finally {
    input.removeEventListener("input", observe);
    input.removeEventListener("beforeinput", observeBefore);
  }
  // Native edits already notify draft saving and token estimation. The legacy
  // replacement needs the same notification, without a second native event.
  if (!notified) input.dispatchEvent(new Event("input", { bubbles: true }));
  return true;
}
