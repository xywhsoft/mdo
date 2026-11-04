export function directoryChild(data, name) {
  return `${data.path}${data.path.endsWith(data.separator) ? "" : data.separator}${name}`;
}

export function directoryBreadcrumbs(path, separator) {
  let root = separator, tail = path;
  if (separator === "\\") {
    const unc = /^(\\\\[^\\]+\\[^\\]+)(?:\\|$)/.exec(path);
    const drive = /^[A-Za-z]:\\/.exec(path);
    if (unc) { root = `${unc[1]}\\`; tail = path.slice(root.length); }
    else if (drive) { root = drive[0]; tail = path.slice(root.length); }
    else return [{ name: path, path }];
  } else tail = path.startsWith("/") ? path.slice(1) : path;
  const rows = [{ name: root, path: root }];
  for (const part of tail.split(separator).filter(Boolean)) {
    const parent = rows.at(-1).path;
    rows.push({ name: part, path: `${parent}${parent.endsWith(separator) ? "" : separator}${part}` });
  }
  return rows;
}

export function directoryCompletion(path, separator) {
  const index = path.lastIndexOf(separator);
  if (index < 0) return null;
  return { path: path.slice(0, index + 1), prefix: path.slice(index + 1), limit: 20 };
}

const validPath = value => typeof value === "string" && value.length > 0 &&
  value.length <= 2048 && !/[\x00-\x1f\x7f]/.test(value);
export function directoryPlaces(storage, scope) {
  const key = `mdo.directory-places.v1:${scope}`;
  let value;
  try { value = JSON.parse(storage?.getItem(key) || "null"); } catch { /* Optional local history. */ }
  const clean = rows => [...new Set(Array.isArray(rows) ? rows.filter(validPath) : [])].slice(0, 16);
  const state = { favorites: clean(value?.favorites), recent: clean(value?.recent) };
  const save = () => { try { storage?.setItem(key, JSON.stringify(state)); } catch { /* Browsing still works without storage. */ } };
  return {
    get: () => state,
    confirm(path) { if (validPath(path)) { state.recent = [path, ...state.recent.filter(row => row !== path)].slice(0, 16); save(); } },
    favorite(path) {
      if (!validPath(path)) return;
      state.favorites = state.favorites.includes(path) ? state.favorites.filter(row => row !== path)
        : [path, ...state.favorites].slice(0, 16); save();
    },
  };
}

/** Own read/navigation state separately from window DOM. Late requests cannot
 * select an old path, move history, or publish into a different device. */
export function createDirectoryNavigation({ read, create, onChange = () => {} }) {
  let state = { current: null, selected: "", filter: "", requestedPath: "", loading: false,
    creating: false, failure: null, history: [], index: -1 };
  let generation = 0, controller = null, disposed = false, retry = null, pendingTimer = 0;
  const publish = patch => { state = { ...state, ...patch }; if (!disposed) onChange(state); };
  const begin = () => {
    controller?.abort(); clearTimeout(pendingTimer); controller = new AbortController();
    const own = { id: ++generation, controller, timer: 0, timedOut: false };
    own.timer = setTimeout(() => {
      own.timedOut = true; own.controller.abort();
      failed(own, Object.assign(new Error("Directory request timed out"), { code: state.creating ? "directory_create_uncertain" : "directory_read_timeout" }));
    }, 10000);
    pendingTimer = own.timer;
    return own;
  };
  const current = own => !disposed && own.id === generation && !own.controller.signal.aborted;
  function failed(own, error) {
    if (disposed || own.id !== generation) return;
    if (own.timedOut && state.failure) return;
    publish({ loading: false, creating: false, failure: error });
  }
  async function navigate(path, options = {}) {
    if (disposed || state.creating) return;
    const own = begin(), filter = options.filter ?? "";
    retry = () => navigate(path, options);
    publish({ requestedPath: path, selected: "", loading: true, failure: null, filter });
    try {
      const data = await read(path, own.controller.signal, { filter, limit: 128 });
      if (!current(own)) return;
      let history = state.history, index = state.index;
      if (options.historyIndex !== undefined) index = options.historyIndex;
      else if (options.replace) {
        history = history.slice();
        if (index < 0) { history.push(data.path); index = 0; } else history[index] = data.path;
      } else if (history[index] !== data.path) {
        history = [...history.slice(0, index + 1), data.path].slice(-64); index = history.length - 1;
      }
      publish({ current: data, requestedPath: data.path, loading: false, history, index,
        selected: options.select && data.directories.includes(options.select) ? options.select : "" });
    } catch (error) { failed(own, error); }
    finally { clearTimeout(own.timer); }
  }
  return {
    get: () => state, navigate,
    select(name) { if (!state.loading && state.current?.directories.includes(name)) publish({ selected: name }); },
    back() { if (state.index > 0) return navigate(state.history[state.index - 1], { historyIndex: state.index - 1 }); },
    forward() { if (state.index + 1 < state.history.length) return navigate(state.history[state.index + 1], { historyIndex: state.index + 1 }); },
    retry() { return retry?.(); },
    async more() {
      if (state.loading || !state.current?.has_more || disposed) return;
      const previous = state.current, own = begin(); retry = () => this.more();
      publish({ loading: true, failure: null });
      try {
        const page = await read(previous.path, own.controller.signal, { filter: state.filter, after: previous.next_cursor, limit: 128 });
        if (current(own)) publish({ current: { ...page, directories: [...new Set([...previous.directories, ...page.directories])] }, loading: false });
      } catch (error) { failed(own, error); }
      finally { clearTimeout(own.timer); }
    },
    async confirm() {
      if (state.loading || state.failure || !state.current || disposed) return null;
      const selected = state.selected ? directoryChild(state.current, state.selected) : state.current.path;
      const own = begin(); publish({ loading: true, failure: null });
      retry = () => navigate(state.current.path, { filter: state.filter, replace: true });
      try {
        const data = await read(selected, own.controller.signal, { limit: 1 });
        if (!current(own)) return null;
        publish({ loading: false }); return data.path;
      } catch (error) { failed(own, error); return null; }
      finally { clearTimeout(own.timer); }
    },
    async mkdir(name) {
      if (state.loading || state.failure || !state.current || !name || disposed) return;
      const parent = state.current.path, own = begin();
      publish({ loading: true, creating: true, failure: null });
      try {
        const value = await create(parent, name, own.controller.signal);
        if (!current(own)) return;
        publish({ loading: false, creating: false });
        await navigate(parent, { replace: true, filter: value.name, select: value.name });
        // A directory with many substring matches still has an accessible
        // selected result: enter the new directory when it is not in this page.
        if (state.current?.path === parent && !state.selected && !state.failure) await navigate(value.path);
        return value;
      } catch (error) { failed(own, error); }
      finally { clearTimeout(own.timer); }
    },
    dispose() { disposed = true; ++generation; controller?.abort(); clearTimeout(pendingTimer); },
  };
}
