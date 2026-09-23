import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";

export const approvalsStore = createResourceStore({ total: 0, limit: 4, truncated: false, items: [] });

function approvalId(value) {
  const id = String(value ?? "");
  if (!/^[1-9][0-9]*$/.test(id)) throw new TypeError("approval ID is invalid");
  return id;
}

export function loadApprovals() {
  return approvalsStore.load(async () => (await api.get("/approvals")).data);
}

export async function decideApproval(value, decision) {
  const id = approvalId(value);
  if (!new Set(["allow", "deny"]).has(decision)) throw new TypeError("approval decision is invalid");
  const response = await api.put(`/approvals/${id}`, { decision });
  await loadApprovals();
  return response.data;
}
