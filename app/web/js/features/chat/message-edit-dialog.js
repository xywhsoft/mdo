import { t } from "../../i18n.js";
import { errorMessage, isImeKey } from "../../utils/dom.js";
import { createCompositionTracker } from "../../utils/composition.js";

export function createMessageEditDialog({ dialog, form, input, cancel, submit, error }) {
  let pending = null;
  let hasAttachments = false;
  let opener = null;
  let commit = null;
  let committing = false;
  const composition = createCompositionTracker(input);

  function finish(value) {
    const resolve = pending;
    pending = null;
    commit = null;
    setCommitting(false);
    const returnFocus = opener;
    opener = null;
    if (dialog.open) dialog.close();
    if (returnFocus?.isConnected && !returnFocus.disabled)
      returnFocus.focus({ preventScroll: true });
    resolve?.(value);
  }

  function setCommitting(value) {
    committing = value;
    input.readOnly = value;
    cancel.disabled = value;
    if (submit) submit.disabled = value;
    form.setAttribute?.("aria-busy", String(value));
  }

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!pending || committing) return;
    const value = input.value.trim();
    if (!value && !hasAttachments) {
      input.setCustomValidity(t("messageEdit.required", {}, "请输入消息内容"));
      input.reportValidity();
      return;
    }
    if (!form.reportValidity()) return;
    if (!commit) { finish(value); return; }
    const request = pending;
    setCommitting(true);
    if (error) error.hidden = true;
    try {
      await commit(value);
      if (request !== pending) return;
      setCommitting(false);
      finish(value);
    } catch (failure) {
      if (request !== pending) return;
      // A conflict must keep the user's edit available. Never close and then
      // try to reconstruct it from a message another client already replaced.
      if (error) {
        error.textContent = errorMessage(failure);
        error.hidden = false;
        error.focus({ preventScroll: true });
      } else {
        input.setCustomValidity(errorMessage(failure));
        input.reportValidity();
      }
    } finally {
      if (request === pending) setCommitting(false);
    }
  });
  input.addEventListener("input", () => input.setCustomValidity(""));
  input.addEventListener("keydown", (event) => {
    if (!pending || event.defaultPrevented || input.disabled || input.readOnly ||
        event.key !== "Enter" || event.shiftKey || event.ctrlKey || event.metaKey ||
        event.altKey || isImeKey(event, composition.isComposing(input))) return;
    // Match the old editor's Enter-to-resend, using the same form validation
    // as the button. Shift+Enter remains the textarea's native line break.
    event.preventDefault();
    form.requestSubmit();
  });
  cancel.addEventListener("click", () => { if (!committing) finish(null); });
  dialog.addEventListener("cancel", (event) => {
    // Once the history transaction starts, cancellation cannot undo it.
    if (committing) event.preventDefault();
  });
  dialog.addEventListener("close", () => {
    // Native close events are queued. A previous cancellation must not
    // resolve a newer edit that has already reopened this dialog.
    if (!dialog.open && pending) finish(null);
  });

  return Object.freeze({
    open(text, attachments = [], source = null, onCommit = null) {
      if (pending) return Promise.reject(new Error(t("messageEdit.alreadyOpen", {}, "已有消息正在编辑")));
      hasAttachments = attachments.length > 0;
      opener = source;
      commit = onCommit;
      setCommitting(false);
      if (error) error.hidden = true;
      input.value = text;
      input.required = !hasAttachments;
      input.setCustomValidity("");
      dialog.showModal();
      input.focus();
      input.setSelectionRange(input.value.length, input.value.length);
      return new Promise((resolve) => { pending = resolve; });
    },
  });
}
