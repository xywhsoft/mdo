import { t } from "../../i18n.js";

// A history edit is bound to the session where the user started it. Once the
// truncate succeeds, finish that session's run even if navigation has changed;
// callbacks may touch the visible UI only while the original route remains.
export async function runMessageReplacement({ session, sequence, text, attachments,
  isCurrent, loadHistory, validateBeforeTruncate = () => {}, truncate, startRun,
  onTruncated, onStartFailure,
  onStarted }) {
  const history = await loadHistory(session);
  if (!isCurrent()) throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
  if (sequence > history.last_sequence)
    throw new Error(t("messageAction.historyChanged", {}, "消息已不在当前会话历史中，请刷新会话"));
  await validateBeforeTruncate();
  if (!isCurrent()) throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));

  const updated = await truncate({ ...session, etag: history.etag,
    revision: history.revision }, sequence - 1);
  if (isCurrent()) onTruncated(updated);
  let run;
  try { run = await startRun(updated.project_id, updated.id,
    text.trim(), attachments); }
  catch (error) {
    onStartFailure(updated, error, isCurrent());
    throw error;
  }
  if (isCurrent()) onStarted(run);
  return { run, current: isCurrent() };
}
