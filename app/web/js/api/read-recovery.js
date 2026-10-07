// Background read failures do not prove that a run failed or needs recovery.
// Keep mutation uncertainty and permanent access failures visible separately.
export function isTransientReadError(error) {
  return ["network_error", "remote_offline", "remote_timeout", "target_changing"].includes(error?.code) ||
    [408, 425, 429, 500, 502, 503, 504].includes(error?.status);
}

export function needsRecoveryCard(state, projectId, sessionId) {
  if (!sessionId) return false;
  const data = state?.data;
  const known = data?.resume_required && !data.unavailable &&
    data.project_id === projectId && data.session_id === sessionId;
  return Boolean(known || (state?.status === "error" && !isTransientReadError(state.error)));
}
