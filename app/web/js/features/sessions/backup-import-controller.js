import { backupImportApi } from "../../api/backup-import.js";
import { ApiError } from "../../api/client.js";
import { createRequestRecovery } from "../../api/request-recovery.js";
import { isTransientReadError } from "../../api/read-recovery.js";

const requestId = /^[0-9a-f]{32}$/, digest = /^[0-9a-f]{64}$/;
const projectId = /^[A-Za-z0-9_-][A-Za-z0-9._-]*$/;
const keys = ["mdoRestore", "mdoRestoreProject", "mdoRestoreSha"];
const failure = (code) => new ApiError(code, { code });

// The bookmark contains identity, never authorization. Hash navigation keeps
// search parameters; a reload only GETs the same durable request.
export function createRestoreBookmark({ location = globalThis.location, history = globalThis.history } = {}) {
  function read() {
    const url = new URL(location.href);
    if (!keys.some((key) => url.searchParams.has(key))) return null;
    const [id, project, sha256] = keys.map((key) => url.searchParams.get(key));
    if (!requestId.test(id || "") || !projectId.test(project || "") || !digest.test(sha256 || ""))
      throw failure("restore_bookmark_invalid");
    return { id, project_id: project, source_sha256: sha256 };
  }
  function write(value) {
    const url = new URL(location.href);
    keys.forEach((key) => url.searchParams.delete(key));
    if (value) {
      if (!requestId.test(value.id || "") || !projectId.test(value.project_id || "") ||
          !digest.test(value.source_sha256 || "")) throw failure("restore_identity_mismatch");
      [value.id, value.project_id, value.source_sha256].forEach((item, index) => url.searchParams.set(keys[index], item));
    }
    try { history.replaceState(history.state, "", url.pathname + url.search + url.hash); }
    catch { throw failure("restore_bookmark_unavailable"); }
    const saved = read();
    if (value ? !saved || saved.id !== value.id || saved.project_id !== value.project_id ||
        saved.source_sha256 !== value.source_sha256 : saved !== null)
      throw failure("restore_bookmark_unavailable");
  }
  return Object.freeze({ read, write });
}

