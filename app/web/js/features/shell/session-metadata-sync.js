import { createRequestRecovery } from "../../api/request-recovery.js";

export function createSessionMetadataSync({ navigation, store, readSession,
  loadSessions, onRestored, now = Date.now, createRecovery = createRequestRecovery }) {
  const inFlight = new Map();
  let stopped = false, cooldown = null;

  function owns(record) {
    const route = navigation.get(), session = store.get().data;
    return route.view === "workspace" && route.projectId === record.projectId &&
      route.sessionId === record.sessionId && session?.project_id === record.projectId &&
      session.id === record.sessionId;
  }

  function cancelObsolete() {
    for (const [key, record] of inFlight) {
      if (record.reading && (!owns(record) || store.isPending?.() ||
          store.get().data.revision !== record.revision)) {
        inFlight.delete(key);
        record.recovery.dispose();
      }
    }
    if (cooldown && (!owns(cooldown) || store.get().data.revision !== cooldown.revision))
      cooldown = null;
  }
  const unsubscribeRoute = navigation.subscribe?.(cancelObsolete);
  const unsubscribeStore = store.subscribe(cancelObsolete);

  async function refresh() {
    if (stopped) return false;
    cancelObsolete();
    const { view, projectId, sessionId } = navigation.get();
    const session = store.get().data;
    if (view !== "workspace" || !projectId || !sessionId ||
        session?.project_id !== projectId || session.id !== sessionId ||
        store.isPending?.()) return false;
    const key = `${projectId}/${sessionId}`;
    if (inFlight.has(key)) return inFlight.get(key).promise;
    // Periodic/live notifications must not reopen a full retry budget after
    // exhaustion. Keep saved metadata and allow one probe per 30 seconds.
    if (cooldown && now() < cooldown.at) return false;
    const probing = Boolean(cooldown);
    const record = { projectId, sessionId, revision: session.revision, reading: true,
      recovery: createRecovery(), promise: null };
    const request = (async () => {
      const fresh = await record.recovery.request(signal =>
        readSession(projectId, sessionId, { signal }), { retry: !probing });
      record.recovery.assertActive();
      record.reading = false;
      cooldown = null;
      const route = navigation.get();
      const current = store.get().data;
      if (route.view !== "workspace" || route.projectId !== projectId ||
          route.sessionId !== sessionId ||
          current?.project_id !== projectId || current.id !== sessionId ||
          current.revision !== record.revision ||
          fresh?.project_id !== projectId || fresh.id !== sessionId ||
          !Number.isSafeInteger(fresh.revision) ||
          fresh.revision <= current.revision) return false;
      store.setData(fresh);
      void loadSessions();
      if (fresh.status === "active" && current.status !== "active" &&
          navigation.get().view === "workspace" &&
          navigation.get().projectId === projectId &&
          navigation.get().sessionId === sessionId &&
          store.get().data?.status === "active") await onRestored();
      return true;
    })();
    record.promise = request.catch(error => {
      if (error?.name === "AbortError") return false;
      if (record.reading && owns(record) && store.get().data.revision === record.revision)
        cooldown = { projectId, sessionId, revision: record.revision, at: now() + 30000 };
      throw error;
    }).finally(() => {
      record.recovery.dispose();
      if (inFlight.get(key) === record) inFlight.delete(key);
    });
    inFlight.set(key, record);
    return record.promise;
  }

  function destroy() {
    stopped = true;
    unsubscribeRoute?.(); unsubscribeStore();
    for (const record of inFlight.values()) record.recovery.dispose();
    inFlight.clear(); cooldown = null;
  }

  return Object.freeze({ refresh, destroy });
}
