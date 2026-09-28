// A dispatch failure needs an explicit user decision, while navigation only
// blocks dispatch until the latest load of that session has settled.
export function createQueueGate() {
  const blocked = new Set();
  const loading = new Map();
  return Object.freeze({
    has(key) { return blocked.has(key) || loading.has(key); },
    block(key) { blocked.add(key); },
    unblock(key) { blocked.delete(key); },
    beginLoad(key) {
      const token = Symbol();
      loading.set(key, token);
      return () => {
        if (loading.get(key) === token) loading.delete(key);
      };
    },
  });
}
