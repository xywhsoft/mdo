export function createSessionMetadataSync({ navigation, store, readSession,
  loadSessions, onRestored }) {
  const inFlight = new Map();

  async function refresh() {
    const { view, projectId, sessionId } = navigation.get();
    const session = store.get().data;
    if (view !== "workspace" || !projectId || !sessionId ||
        session?.project_id !== projectId || session.id !== sessionId ||
        store.isPending?.()) return false;
    const key = `${projectId}/${sessionId}`;
    if (inFlight.has(key)) return inFlight.get(key);
    const request = (async () => {
      const fresh = await readSession(projectId, sessionId);
      const route = navigation.get();
      const current = store.get().data;
      if (route.view !== "workspace" || route.projectId !== projectId ||
          route.sessionId !== sessionId ||
          current?.project_id !== projectId || current.id !== sessionId ||
          fresh?.project_id !== projectId || fresh.id !== sessionId ||
          !Number.isSafeInteger(fresh.revision) ||
          fresh.revision <= current.revision) return false;
      store.setData(fresh);
      void loadSessions();
      if (fresh.status === "active" && current.status !== "active" &&
          navigation.get().projectId === projectId &&
          navigation.get().sessionId === sessionId &&
          store.get().data?.status === "active") await onRestored();
      return true;
    })();
    inFlight.set(key, request);
    try { return await request; }
    finally { if (inFlight.get(key) === request) inFlight.delete(key); }
  }

  return Object.freeze({ refresh });
}
