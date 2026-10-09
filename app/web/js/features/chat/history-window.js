// History requests return complete reply/process groups. The viewport decides
// how many groups to fetch; text length is never used to split a reply.
const BUFFER_SCREENS = 2.5;
const PREFETCH_SCREENS = 1;
const MAX_PAGES_PER_BATCH = 64;

export function createHistoryWindow({ read, measure, load, afterPaint }) {
  let pending = null;
  let disposed = false;
  let blockedCursor = "";
  const owner = data => `${data?.projectId}/${data?.sessionId}:${data?.epoch ?? ""}`;
  const cursor = data => `${owner(data)}:${data?.firstLoadedTurn}`;

  async function fill(mode) {
    const initial = read();
    const identity = owner(initial.data);
    const start = measure();
    const target = mode === "fill" ? start.viewport * BUFFER_SCREENS
      : start.height + start.viewport * BUFFER_SCREENS;
    for (let page = 0; page < MAX_PAGES_PER_BATCH && !disposed; ++page) {
      const state = read(), data = state.data;
      if (owner(data) !== identity || !state.enabled || !data?.hasOlder ||
          data.initializing || data.loadingHistory || (mode !== "manual" && data.historyError)) return;
      const before = measure();
      if (before.viewport <= 0 || before.height >= target) return;
      const beforeHeight = before.height;
      const previous = cursor(data);
      await load();
      await afterPaint();
      if (disposed || owner(read().data) !== identity || !read().enabled) return;
      const current = read().data;
      if (current.historyError || cursor(current) === previous || measure().height <= beforeHeight) {
        // No automatic retry loop on unreadable or invisible history. A click
        // can retry, while the timeline store retains its bounded recovery.
        blockedCursor = cursor(current);
        return;
      }
    }
    if (!disposed && owner(read().data) === identity && measure().height < target)
      blockedCursor = cursor(read().data);
  }

  function request(mode) {
    if (disposed) return Promise.resolve();
    if (pending) return pending;
    if (mode === "manual") blockedCursor = "";
    // Defer fill so pending is set before load publishes a synchronous state.
    pending = Promise.resolve().then(() => fill(mode)).finally(() => { pending = null; });
    return pending;
  }

  return Object.freeze({
    consider() {
      const state = read(), data = state.data, geometry = measure();
      if (disposed || pending || !state.enabled || !data?.hasOlder || data.initializing ||
          data.loadingHistory || data.historyError || cursor(data) === blockedCursor || geometry.viewport <= 0)
        return;
      if (geometry.height < geometry.viewport * BUFFER_SCREENS) void request("fill");
      else if (!state.followTail && geometry.top < geometry.viewport * PREFETCH_SCREENS)
        void request("prefetch");
    },
    load() { return request("manual"); },
    resize() { blockedCursor = ""; this.consider(); },
    destroy() { disposed = true; },
  });
}
