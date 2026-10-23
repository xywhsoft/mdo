import { t } from "../../i18n.js";
import { errorMessage, isImeKey } from "../../utils/dom.js";
import { createCompositionTracker } from "../../utils/composition.js";

export function createMessageEditDialog({ dialog, form, input, cancel, submit, error,
  getText = () => input.value, restoreText = value => { input.value = value; },
  onKeyDown = () => false, prepareText = null }) {
  let pending = null;
  let hasAttachments = false;
  let opener = null;
  let commit = null;
  let fallbackFocus = null;
  let committing = false, leaveRequested = false;
  let host = null, hiddenNodes = [], observer = null, onDraft = null, beforeLeave = null;
  const composition = createCompositionTracker(input);

  function finish(value) {
    const resolve = pending;
    pending = null;
    leaveRequested = false;
    observer?.disconnect(); observer = null;
    if (host) {
      form.classList.remove("message-inline-editor");
      dialog.append(form); delete host.dataset.inlineEditing;
      for (const [node, hidden] of hiddenNodes) node.hidden = hidden;
      host = null; hiddenNodes = [];
    }
    onDraft = beforeLeave = null;
    commit = null;
    setCommitting(false);
    const returnFocus = opener;
    const fallback = fallbackFocus;
    opener = null;
    fallbackFocus = null;
    if (dialog.open) dialog.close();
    const target = returnFocus?.isConnected && !returnFocus.disabled
      ? returnFocus : fallback?.();
    if (target?.isConnected && !target.disabled)
      target.focus({ preventScroll: true });
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
    let value = getText().trim();
    if (!value && !hasAttachments) {
      input.setCustomValidity(t("messageEdit.required", {}, "请输入消息内容"));
      input.reportValidity();
      return;
    }
    input.required = !hasAttachments && !value;
    if (!form.reportValidity()) return;
    if (!commit) { finish(value); return; }
    const request = pending;
    setCommitting(true);
    if (error) error.hidden = true;
    try {
      if (prepareText && await prepareText()) {
        if (request === pending) { setCommitting(false); onDraft?.(getText()); input.focus(); }
        return;
      }
      value = getText().trim();
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
      if (request === pending) {
        setCommitting(false);
        if (leaveRequested || (host && !host.isConnected)) { beforeLeave?.(getText()); finish(null); }
      }
    }
  });
  input.addEventListener("input", () => { input.setCustomValidity(""); if (pending && !committing) onDraft?.(getText()); });
  input.addEventListener("keydown", (event) => {
    if (pending && onKeyDown(event)) return;
    if (pending && host && event.key === "Escape" && !committing && !isImeKey(event, composition.isComposing(input))) {
      event.preventDefault(); event.stopPropagation(); finish(null); return;
    }
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
    if (!dialog.open && pending && !host) finish(null);
  });

  return Object.freeze({
    open(text, attachments = [], source = null, options = {}) {
      if (pending) return Promise.reject(new Error(t("messageEdit.alreadyOpen", {}, "已有消息正在编辑")));
      hasAttachments = attachments.length > 0;
      leaveRequested = false;
      opener = source;
      commit = options.onCommit ?? null;
      fallbackFocus = options.fallbackFocus ?? null;
      onDraft = options.onDraft ?? null;
      beforeLeave = options.onDetach ?? null;
      setCommitting(false);
      if (error) error.hidden = true;
      restoreText(text);
      input.required = !hasAttachments && !getText().trim();
      input.setCustomValidity("");
      host = options.host?.isConnected ? options.host : null;
      if (host) {
        hiddenNodes = [...host.children].filter(node => node.matches(".timeline-body,.timeline-actions,.timeline-images"))
          .map(node => [node, node.hidden]);
        for (const [node] of hiddenNodes) node.hidden = true;
        host.dataset.inlineEditing = "true"; form.classList.add("message-inline-editor"); host.append(form);
        observer = new MutationObserver(() => {
          if (pending && host && !host.isConnected && !committing) { beforeLeave?.(getText()); finish(null); }
        });
        observer.observe(document.body, { childList: true, subtree: true });
      } else dialog.showModal();
      input.focus();
      input.setSelectionRange(input.value.length, input.value.length);
      return new Promise((resolve) => { pending = resolve; });
    },
    text: getText,
    cancel() {
      if (!pending) return;
      if (committing) { leaveRequested = true; return; }
      beforeLeave?.(getText()); finish(null);
    },
  });
}
