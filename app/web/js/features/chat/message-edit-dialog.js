import { t } from "../../i18n.js";

export function createMessageEditDialog({ dialog, form, input, cancel }) {
  let pending = null;
  let hasAttachments = false;

  function finish(value) {
    const resolve = pending;
    pending = null;
    if (dialog.open) dialog.close();
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
  cancel.addEventListener("click", () => finish(null));
  dialog.addEventListener("close", () => {
    if (pending) finish(null);
  });

  return Object.freeze({
    open(text, attachments = []) {
      if (pending) return Promise.reject(new Error(t("messageEdit.alreadyOpen", {}, "已有消息正在编辑")));
      hasAttachments = attachments.length > 0;
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
