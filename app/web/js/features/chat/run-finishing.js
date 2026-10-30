// UI completion is evidence of finishing, never authority to start another
// run. Reused native IDs after a host restart require the current time bound.
export function runAwaitingRelease(run, timeline) {
  if (!run || run.terminal !== false || !run.agent_run_id ||
      timeline?.projectId !== run.project_id || timeline?.sessionId !== run.session_id) return false;
  const start = Number(run.started_at || run.created_at);
  if (!Number.isSafeInteger(start) || start <= 0) return false;
  const event = (timeline.events ?? []).findLast(item =>
    !item.agent_depth && item.run_id === run.agent_run_id && item.time >= start &&
    ["agent_start", "agent_done", "error"].includes(item.kind));
  return Boolean(event && ["agent_done", "error"].includes(event.kind));
}
