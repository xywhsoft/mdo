import { createSha256 } from "../../utils/sha256.js";

function invalid() {
  return Object.assign(new Error("Conversation snapshot failed validation"), { code: "conversation_invalid" });
}
// Hash exactly the native bytes, before parsing unrelated uint64 identifiers.
export function decodeConversationPage(page, owner) {
  if (!page || page.project_id !== owner.projectId || page.session_id !== owner.sessionId ||
      !/^[0-9a-f]{64}$/.test(page.epoch) || !/^[0-9a-f]{64}$/.test(page.items_hash) ||
      typeof page.items_json !== "string" || page.items_json.length > 256 * 1024 ||
      typeof page.delta !== "boolean" || typeof page.has_more !== "boolean" ||
      !Number.isSafeInteger(page.latest_event_id) || page.latest_event_id < 0 ||
      !Number.isSafeInteger(page.next_cursor) || page.next_cursor < 0 || page.next_cursor > page.latest_event_id ||
      !Number.isSafeInteger(page.next_before) || page.next_before < 0) throw invalid();
  const bytes = new TextEncoder().encode(page.items_json);
  const hash = createSha256(); hash.update(bytes);
  if (bytes.length > 256 * 1024 || hash.hex() !== page.items_hash) throw invalid();
  let items;
  try { items = JSON.parse(page.items_json); } catch { throw invalid(); }
  if (!Array.isArray(items) || items.length > 96) throw invalid();
  let previous = 0;
  for (const event of items) {
    if (!event || !Number.isSafeInteger(event.event_id) || event.event_id <= previous ||
        event.event_id > page.latest_event_id || typeof event.kind !== "string" || typeof event.text !== "string" ||
        !/^[0-9a-f]{64}$/.test(event.content_hash) || event.projection_epoch !== page.epoch ||
        event.node_id !== `${owner.sessionId}:${page.epoch}:${event.event_id}` ||
        !Number.isSafeInteger(event.aggregate_end_id) || event.aggregate_end_id < 0 ||
        (event.aggregate_end_id && (event.aggregate_end_id < event.event_id || event.aggregate_end_id > page.next_cursor)))
      throw invalid();
    previous = event.event_id;
  }
  return { ...page, items };
}
