import { exportSession } from "../../state/sessions.js";
import { subscribeLocale, t } from "../../i18n.js";
import { errorMessage, toast } from "../../utils/dom.js";

export function saveBackupFile(file) {
  const url = URL.createObjectURL(file.blob);
  try {
    const link = document.createElement("a");
    link.href = url; link.download = file.filename;
    document.body.append(link); link.click(); link.remove();
  } finally {
    // Give the browser download time to adopt its own reference to the Blob.
    window.setTimeout(() => URL.revokeObjectURL(url), 60000);
  }
}

export function createSessionBackupExport({ dialog, download = exportSession,
  save = saveBackupFile, notify = toast, fallbackFocus = () => null }) {
  const title = dialog.querySelector("[data-backup-title]");
  const scope = dialog.querySelector("[data-backup-scope]");
  const status = dialog.querySelector("[data-backup-status]");
  const progress = dialog.querySelector("progress");
  const error = dialog.querySelector("[data-backup-error]");
  const cancel = dialog.querySelector("[data-backup-cancel]");
  const closeButton = dialog.querySelector("[data-backup-close]");
  const retry = dialog.querySelector("[data-backup-retry]");
  let job = null, opener = null, destroyed = false;

  function render() {
    if (!job) return;
    title.textContent = t("backupExport.title", { title: job.session.title ||
      t("sessionAction.untitled", {}, "未命名任务") }, "导出“{title}”");
    scope.textContent = t("backupExport.scope", {},
      "包含已保存且仍保留的模型记录、消息、附件、产物、待办、反馈、草稿和队列。已清理的记录和未保存的输入不在备份中。");
    closeButton.setAttribute("aria-label", t("sessionAction.close", {}, "关闭"));
    cancel.textContent = t(job.failure ? "project.close" : "sessionAction.cancel", {}, job.failure ? "关闭" : "取消");
    retry.textContent = t("backupExport.retry", {}, "重新导出");
    retry.hidden = !job.failure; error.hidden = !job.failure;
    if (job.failure) error.textContent = errorMessage(job.failure);
    const state = job.progress;
    const phase = job.failure ? "failed" : state.phase;
    const bytes = (value) => (value / 1048576).toFixed(2);
    status.textContent = phase === "receiving"
      ? t("backupExport.receiving", { received: bytes(state.received), total: bytes(state.total) },
        "正在接收 {received} / {total} MiB…")
      : t(`backupExport.${phase}`, {}, phase === "verifying" ? "正在校验备份…" :
        phase === "failed" ? "备份尚未导出" : "正在整理会话备份…");
    progress.hidden = !!job.failure;
    if (state.total) { progress.max = state.total; progress.value = state.received; }
    else progress.removeAttribute("value");
    dialog.setAttribute("aria-busy", String(!job.failure));
  }

  function close() {
    const previous = job;
    job = null;
    previous?.controller.abort();
    if (dialog.open) dialog.close();
    dialog.setAttribute("aria-busy", "false");
    const target = opener?.isConnected && !opener.disabled ? opener : fallbackFocus();
    opener = null;
    if (target?.isConnected && !target.disabled) target.focus({ preventScroll: true });
  }

  async function run(session) {
    const active = { session: { project_id: session.project_id, id: session.id, title: session.title },
      controller: new AbortController(), failure: null, progress: { phase: "preparing", received: 0, total: 0 } };
    job = active; render();
    try {
      const file = await download(active.session, {
        signal: active.controller.signal,
        onProgress(state) { if (job === active) { active.progress = state; render(); } },
      });
      if (job !== active || active.controller.signal.aborted || destroyed) return;
      save(file); close();
      notify(t("backupExport.downloading", {}, "备份已校验，已交给浏览器下载"));
    } catch (failure) {
      if (job !== active || destroyed) return;
      if (failure?.name === "AbortError") { close(); return; }
      active.failure = failure; render();
      // Native focus reveals the error in the scrollable body on short screens.
      error.focus();
    }
  }

  function retryDownload() {
    if (!job?.failure) return;
    const session = job.session;
    void run(session); cancel.focus({ preventScroll: true });
  }
  function onCancel(event) { event.preventDefault(); close(); }
  // Native close events are queued; a previous event must not cancel a newly
  // reopened dialog after a fast Esc/reopen sequence.
  function onClose() { if (job && !dialog.open) close(); }
  cancel.addEventListener("click", close); closeButton.addEventListener("click", close);
  retry.addEventListener("click", retryDownload);
  dialog.addEventListener("cancel", onCancel); dialog.addEventListener("close", onClose);
  const unsubscribe = subscribeLocale(render);
  return Object.freeze({
    open(session) {
      if (destroyed) return;
      // Header/sidebar repeat actions share one dialog and one pinned owner.
      if (job) { if (!dialog.open) dialog.showModal(); cancel.focus({ preventScroll: true }); return; }
      opener = document.activeElement;
      void run(session);
      if (!dialog.open) dialog.showModal(); cancel.focus({ preventScroll: true });
    },
    destroy() {
      destroyed = true; close(); unsubscribe();
      cancel.removeEventListener("click", close); closeButton.removeEventListener("click", close);
      retry.removeEventListener("click", retryDownload);
      dialog.removeEventListener("cancel", onCancel); dialog.removeEventListener("close", onClose);
    },
  });
}
