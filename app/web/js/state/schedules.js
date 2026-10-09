import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { isTransientReadError } from "../api/read-recovery.js";

export const schedulesStore = createResourceStore({ items: [], total: 0 }, {
  recoverRead: isTransientReadError,
});

export function loadSchedules({ background = false } = {}) {
  return schedulesStore.load(async signal => (await api.get("/schedules", { signal })).data,
    { background });
}

// Leaving the panel releases both an in-flight GET and its backoff timers.
// Keep the last catalog; the next visit reads a fresh version before editing.
export function cancelScheduleReads() { schedulesStore.reset(schedulesStore.get().data); }

function path(id) { return `/schedules/${resourceId(id, "schedule")}`; }

export function runSchedule(id, revision) {
  return api.post(`${path(id)}/run`, undefined, {
    ifMatch: `"mdo-schedule-${id}-${revision}"`,
  });
}
export function createSchedule(body) { return api.post("/schedules", body); }
export function replaceSchedule(id, etag, body) {
  return api.put(path(id), body, { ifMatch: etag });
}
export function setScheduleEnabled(id, revision, enabled) {
  return api.put(`${path(id)}/enabled`, { enabled }, {
    ifMatch: `"mdo-schedule-${id}-${revision}"`,
  });
}
export function removeSchedule(id, revision) {
  return api.delete(path(id), {
    ifMatch: `"mdo-schedule-${id}-${revision}"`,
  });
}
