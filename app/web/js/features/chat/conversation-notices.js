// Display diagnostics are independent of model errors and durable history edits.
// They never enter event replay, prompt context, retry or fork actions.
export const conversationNoticeCodes = new Set([
  "scan_limit", "invalid_record", "text_limit", "event_limit", "unreadable_event",
]);
export function mergeConversationNotices(retained = [], additions = []) {
  const unique = new Map(retained.map(notice => [notice.id, notice]));
  for (const notice of additions) unique.set(notice.id, notice);
  return [...unique.values()].slice(-128);
}

export function insertConversationNotices(items, notices, text) {
  const result = [...items];
  for (const notice of notices || []) {
    const item = { key: `history-notice-${notice.id}`, kind: "history-notice", state: "done", time: 0,
      text: text(notice.code), sourceEventId: notice.before_event_id || 0 };
    const index = notice.before_event_id ? result.findIndex(value =>
      value.kind !== "history-notice" && value.sourceEventId >= notice.before_event_id) : -1;
    result.splice(index < 0 ? result.length : index, 0, item);
  }
  return result;
}
