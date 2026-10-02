import { createBackupImportController } from "./backup-import-controller.js";
import { subscribeLocale, t } from "../../i18n.js";
import { element, errorMessage } from "../../utils/dom.js";

export function createSessionBackupImport({ dialog, projectsStore, preferredProject,
  openSession, fallbackFocus = () => null, controller = createBackupImportController() }) {
  const node = (name) => dialog.querySelector(`[data-import-${name}]`);
  const title = node("title"), scope = node("scope"), status = node("status"), error = node("error");
  const progress = node("progress"), file = node("file"), fields = node("fields"), facts = node("facts");
  const project = node("project"), target = node("target"), closeButton = node("close");
  const primary = node("primary"), refresh = node("refresh"), cancel = node("cancel"), next = node("next");
  let opener = null, destroyed = false, opening = false, openingError = null, lastError = null, lastPhase = "";
  const copy = (key, params = {}) => t(`backupImport.${key}`, params);
  const size = (bytes) => (Number(bytes || 0) / 1048576).toFixed(2) + " MiB";
  function fillProjects() {
    const selected = project.value || preferredProject();
    const items = projectsStore.get().data?.items || [];
    project.replaceChildren(element("option", { text: copy("selectProject"), attrs: { value: "" } }),
      ...items.map((item) => element("option", { text: item.id === "default" ? t("nav.defaultProject", {}, "默认项目") :
        item.name || item.id, attrs: { value: item.id } })));
    project.value = items.some((item) => item.id === selected) ? selected : "";
    render();
  }
  function render() {
    if (destroyed) return;
    const state = controller.get(), result = state.restore, preview = state.preview?.result;
    title.textContent = copy("title"); scope.textContent = copy("scope");
    node("file-label").textContent = copy("file");
    node("project-label").textContent = copy("project");
    closeButton.setAttribute("aria-label", t("sessionAction.close", {}, "关闭"));
    next.textContent = copy("another"); refresh.textContent = copy("query");
    progress.setAttribute("aria-label", copy("progress"));
    const phase = state.phase;
    scope.hidden = !["choose", "hashing", "uploading", "inspecting", "preview"].includes(phase);
    status.textContent = copy(`phase.${phase}`, state.progress ? {
      received: size(state.progress.received), total: size(state.progress.total),
    } : {});
    if (result?.cancel_requested && !result.terminal) status.textContent += " " + copy("cancelPending");
    progress.hidden = !["hashing", "uploading", "inspecting", "restoring", "resolving", "reviewing"].includes(phase) || !state.busy;
    if (state.progress) { progress.max = state.progress.total; progress.value = state.progress.received; }
    else progress.removeAttribute("value");
    file.disabled = phase !== "choose" || state.busy;
    node("file-field").hidden = phase !== "choose";
    fields.hidden = phase !== "preview"; project.disabled = state.busy;
    facts.replaceChildren();
    const fact = (key, value) => {
      if (value === undefined || value === null || value === "") return;
      facts.append(element("dt", { text: copy(key) }), element("dd", { text: String(value) }));
    };
    if (result) {
      // Keep the reviewed destination ahead of source details, including on
      // short screens where the body must scroll independently of the footer.
      fact("targetProject", result.project_id); fact("targetWorkspace", result.workspace_root);
      if (result.project_revision !== undefined) fact("targetRevision", result.project_revision);
      fact("request", result.id);
      if (result.restart_required || result.cleanup_pending) fact("notice", copy("restart"));
      if (result.message) fact("diagnostic", result.message);
    }
    if (preview || result) {
      fact("sourceFile", state.fileName);
      fact("sourceSession", preview?.session_id || result?.source_session_id);
      fact("sourceTitle", preview?.title || result?.source_title);
      fact("sourceProject", preview?.project_id || result?.source_project_id);
      fact("sourceWorkspace", preview?.workspace_root || result?.source_workspace_root);
      fact("model", preview?.model_id || result?.source_model_id);
      fact("agent", preview?.agent_id || result?.source_agent_id);
      fact("protocol", preview?.protocol || result?.source_protocol);
      const captured = preview?.captured_at || result?.source_captured_at;
      if (captured) fact("captured", new Date(captured / 1000).toLocaleString());
      fact("files", preview?.file_count ?? result?.file_count);
      const totalBytes = preview?.total_bytes ?? result?.total_bytes;
      if (totalBytes !== undefined) fact("bytes", size(totalBytes));
      fact("messages", preview?.ui_records);
      fact("images", preview?.image_attachments);
      fact("unverified", preview?.unverified_history_references ?? result?.unverified_history_references);
      fact("sha256", result?.source_sha256 || state.preview?.sha256);
      if (preview?.legacy_partial) fact("notice", copy("partial"));
      else fact("notice", copy("retained"));
    }
    if (!result && state.preview?.message) fact("diagnostic", state.preview.message);
    facts.hidden = !facts.childElementCount;
    target.hidden = !["preview", "review", "restoring", "committed", "aborted", "unknown"].includes(phase);
    target.textContent = copy(phase === "unknown" ? "unknown" : phase === "review" ? "confirmation" : "staged");
    primary.hidden = !["preview", "review", "committed"].includes(phase);
    primary.textContent = copy(phase === "preview" ? "review" : phase === "review" ? "confirm" : "open");
    primary.disabled = state.busy || opening || result?.restart_required || (phase === "preview" && (!project.value || preview?.legacy_partial)) ||
      (phase === "committed" && (result.cleanup_pending || result.restart_required || !result.terminal));
    refresh.hidden = !(["failed", "unknown", "restoring", "inspecting"].includes(phase) || result?.restart_required || result?.cleanup_pending);
    refresh.disabled = state.busy || opening;
    const cancellable = result ? !result.terminal && !result.committed : Boolean(state.preview || state.upload || state.busy);
    cancel.hidden = !cancellable;
    cancel.disabled = opening || phase === "resolving" || phase === "reviewing";
    cancel.textContent = copy(phase === "review" && !state.attempted ? "changeTarget" : "cancel");
    next.hidden = !(result ? result.terminal && !result.cleanup_pending && !result.restart_required :
      phase === "failed" || phase === "preview");
    next.disabled = state.busy || opening;
    dialog.setAttribute("aria-busy", String(state.busy || opening));
    const failure = openingError || state.error;
    error.hidden = !failure;
    error.textContent = failure ? errorMessage(failure) : "";
    if (phase !== lastPhase) dialog.querySelector(".backup-import-body").scrollTop = 0;
    lastPhase = phase;
    if (failure && failure !== lastError && dialog.open) error.focus();
    lastError = failure;
  }
  function hide() {
    if (dialog.open) dialog.close();
    const focus = opener?.isConnected && !opener.disabled ? opener : fallbackFocus();
    opener = null;
    if (focus?.isConnected && !focus.disabled) focus.focus({ preventScroll: true });
  }
  async function primaryAction() {
    const state = controller.get();
    if (state.busy || opening || primary.disabled) return;
    if (state.phase === "preview") return controller.review(project.value);
    if (state.phase === "review") return controller.confirm();
    if (state.phase !== "committed") return;
    opening = true; openingError = null; render();
    try {
      // Navigation is explicit, so a completed import never replaces the
      // user's current unsent draft. Restored queue entries stay staged.
      await openSession(state.restore); hide();
    } catch (failure) { openingError = failure; }
    finally { opening = false; render(); }
  }
  function fileChanged() {
    const selected = file.files?.[0]; file.value = "";
    if (selected) { openingError = null; void controller.choose(selected); }
  }
  function query() { openingError = null; void controller.refresh(); }
  function cancelWork() { openingError = null; void controller.cancel(); }
  function another() {
    openingError = null;
    void (controller.get().restore ? controller.acknowledge() : controller.cancel());
  }
  function onEscape(event) { event.preventDefault(); hide(); }
  function onClose() { if (!dialog.open) hide(); }
  file.addEventListener("change", fileChanged); project.addEventListener("change", render);
  primary.addEventListener("click", primaryAction); refresh.addEventListener("click", query);
  cancel.addEventListener("click", cancelWork); next.addEventListener("click", another);
  closeButton.addEventListener("click", hide);
  dialog.addEventListener("cancel", onEscape); dialog.addEventListener("close", onClose);
  const unsubscribeState = controller.subscribe(render);
  const unsubscribeProjects = projectsStore.subscribe(fillProjects);
  const unsubscribeLocale = subscribeLocale(fillProjects);
  function open() {
    if (destroyed) return;
    if (!dialog.open) { opener = document.activeElement; dialog.showModal(); }
    render();
    if (controller.get().phase === "choose") void controller.inspect();
    (controller.get().error ? error : closeButton).focus({ preventScroll: true });
  }
  return Object.freeze({ open,
    resume() { if (controller.hasRecovery()) { open(); void controller.inspect(); } },
    destroy() {
      destroyed = true; hide(); controller.destroy();
      unsubscribeState(); unsubscribeProjects(); unsubscribeLocale();
      file.removeEventListener("change", fileChanged); project.removeEventListener("change", render);
      primary.removeEventListener("click", primaryAction); refresh.removeEventListener("click", query);
      cancel.removeEventListener("click", cancelWork); next.removeEventListener("click", another);
      closeButton.removeEventListener("click", hide);
      dialog.removeEventListener("cancel", onEscape); dialog.removeEventListener("close", onClose);
    },
  });
}
