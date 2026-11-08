// Explicit validated ownership wins over run IDs. Missing legacy metadata may
// fall back to a real selected Agent run, never the SDK sentinel run_id = 0.
export function approvalBelongsToSession(item, projectId, sessionId, runIds) {
  if (!projectId || !sessionId) return false;
  if (item.project_id || item.session_id)
    return item.project_id === projectId && item.session_id === sessionId;
  const id = String(item.run_id ?? "");
  return /^[1-9][0-9]*$/.test(id) && runIds.has(id);
}
