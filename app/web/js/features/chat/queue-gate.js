// A dispatch failure needs an explicit user decision. A detail refresh may
// supersede an older detail refresh, but it must not release the runtime check
// started when a session was selected.
export function createQueueGate() {
  const blocked = new Set();
  const loading = new Map();
  return Object.freeze({
    has(key) { return blocked.has(key) || loading.has(key); },
    block(key) { blocked.add(key); },
    unblock(key) { blocked.delete(key); },
    beginLoad(key, kind = "detail") {
      const token = Symbol();
      let current = loading.get(key);
      if (!current) {
        current = new Map();
        loading.set(key, current);
      }
      current.set(kind, token);
      return () => {
        const active = loading.get(key);
        if (active?.get(kind) !== token) return;
        active.delete(kind);
        if (!active.size) loading.delete(key);
      };
    },
  });
}
