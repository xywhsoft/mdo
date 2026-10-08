import { t } from "../../i18n.js";
import { ApiError } from "../../api/client.js";
import { isTransientReadError } from "../../api/read-recovery.js";
import { createMessageEditRecovery, messageEditId } from "./message-edit-recovery.js";

// A history edit is bound to the session where the user started it. Once the
// truncate succeeds, finish that session's run even if navigation has changed;
// callbacks may touch the visible UI only while the original route remains.
export async function runMessageReplacement({ session, sequence, sourceEventId, text, attachments,
  isCurrent, loadHistory, validateBeforeTruncate = () => {}, truncate, startRun,
  preserveInput = () => true, confirmInput = () => true,
  onPreserveFailure = () => {}, onTruncated, onStartFailure,
  onAccepted = () => {}, onStarted, recoveryOptions, editId = messageEditId() }) {
  if (!Number.isSafeInteger(sourceEventId) || sourceEventId < 1)
    throw new Error(t("messageAction.historyChanged", {}, "消息已不在当前会话历史中，请刷新会话"));
  const recovery = createMessageEditRecovery(recoveryOptions);
  let preserving = false, starting = false;
  try {
    const history = await recovery.request(signal => loadHistory(session, { signal }));
    if (!isCurrent()) throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
    if (sequence > history.last_sequence)
      throw new Error(t("messageAction.historyChanged", {}, "消息已不在当前会话历史中，请刷新会话"));
    await recovery.request(signal => validateBeforeTruncate({ signal }));
    if (!isCurrent()) throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
    preserving = true;
    // Persist once before cutting history. A lost draft acknowledgement is
    // confirmed by reads, never by replaying a draft write from this action.
    try {
      const saved = await recovery.request(() => preserveInput(), { retry: false });
      if (!saved && !await recovery.request(signal => confirmInput({ signal })))
        throw new ApiError("The edit input has not been saved", { code: "session_edit_draft_unsaved" });
    } catch (error) {
      if (isTransientReadError(error) ||
          ["remote_result_unconfirmed", "invalid_response"].includes(error?.code)) {
        const final = new ApiError("The edit input has not been confirmed saved", {
          code: "session_edit_draft_unsaved" });
        final.cause = error;
        throw final;
      }
      throw error;
    }
    if (!isCurrent()) throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));

    let updated;
    try {
      updated = await recovery.request(signal => truncate({ ...session, etag: history.etag,
        revision: history.revision }, sequence - 1, sourceEventId, { editId, signal }), { mutation: true });
    } catch (error) {
      if (isTransientReadError(error) ||
          ["remote_result_unconfirmed", "invalid_response"].includes(error?.code)) {
        const final = new ApiError("The history edit result could not be confirmed", {
          code: "session_edit_unconfirmed" });
        final.cause = error;
        throw final;
      }
      throw error;
    }
    if (isCurrent()) onTruncated(updated);
    recovery.assertActive();
    starting = true;
    let run;
    try { run = await startRun(updated.project_id, updated.id,
      text.trim(), attachments); }
    catch (error) {
      onStartFailure(updated, error, isCurrent());
      throw error;
    }
    onAccepted(run);
    if (isCurrent()) onStarted(run);
    return { run, current: isCurrent() };
  } catch (error) {
    if (preserving && !starting)
      onPreserveFailure(error, error?.name !== "AbortError" && isCurrent());
    throw error;
  } finally { recovery.dispose(); }
}
