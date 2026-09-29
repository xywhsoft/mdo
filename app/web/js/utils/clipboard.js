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
  }
  if (!copied) throw new Error("clipboard unavailable");
}
