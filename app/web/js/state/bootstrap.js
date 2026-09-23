import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";

export const bootstrapStore = createResourceStore();

export function loadBootstrap() {
  return bootstrapStore.load(async () => (await api.get("/bootstrap")).data);
}
