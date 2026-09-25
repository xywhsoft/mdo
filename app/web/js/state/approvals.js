import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";

export const approvalsStore = createResourceStore({ total: 0, limit: 4, truncated: false, items: [] });
export const approvalDecisionStore = createResourceStore({ pending: [], submitted: [] });
const pending = new Set();
const submitted = new Set();

function publishDecisions() {
  approvalDecisionStore.setData({ pending: [...pending], submitted: [...submitted] });
}

export function approvalDecisionStatus(value) {
  const id = String(value);
  return pending.has(id) ? "pending" : submitted.has(id) ? "submitted" : "idle";
}

function approvalId(value) {
  const id = String(value ?? "");
  if (!/^[1-9][0-9]*$/.test(id)) throw new TypeError("approval ID is invalid");
  return id;
}

export function loadApprovals() {
  return approvalsStore.load(async () => {
    const data = (await api.get("/approvals")).data;
    const live = new Set((data.items ?? []).map((item) => String(item.id)));
    let changed = false;
    for (const id of submitted) {
      if (live.has(id)) continue;
      submitted.delete(id);
      changed = true;
    }
    if (changed) publishDecisions();
    return data;
  });
}

export async function decideApproval(value, decision) {
  const id = approvalId(value);
  if (!new Set(["allow", "deny"]).has(decision)) throw new TypeError("approval decision is invalid");
  if (approvalDecisionStatus(id) !== "idle") return false;
  pending.add(id);
  publishDecisions();
  try {
    await api.put(`/approvals/${id}`, { decision });
    submitted.add(id);
    publishDecisions();
    const refreshed = await loadApprovals();
    if (refreshed.status === "error") throw refreshed.error;
    return true;
  } finally {
    pending.delete(id);
    publishDecisions();
  }
}
