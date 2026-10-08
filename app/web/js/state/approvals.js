import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { t } from "../i18n.js";

export const approvalsStore = createResourceStore({ total: 0, limit: 4, truncated: false, items: [] },
  { recoverRead: isTransientReadError, retainDataOnError: isTransientReadError });
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

export function loadApprovals({ retry = false } = {}) {
  if (!retry && approvalsStore.isPending()) return Promise.resolve(approvalsStore.get());
  return approvalsStore.load(async signal => {
    const data = (await api.get("/approvals", { signal })).data;
    if (signal.aborted) throw new DOMException("Approval read cancelled", "AbortError");
    if (!Array.isArray(data?.items)) throw new Error(t("approval.invalidResponse", {}, "权限审核响应无效"));
    const live = new Set(data.items.map((item) => String(item.id)));
    let changed = false;
    for (const id of submitted) {
      if (live.has(id)) continue;
      submitted.delete(id);
      changed = true;
    }
    if (changed) publishDecisions();
    return data;
  }, { background: !retry });
}

export async function decideApproval(value, decision) {
  const id = approvalId(value);
  if (!new Set(["allow", "allow_run", "deny"]).has(decision))
    throw new TypeError("approval decision is invalid");
  if (approvalDecisionStatus(id) !== "idle") return false;
  pending.add(id);
  publishDecisions();
  try {
    await api.put(`/approvals/${id}`, { decision });
    submitted.add(id);
    publishDecisions();
    // Acknowledgement completes the decision. Readback has its own quiet
    // budget and final notice; it must neither delay nor fail this PUT.
    void loadApprovals({ retry: true });
    return true;
  } finally {
    pending.delete(id);
    publishDecisions();
  }
}