export function createBackupImportController({ transport = backupImportApi,
  bookmark = createRestoreBookmark(), pollMs = 350, observationMs = 60000,
  requestMs = 10000 } = {}) {
  let state = Object.freeze({ phase: "choose", busy: false, error: null, fileName: "",
    progress: null, upload: null, preview: null, restore: null, attempted: false });
  let saved = null, bookmarkFailure = null, operation = null, generation = 0, destroyed = false;
  const listeners = new Set();
  try { saved = bookmark.read(); if (saved) state = Object.freeze({ ...state, phase: "unknown", restore: saved, attempted: true }); }
  catch (error) { bookmarkFailure = error; state = Object.freeze({ ...state, phase: "unknown", error }); }
  const publish = (patch) => {
    if (destroyed) return;
    state = Object.freeze({ ...state, ...patch });
    for (const listener of listeners) listener(state);
  };
  function assertIdentity(value, expected = state.restore) {
    if (!value || !requestId.test(value.id || "") || value.session_id !== value.id ||
        !projectId.test(value.project_id || "") || !digest.test(value.source_sha256 || "") ||
        (expected && (value.id !== expected.id || value.project_id !== expected.project_id ||
          value.source_sha256 !== expected.source_sha256))) throw failure("restore_identity_mismatch");
    return value;
  }
  function acceptResult(value) {
    assertIdentity(value);
    // Commit is an irreversible fact, even if cancellation or cleanup failed.
    const phase = value.committed ? "committed" : value.state === "aborted" ? "aborted" :
      value.state === "review" ? "review" : value.state === "not_accepted" ? "failed" : "restoring";
    publish({ restore: value, phase });
  }
  async function call(method, args, signal, { timeoutMs = requestMs, budgetMs = observationMs,
    transportOptions = {} } = {}) {
    const retry = ["current", "previews", "readPreview", "read"].includes(method);
    const timeoutCode = method === "upload" ? "backup_upload_timeout" : "restore_observation_timeout";
    const recovery = createRequestRecovery({ requestMs: timeoutMs, recoveryMs: retry ? budgetMs : timeoutMs,
      timeoutError: () => failure(timeoutCode) });
    const abort = () => recovery.dispose();
    signal.addEventListener("abort", abort, { once: true });
    if (signal.aborted) abort();
    // Only observations retry. Upload/apply/cancel keep their original identity
    // and one attempt, even if the reply is lost or ignores cancellation.
    try { return await recovery.request(signal => transport[method](...args, { ...transportOptions, signal }),
      { retry, retryable: error => error?.code === timeoutCode || isTransientReadError(error) }); }
    finally { recovery.dispose(); signal.removeEventListener("abort", abort); }
  }
  function delay(signal, ms = pollMs) {
    return new Promise((resolve, reject) => {
      const end = () => { signal.removeEventListener("abort", abort); resolve(); };
      const timer = setTimeout(end, ms);
      const abort = () => { clearTimeout(timer); signal.removeEventListener("abort", abort);
        reject(new DOMException("Cancelled", "AbortError")); };
      signal.addEventListener("abort", abort, { once: true });
      if (signal.aborted) abort();
    });
  }
  function remaining(until) {
    const budgetMs = until - Date.now();
    if (budgetMs <= 0) throw failure("restore_observation_timeout");
    return { budgetMs, timeoutMs: Math.min(requestMs, budgetMs) };
  }
  async function run(phase, work, interrupt = false) {
    if (destroyed || (state.busy && !interrupt)) return;
    operation?.abort();
    const controller = operation = new AbortController(), token = ++generation;
    const current = () => !destroyed && generation === token && !controller.signal.aborted;
    publish({ phase, busy: true, error: null });
    try { await work(controller.signal, current); }
    catch (error) {
      if (!current()) return;
      publish({ error, phase: bookmarkFailure || (state.restore && (state.attempted || saved)) ? "unknown" : "failed" });
    } finally { if (!destroyed && generation === token) { operation = null; publish({ busy: false }); } }
  }
  async function observePreview(value, signal, current) {
    const until = Date.now() + observationMs;
    while (current()) {
      publish({ preview: value, phase: "inspecting" });
      if (value.terminal) {
        if (value.state !== "succeeded" || !value.result_available)
          throw new ApiError(value.message || "Backup preview failed", { code: "backup_preview_failed" });
        publish({ phase: "preview" }); return;
      }
      if (Date.now() >= until) throw failure("restore_observation_timeout");
      await delay(signal, Math.min(pollMs, until - Date.now()));
      value = await call("readPreview", [value.id], signal, remaining(until));
    }
  }
  async function observeRestore(signal, current, initial) {
    const until = Date.now() + observationMs;
    let value = initial;
    while (current()) {
      value ||= await call("read", [state.restore.id], signal, remaining(until));
      if (!current()) return;
      acceptResult(value);
      if (value.terminal || ["review", "not_accepted"].includes(value.state)) return;
      if (Date.now() >= until) throw failure("restore_observation_timeout");
      await delay(signal, Math.min(pollMs, until - Date.now())); value = null;
    }
  }
  async function discover(signal, current) {
    const resident = await call("current", [], signal);
    if (!current()) return;
    if (!resident.empty) {
      assertIdentity(resident, null);
      publish({ restore: resident, attempted: resident.state !== "review" });
      if (resident.state !== "review") { bookmark.write(resident); saved = resident; }
      await observeRestore(signal, current, resident); return;
    }
    const preview = await call("previews", [], signal);
    if (!current()) return;
    if (preview) await observePreview(preview, signal, current);
    else if (state.upload || state.preview)
      publish({ phase: "failed", error: failure("backup_upload_incomplete") });
    else publish({ phase: "choose" });
  }
  async function disposeInputs(signal, current) {
    if (state.preview) await call("discardPreview", [state.preview.id], signal);
    if (state.upload) await call("discardUpload", [state.upload.id], signal);
    if (current()) publish({ preview: null, upload: null, fileName: "", progress: null });
  }
  return Object.freeze({
    get: () => state,
    subscribe(listener) { listeners.add(listener); listener(state); return () => listeners.delete(listener); },
    hasRecovery: () => Boolean(saved || state.phase === "unknown"),
    inspect() {
      return run("resolving", async (signal, current) => {
        if (state.restore) await observeRestore(signal, current);
        else if (bookmarkFailure) throw bookmarkFailure;
        else await discover(signal, current);
      });
    },
    choose(file) {
      if (state.phase !== "choose" || state.restore || state.preview || state.upload) return Promise.resolve();
      return run("hashing", async (signal, current) => {
        publish({ fileName: file?.name || "" });
        const upload = await call("upload", [file], signal, { timeoutMs: 120000, transportOptions: {
          onIdentity(value) { if (current()) publish({ upload: value }); },
          onProgress(progress) { if (current()) publish({ progress, phase: progress.phase }); } } });
        if (!current()) return;
        publish({ upload, phase: "inspecting", progress: null });
        // A lost preview POST response is recovered by discovery, not by
        // blindly creating another worker or uploading the file again.
        const preview = await call("preview", [upload.id], signal);
        if (current()) await observePreview(preview, signal, current);
      });
    },
    review(project) {
      if (state.phase !== "preview" || state.preview?.result?.legacy_partial || !projectId.test(project || ""))
        return Promise.resolve();
      return run("reviewing", async (signal, current) => {
        const value = await call("review", [state.preview.id, project], signal);
        if (!current()) return;
        assertIdentity(value, null);
        if (value.project_id !== project || value.preview_id !== state.preview.id ||
            value.source_sha256 !== state.preview.sha256 || value.state !== "review")
          throw failure("restore_identity_mismatch");
        publish({ restore: value, phase: "review", attempted: false });
      });
    },
    confirm() {
      if (state.phase !== "review" || state.busy) return Promise.resolve();
      return run("restoring", async (signal, current) => {
        const value = assertIdentity(state.restore);
        // Save before POST. Failure to save prevents acceptance rather than
        // leaving an untraceable request after reload.
        bookmark.write(value); saved = value; publish({ attempted: true });
        const result = await call("apply", [value.id], signal);
        if (current()) await observeRestore(signal, current, result);
      });
    },
    refresh() {
      return run(state.restore ? "resolving" : "inspecting", async (signal, current) => {
        if (state.restore) await observeRestore(signal, current);
        else if (bookmarkFailure) throw bookmarkFailure;
        else await discover(signal, current);
      });
    },
    cancel() {
      if (bookmarkFailure) return Promise.resolve();
      // Explicit cancellation is separate from hiding the dialog. Interrupt
      // only this page's observation; the server's token owns actual work.
      return run("resolving", async (signal, current) => {
        if (state.restore) {
          const previous = state.restore, attempted = state.attempted || !!saved;
          await call("cancel", [previous.id], signal);
          let value;
          try { value = await call("read", [previous.id], signal); }
          catch (error) {
            if (error.code !== "restore_request_not_found" || attempted) throw error;
            if (current()) publish({ restore: null, attempted: false, phase: state.preview ? "preview" : "choose" });
            return;
          }
          if (!current()) return;
          // Another client may have accepted this review concurrently. Pin
          // identity and query its irreversible outcome before allowing reset.
          bookmark.write(value); saved = value; publish({ attempted: true });
          await observeRestore(signal, current, value);
        } else {
          await disposeInputs(signal, current);
          if (current()) publish({ phase: "choose" });
        }
      }, true);
    },
    acknowledge() {
      const result = state.restore;
      if (!result?.terminal || result.cleanup_pending || result.restart_required || state.busy) return Promise.resolve();
      return run("resolving", async (signal, current) => {
        await call("cancel", [result.id], signal); // Retire the resident pin; receipt stays.
        await disposeInputs(signal, current);
        if (!current()) return;
        bookmark.write(null); saved = null;
        publish({ restore: null, attempted: false, phase: "choose" });
      });
    },
    destroy() { destroyed = true; ++generation; operation?.abort(); listeners.clear(); },
  });
}
