// Recovery inspection and run creation both open an exclusive Agent runtime on
// the server. Keep those requests ordered within this page for each session.
const pending = new Map();

export async function withSessionRuntime(projectId, sessionId, operation) {
  const key = `${projectId}/${sessionId}`;
  const previous = pending.get(key);
  let release;
  const current = new Promise((resolve) => { release = resolve; });
  pending.set(key, current);
  if (previous) await previous;
  try {
    return await operation();
  } finally {
    release();
    if (pending.get(key) === current) pending.delete(key);
  }
}
