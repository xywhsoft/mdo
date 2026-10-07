import { subscribeLocale, t } from "../../i18n.js";
import { errorMessage, toast } from "../../utils/dom.js";

function statusCopy(state) {
  if (state.writeConflictReason === "restart" && !state.intent) return t("shell.serviceRestarted");
  if (state.writeConflict && !state.intent) return t("purgeRecovery.stalePage");
  if (!state.checked) return t("purgeRecovery.loading");
  if (!state.intent) return state.error ? t("purgeRecovery.unavailable") : "";
  if (state.result?.committed) return t(state.result.restart_required ? "purgeConfirm.committedRestart" : "purgeRecovery.committed");
  if (state.result?.outcome === "aborted") return t("purgeRecovery.aborted");
  if (state.result?.outcome === "pending") return t("purgeRecovery.pending");
  if (state.result?.outcome === "not_accepted") return t("purgeRecovery.notAccepted");
  return t("purgeRecovery.unknown");
}

// Stable nodes keep keyboard focus while a query, locale change, or another
// page changes the result. No filesystem names are interpreted as markup.
export function createProjectPurgeRecoveryPanel({ panel, notice, recovery, navigation, dialog,
  unsentSnapshots = () => [], onReview = () => {} }) {
  const query = panel.querySelector('[data-purge-action="query"]');
  const cancel = panel.querySelector('[data-purge-action="cancel"]');
  const acknowledge = panel.querySelector('[data-purge-action="acknowledge"]');
  const reload = panel.querySelector('[data-purge-action="reload"]');
  const copy = panel.querySelector('[data-purge-action="copy"]');
  const review = panel.querySelector('[data-purge-action="review"]');
  const complete = panel.querySelector('[data-purge-action="complete"]');
  const error = panel.querySelector('[data-purge-field="error"]');
  const binding = panel.querySelector("dl");
  const jump = notice.querySelector("button");
  const fallbackFocus = () => document.querySelector(navigation.get().view === "settings"
    ? "#settings-title" : "#new-session")?.focus({ preventScroll: true });

  function focus() {
    if (!recovery.isPaused()) { fallbackFocus(); return; }
    if (!dialog.open) dialog.showModal();
    render();
    panel.querySelector("h3").focus({ preventScroll: true });
    panel.scrollIntoView({ block: "nearest" });
  }

  function render(state = recovery.get()) {
    const restarted = state.writeConflictReason === "restart" && !state.intent;
    const paused = recovery.isPaused();
    const active = document.activeElement;
    panel.hidden = !paused;
    panel.setAttribute("aria-busy", String(state.busy));
    if (!paused && dialog.open) dialog.close();
    notice.hidden = !paused || dialog.open;
    notice.querySelector("span").textContent = t(restarted ? "shell.serviceRestarted" : "purgeRecovery.notice");
    jump.textContent = t(restarted ? "devices.retry" : "purgeRecovery.open");
    panel.querySelector("h3").textContent = t(restarted ? "shell.serviceRestartedTitle" : "purgeRecovery.title");
    panel.querySelector('[data-purge-field="description"]').textContent = t(restarted
      ? "shell.serviceRestartedDescription" : "purgeRecovery.description");
    panel.querySelector('[data-purge-field="status"]').textContent = statusCopy(state);
    binding.hidden = !state.intent;
    for (const field of ["name", "project_id", "revision", "purge_request_id"])
      panel.querySelector(`[data-purge-field="${field}"]`).textContent = String(state.intent?.[field] ?? "");
    for (const field of ["name", "project_id", "revision", "purge_request_id"])
      panel.querySelector(`[data-purge-label="${field}"]`).textContent = t(`purgeRecovery.${field}`);
    error.hidden = !state.error || (restarted && state.error.code === "service_restarted");
    error.textContent = state.error ? errorMessage(state.error) : "";
    query.textContent = state.busy ? t("purgeRecovery.working") : t("purgeRecovery.query");
    cancel.textContent = t("purgeRecovery.cancel");
    acknowledge.textContent = t("purgeRecovery.acknowledge");
    query.disabled = state.busy;
    cancel.hidden = !state.intent || state.result?.committed ||
      ["pending", "aborted"].includes(state.result?.outcome);
    cancel.disabled = state.busy;
    acknowledge.hidden = state.result?.outcome !== "aborted";
    acknowledge.disabled = state.busy;
    reload.textContent = t("purgeRecovery.reload");
    copy.textContent = t("purgeRecovery.copyDrafts");
    reload.hidden = copy.hidden = !(state.writeConflict || state.error?.code === "purge_draft_unsaved" ||
      (state.intent && !state.intentSaved));
    reload.disabled = copy.disabled = state.busy;
    review.textContent = t("purgeConfirm.review");
    review.hidden = !state.intent || !state.intentSaved || state.result?.outcome !== "not_accepted" || state.writeConflict;
    review.disabled = state.busy;
    complete.textContent = t("purgeConfirm.complete");
    complete.hidden = state.result?.accepted !== true || state.result?.outcome !== "committed" ||
      !state.result?.committed || state.result?.restart_required;
    complete.disabled = state.busy;
    // If the clicked action disappears, return to the surviving query button.
    // Do not steal focus from a user's newer choice elsewhere on the page.
    if (active && panel.contains(active) && (panel.hidden || active.hidden)) {
      if (panel.hidden) fallbackFocus();
      else query.focus();
    }
  }

  async function act(origin, operation) {
    await operation();
    // Disabling the clicked button while awaiting HTTP may move focus to
    // body before render sees it. Recover it after the operation settles.
    if (document.activeElement === document.body || document.activeElement === origin) {
      if (panel.hidden) fallbackFocus();
      else (origin.hidden ? query : origin).focus();
    }
  }
  query.addEventListener("click", () => { void act(query, recovery.refresh); });
  cancel.addEventListener("click", () => { void act(cancel, recovery.cancel); });
  acknowledge.addEventListener("click", () => { void act(acknowledge, recovery.acknowledgeAbort); });
  reload.addEventListener("click", () => recovery.reload());
  review.addEventListener("click", () => {
    dialog.close();
    onReview(recovery.get().intent, jump);
  });
  complete.addEventListener("click", () => { void act(complete, recovery.completeCommitted); });
  copy.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(JSON.stringify(unsentSnapshots(), null, 2));
      toast(t("purgeRecovery.copiedDrafts"));
    } catch (cause) { toast(errorMessage(cause), "error"); }
  });
  jump.addEventListener("click", focus);
  dialog.querySelector("#project-purge-recovery-close").addEventListener("click", () => dialog.close());
  dialog.addEventListener("close", () => { if (!dialog.open) render(); });
  recovery.subscribe(render);
  subscribeLocale(() => render());
  navigation.subscribe(() => render());
  window.addEventListener("focus", () => { void recovery.refresh(); });
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "visible") void recovery.refresh();
  });
  return Object.freeze({ focus });
}
