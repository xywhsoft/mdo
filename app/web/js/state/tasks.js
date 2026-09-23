import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";

export const tasksStore = createResourceStore({ total: 0, items: [] });

export function loadTasks() {
  return tasksStore.load(async () => (await api.get("/tasks")).data);
}

export async function cancelTask(taskId) {
  const id = String(taskId);
  if (!/^[1-9][0-9]*$/.test(id)) throw new TypeError("task ID is invalid");
  const response = await api.delete(`/tasks/${id}`);
  await loadTasks();
  return response.data;
}
