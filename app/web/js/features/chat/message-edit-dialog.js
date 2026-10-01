import { t } from "../../i18n.js";
import { isImeKey } from "../../utils/dom.js";
import { createCompositionTracker } from "../../utils/composition.js";

export function createMessageEditDialog({ dialog, form, input, cancel }) {
  let pending = null;
  let hasAttachments = false;
  let opener = null;
  const composition = createCompositionTracker(input);

  function finish(value) {
    const resolve = pending;
    pending = null;
    const returnFocus = opener;
    opener = null;
    if (dialog.open) dialog.close();
    if (returnFocus?.isConnected && !returnFocus.disabled)
      returnFocus.focus({ preventScroll: true });
    resolve?.(value);
  }

  form.addEventListener("submit", (event) => {
    event.preventDefault();
    if (!pending) return;
    const value = input.value.trim();
    if (!value && !hasAttachments) {
      input.setCustomValidity(t("messageEdit.required", {}, "请输入消息内容"));
      input.reportValidity();
      return;
    }
    if (form.reportValidity()) finish(value);
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
  cancel.addEventListener("click", () => finish(null));
  dialog.addEventListener("close", () => {
    // Native close events are queued. A previous cancellation must not
    // resolve a newer edit that has already reopened this dialog.
    if (!dialog.open && pending) finish(null);
  });

  return Object.freeze({
    open(text, attachments = [], source = null) {
      if (pending) return Promise.reject(new Error(t("messageEdit.alreadyOpen", {}, "已有消息正在编辑")));
      hasAttachments = attachments.length > 0;
      opener = source;
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
