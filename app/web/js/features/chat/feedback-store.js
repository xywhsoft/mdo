import { api, resourceId } from "../../api/client.js";
import { createResourceStore } from "../../state/store.js";
import { t } from "../../i18n.js";

export const feedbackStore = createResourceStore({
  projectId: "", sessionId: "", items: new Map(),
});

let generation = 0;
let writeVersion = 0;
const pending = new Set();

function responseData(projectId, sessionId, response) {
  return {
    projectId,
    sessionId,
    items: new Map((response.data?.items ?? []).map((item) =>
      [Number(item.event_id), item.value])),
  };
}

export async function selectFeedback(projectId, sessionId) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  const token = ++generation;
  const readVersion = writeVersion;
  feedbackStore.setData({ projectId: project, sessionId: session, items: new Map() });
  try {
    const response = await api.get(`/projects/${project}/sessions/${session}/feedback`);
    if (token === generation && readVersion === writeVersion)
      feedbackStore.setData(responseData(project, session, response));
  } catch (error) {
    if (token === generation && readVersion === writeVersion) feedbackStore.setError(error);
  }
}

export function clearFeedback() {
  generation += 1;
  feedbackStore.setData({ projectId: "", sessionId: "", items: new Map() });
}

export async function setFeedback(projectId, sessionId, eventId, value) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  if (!Number.isSafeInteger(eventId) || eventId <= 0 ||
      !["good", "bad", "none"].includes(value))
    throw new TypeError(t("feedback.invalid", {}, "无效的消息反馈"));
  const key = `${project}/${session}/${eventId}`;
  if (pending.has(key)) return;
  pending.add(key);
  const token = generation;
  try {
    const response = await api.put(`/projects/${project}/sessions/${session}/feedback`,
      { event_id: eventId, value });
    if (token === generation && feedbackStore.get().data?.projectId === project &&
        feedbackStore.get().data?.sessionId === session) {
      // The acknowledged vote is newer than any outstanding selection read.
      // Failed writes leave that read useful for restoring persisted votes.
      writeVersion += 1;
      feedbackStore.setData(responseData(project, session, response));
    }
  } finally { pending.delete(key); }
}
