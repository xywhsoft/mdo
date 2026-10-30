import { setUiText as writeUiText, uiMessage, t, subscribeLocale } from "../../i18n.js";

import { errorMessage } from "../../utils/dom.js";
import { reviewedPurgeIntent } from "./project-purge-contract.js";

// Confirmation stays separate from the advisory inventory and durable request
// state. Merely opening or refreshing either dialog can never execute removal.
export function createProjectPurgeConfirmation({ dialog, recovery, onResolved }) {
  const form = dialog.querySelector("form");
  const input = form.querySelector("input");
  const submit = form.querySelector('[type="submit"]');
  const cancel = form.querySelector('[data-purge-confirm="cancel"]');
  const error = form.querySelector('[role="alert"]');
  let preview = null;
  let origin = null;
  let working = false;

  function render() {
    if (!preview) return;
    writeUiText(dialog.querySelector("h3"), uiMessage("purgeConfirm.title", { name: preview.name }));
    writeUiText(dialog.querySelector('[data-purge-confirm="description"]'), uiMessage("purgeConfirm.description",
      { sessions: preview.session_count, schedules: preview.schedule_count,
        files: preview.file_count, directories: preview.directory_count }));
    writeUiText(dialog.querySelector("label span"), uiMessage("purgeConfirm.typeId", { id: preview.id }));
    writeUiText(cancel, uiMessage("purgeConfirm.cancel"));
    writeUiText(submit, working ? uiMessage("purgeConfirm.working") : uiMessage("purgeConfirm.execute"));
    cancel.disabled = input.disabled = working;
    submit.disabled = working || recovery.get().busy || input.value !== preview.id;
    dialog.setAttribute("aria-busy", String(working));
  }

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (working || recovery.get().busy || !preview || input.value !== preview.id) return;
    working = true; error.hidden = true; render();
    try {
      if (await recovery.prepare(preview)) await recovery.execute(preview);
      const state = recovery.get();
      if (state.result?.committed || ["pending", "aborted"].includes(state.result?.outcome)) {
        origin = null;
        dialog.close(); onResolved();
      } else if (state.error) {
        writeUiText(error, errorMessage(state.error)); error.hidden = false; error.focus();
      }
    } finally {
      working = false; render();
      if (dialog.open && (document.activeElement === document.body || document.activeElement === submit))
        (error.hidden ? cancel : error).focus();
    }
  });
  input.addEventListener("input", render);
  cancel.addEventListener("click", () => { if (!working) dialog.close(); });
  dialog.addEventListener("cancel", (event) => { if (working) event.preventDefault(); });
  dialog.addEventListener("close", () => {
    const previous = origin; preview = null; origin = null;
    if (previous?.isConnected) previous.focus();
  });
  recovery.subscribe(render);
  subscribeLocale(render);
  return Object.freeze({
    open(data, returnFocus) {
      reviewedPurgeIntent(data, "0".repeat(32));
      preview = Object.freeze({ ...data }); origin = returnFocus;
      input.value = ""; error.hidden = true;
      render(); dialog.showModal(); cancel.focus();
    },
  });
}
