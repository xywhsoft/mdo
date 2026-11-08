import { api, resourceId } from "../api/client.js";
import { t as uiErrorText, t } from "../i18n.js";
import { createResourceStore } from "./store.js";
import { isTransientReadError } from "../api/read-recovery.js";

import { runDecisionAction } from "./decision-action.js";

export const approvalsStore = createResourceStore({ total: 0, limit: 4, truncated: false, items: [] },
  { recoverRead: isTransientReadError, retainDataOnError: isTransientReadError });
let selection = Object.freeze({projectId:"",sessionId:""});
export function clearApprovals() {
  selection = Object.freeze({projectId:"",sessionId:""});
  approvalsStore.reset();
}
export async function selectApprovals(projectId, sessionId) {
  selection = Object.freeze({projectId:resourceId(projectId,"project"),sessionId:resourceId(sessionId,"session")});
  approvalsStore.reset();
  return loadApprovals({retry:true});
}
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
  if (!/^[1-9][0-9]*$/.test(id)) throw new TypeError(uiErrorText("error.detail.approvalIdIsInvalid", {}, "approval ID is invalid"));
  return id;
}

export function loadApprovals({ retry = false } = {}) {
  if (!retry && approvalsStore.isPending()) return Promise.resolve(approvalsStore.get());
  const selected = selection;
  return approvalsStore.load(async signal => {
    const query = selected.sessionId ? `?project_id=${selected.projectId}&session_id=${selected.sessionId}` : "";
    const data = (await api.get("/approvals" + query, { signal })).data;
    if (signal.aborted) throw new DOMException("Approval read cancelled", "AbortError");
    if (!Array.isArray(data?.items)) throw new Error(t("approval.invalidResponse", {}, "权限审核响应无效"));
    if (selected.sessionId && (data.project_id !== selected.projectId || data.session_id !== selected.sessionId))
      throw new Error(t("approval.invalidResponse", {}, "权限审核响应无效"));
    const live = new Set(data.items.map((item) => String(item.id)));
    let changed = false;
    for (const id of submitted) {
      if (live.has(id)) continue;
      submitted.delete(id);
      changed = true;
    }
    if (changed) publishDecisions();
    return {...data, total:data.pending_total ?? data.total, scopeTotal:data.total, ...selected};
  }, { background: !retry });
}

export async function decideApproval(value, decision) {
  const id = approvalId(value);
  if (!new Set(["allow", "allow_run", "deny"]).has(decision))
    throw new TypeError(uiErrorText("error.detail.approvalDecisionIsInvalid", {}, "approval decision is invalid"));
  if (approvalDecisionStatus(id) !== "idle") return false;
  pending.add(id);
  publishDecisions();
  try {
    await runDecisionAction({ path: `/approvals/${id}`, id, field: "decision", value: decision });
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
