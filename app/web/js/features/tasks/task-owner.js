// xwork records the session snapshot path as the task owner. Keep path parsing
// here so the conversation dock does not mistake that path for a session ID.
export function taskOwnerLocation(ownerSession) {
  if (typeof ownerSession !== "string") return null;
  const parts = ownerSession.split(/[\\/]/);
  if (parts.length < 4 || parts.at(-4) !== "sessions" ||
      parts.at(-1) !== "snapshot.json" || !parts.at(-3) || !parts.at(-2))
    return null;
  return { projectId: parts.at(-3), sessionId: parts.at(-2) };
}

export function taskBelongsToSession(task, projectId, sessionId) {
  if (!projectId || !sessionId) return false;
  const owner = taskOwnerLocation(task?.owner_session);
  return owner?.projectId === projectId && owner.sessionId === sessionId;
}
