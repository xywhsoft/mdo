export function createMessageEditDialog({ dialog, form, input, cancel }) {
  let pending = null;

  function finish(value) {
    const resolve = pending;
    pending = null;
    if (dialog.open) dialog.close();
    resolve?.(value);
  }

  form.addEventListener("submit", (event) => {
    event.preventDefault();
    if (!pending || !form.reportValidity()) return;
    const value = input.value.trim();
    if (value) finish(value);
    else {
      input.setCustomValidity("请输入消息内容");
      input.reportValidity();
    }
  });
  input.addEventListener("input", () => input.setCustomValidity(""));
  cancel.addEventListener("click", () => finish(null));
  dialog.addEventListener("close", () => {
    if (pending) finish(null);
  });

  return Object.freeze({
    open(text) {
      if (pending) return Promise.reject(new Error("已有消息正在编辑"));
      input.value = text;
      input.setCustomValidity("");
      dialog.showModal();
      input.focus();
      input.setSelectionRange(input.value.length, input.value.length);
      return new Promise((resolve) => { pending = resolve; });
    },
  });
}
