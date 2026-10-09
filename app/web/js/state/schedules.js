import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { createScheduleWriter } from "./schedule-write.js";

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

const writer = createScheduleWriter();
export const runSchedule = writer.run;
export const createSchedule = writer.create;
export const replaceSchedule = writer.replace;
export const setScheduleEnabled = writer.enabled;
export const removeSchedule = writer.remove;
