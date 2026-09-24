import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";

export const schedulesStore = createResourceStore({ items: [], total: 0 });

export function loadSchedules() {
  return schedulesStore.load(async () => (await api.get("/schedules")).data);
}

function path(id) { return `/schedules/${resourceId(id, "schedule")}`; }

export function readSchedule(id) { return api.get(path(id)); }
export function readScheduleHistory(id) { return api.get(`${path(id)}/history`); }
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
