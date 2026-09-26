import { api, resourceId } from "../../api/client.js";
import { t } from "../../i18n.js";

const SAVE_DELAY_MS = 300;
const MAX_DRAFT_BYTES = 65536;
const MAX_SUBMISSIONS = 20;
const MAX_SUBMISSION_BYTES = 192 * 1024;

function imageIds(value) {
  return Array.isArray(value) ? value.filter((id) =>
    typeof id === "string" && /^[0-9a-f]{32}$/.test(id)).slice(0, 4) : [];
}

function sameIds(a, b) {
  return a.length === b.length && a.every((id, index) => id === b[index]);
}

function submission(value) {
  if (!value || !/^[0-9a-f]{32}$/.test(value.id) ||
      typeof value.text !== "string" || typeof value.interrupt !== "boolean" ||
      !["prepared", "posting"].includes(value.state))
    return null;
  const attachments = imageIds(value.attachments);
  if (!Array.isArray(value.attachments) ||
      attachments.length !== value.attachments.length ||
      attachments.length > 4 ||
      new TextEncoder().encode(value.text).length > MAX_DRAFT_BYTES ||
      (!value.text && !attachments.length)) return null;
  return { id: value.id, text: value.text, attachments,
    interrupt: value.interrupt, state: value.state };
}

function sameSubmission(a, b) {
  return (!a && !b) || (a && b && a.id === b.id && a.text === b.text &&
    a.interrupt === b.interrupt && a.state === b.state &&
    sameIds(a.attachments, b.attachments));
}

function submissions(value) {
  if (!Array.isArray(value) || value.length > MAX_SUBMISSIONS) return null;
  const items = value.map(submission);
  const encoder = new TextEncoder();
  if (items.some((item) => !item) ||
      new Set(items.map((item) => item.id)).size !== items.length ||
      items.reduce((total, item) => total + encoder.encode(item.text).length,
        0) > MAX_SUBMISSION_BYTES) return null;
  return items;
}

function sameSubmissions(a, b) {
  return a.length === b.length && a.every((item, index) =>
    sameSubmission(item, b[index]));
}

function newTask(value) {
  if (value == null) return null;
  if (typeof value !== "object" || Array.isArray(value) ||
      !/^[0-9a-f]{32}$/.test(value.session_id) ||
      !/^[A-Za-z0-9_-][A-Za-z0-9_.-]{0,63}$/.test(value.project_id) ||
      !["creating", "copying"].includes(value.phase)) return undefined;
  const fields = ["title", "agent_id", "model_id",
    "reasoning_effort", "permission_profile"];
  const limits = [256, 128, 128, 32, 32];
  if (fields.some((field, index) => typeof value[field] !== "string" ||
      !value[field] || new TextEncoder().encode(value[field]).length >
        limits[index])) return undefined;
  return { project_id: value.project_id, session_id: value.session_id,
    title: value.title, agent_id: value.agent_id, model_id: value.model_id,
    reasoning_effort: value.reasoning_effort,
    permission_profile: value.permission_profile, phase: value.phase };
}

function sameNewTask(a, b) {
  return (!a && !b) || (a && b &&
    Object.keys(a).every((key) => a[key] === b[key]));
}

function endpoint(key) {
  if (!key) return "/draft";
  const [projectId, sessionId, extra] = key.split("/");
  if (extra !== undefined) throw new TypeError("Invalid draft owner");
  return `/projects/${resourceId(projectId, "project")}/sessions/${resourceId(sessionId, "session")}/draft`;
}

export function createDraftStore({ onRestore, onError, onSaved, onLoaded = () => {} }) {
  const entries = new Map();
  const encoder = new TextEncoder();
  let selected = "";

  function entry(key) {
    let value = entries.get(key);
    if (!value) {
      value = { text: "", attachments: [], revision: 0, loaded: false, dirty: false,
        uncertainRun: false, submissions: [], newTask: null, conflict: false,
        error: null, loading: null,
        saving: null, timer: 0 };
      entries.set(key, value);
    }
    return value;
  }

  function schedule(key, immediate = false) {
    const current = entry(key);
    window.clearTimeout(current.timer);
    if (current.conflict) return;
    current.timer = window.setTimeout(() => { void flush(key); },
      immediate ? 0 : SAVE_DELAY_MS);
  }

  async function load(key) {
    const current = entry(key);
    if (current.loaded) return;
    if (current.loading) return current.loading;
    current.loading = (async () => {
      try {
        const response = await api.get(endpoint(key));
        current.revision = Number(response.data.revision);
        // A local edit may precede the GET response. Never clear a persisted
        // review guard while saving that edit.
        current.uncertainRun ||= response.data.run_admission_uncertain === true;
        const storedSubmissions = submissions(response.data.submissions ??
          (response.data.submission == null ? [] : [{
            ...response.data.submission, state: "posting",
          }]));
        if (!storedSubmissions)
          throw new Error(t("draft.submissionConflict"));
        const storedNewTask = newTask(response.data.new_task);
        if (storedNewTask === undefined ||
            (current.newTask && storedNewTask &&
             !sameNewTask(current.newTask, storedNewTask))) {
          current.conflict = true;
          throw new Error(t("draft.submissionConflict"));
        }
        if (current.submissions.length && storedSubmissions.length &&
            !sameSubmissions(current.submissions, storedSubmissions)) {
          current.conflict = true;
          throw new Error(t("draft.submissionConflict"));
        }
        current.loaded = true;
        current.error = null;
        if (!current.submissions.length) current.submissions = storedSubmissions;
        current.newTask ??= storedNewTask;
        if (!current.dirty) {
          current.text = response.data.text ?? "";
          current.attachments = imageIds(response.data.attachments);
        } else schedule(key, true);
        if (selected === key) onRestore(current.text,
          [...current.attachments], current.uncertainRun,
          current.submissions[0] ?? null, [...current.submissions]);
        if (selected === key && !current.dirty) onSaved();
        if (selected === key) onLoaded();
      } catch (error) {
        current.error = error;
        if (selected === key) onError(error);
      } finally { current.loading = null; }
    })();
    return current.loading;
  }

  async function flush(key) {
    const current = entry(key);
    window.clearTimeout(current.timer);
    current.timer = 0;
    if (current.saving) {
      await current.saving;
      return current.loaded && !current.dirty && !current.conflict;
    }
    current.saving = (async () => {
      if (!current.loaded) await load(key);
      if (!current.loaded || current.conflict) return;
      while (current.dirty) {
        const text = current.text;
        const attachments = [...current.attachments];
        const uncertainRun = current.uncertainRun;
        const stagedSubmissions = [...current.submissions];
        const stagedNewTask = current.newTask && { ...current.newTask };
        if (encoder.encode(text).length > MAX_DRAFT_BYTES) {
          if (selected === key) onError(new Error(t("draft.tooLarge", {},
            "草稿超过 64 KiB 保存上限")));
          return;
        }
        current.dirty = false;
        try {
          const body = { revision: current.revision, text, attachments,
            run_admission_uncertain: uncertainRun,
            submissions: stagedSubmissions };
          if (!key) body.new_task = stagedNewTask;
          const keepalive = encoder.encode(JSON.stringify(body)).length <= 60 * 1024;
          const response = await api.put(endpoint(key), body, { keepalive });
          current.revision = Number(response.data.revision);
          current.error = null;
          if (current.text !== text || !sameIds(current.attachments, attachments) ||
              current.uncertainRun !== uncertainRun ||
              !sameSubmissions(current.submissions, stagedSubmissions) ||
              !sameNewTask(current.newTask, stagedNewTask))
            current.dirty = true;
          if (selected === key && !current.dirty) onSaved();
        } catch (error) {
          current.dirty = true;
          current.error = error;
          if (error?.code === "draft_conflict") current.conflict = true;
          if (selected === key) onError(error);
          return;
        }
      }
    })();
    try { await current.saving; }
    finally {
      current.saving = null;
      if (current.dirty && !current.conflict && current.loaded)
        schedule(key);
    }
    return current.loaded && !current.dirty && !current.conflict;
  }

  function edit(key, text, attachments = entry(key).attachments, immediate = false) {
    const current = entry(key);
    const ids = key ? imageIds(attachments) : [];
    if (current.text === text && sameIds(current.attachments, ids) &&
        !current.dirty) return;
    current.text = text;
    current.attachments = ids;
    current.dirty = true;
    schedule(key, immediate);
  }

  function select(key) {
    selected = key;
    const current = entry(key);
    onRestore(current.text, [...current.attachments], current.uncertainRun,
      current.submissions[0] ?? null, [...current.submissions]);
    if (current.error) onError(current.error);
    else onSaved();
    if (!current.loaded) void load(key);
    else if (current.dirty) schedule(key, true);
  }

  window.addEventListener("pagehide", () => {
    for (const key of entries.keys())
      if (entry(key).dirty) void flush(key);
  });

  function insertSubmission(key, value, index = entry(key).submissions.length) {
    const current = entry(key);
    const next = submission(value);
    if (!next || !Number.isInteger(index) || index < 0 ||
        index > current.submissions.length ||
        current.submissions.length >= MAX_SUBMISSIONS ||
        current.submissions.some((item) => item.id === next.id) ||
        !submissions([...current.submissions, next])) return false;
    current.submissions = [
      ...current.submissions.slice(0, index), next,
      ...current.submissions.slice(index),
    ];
    current.dirty = true;
    schedule(key, true);
    return true;
  }

  return Object.freeze({
    select,
    edit,
    editAttachments(key, attachments, immediate = false) {
      edit(key, entry(key).text, attachments, immediate);
    },
    capture(key, text, attachments = []) {
      if (entry(key).text !== text ||
          !sameIds(entry(key).attachments, attachments))
        edit(key, text, attachments, true);
    },
    clear(key) { edit(key, "", [], true); },
    isLoaded(key) { return entry(key).loaded; },
    async ensureLoaded(key) {
      await load(key);
      return entry(key).loaded;
    },
    isRunUncertain(key) { return entry(key).uncertainRun; },
    text(key) { return entry(key).text; },
    newTask(key = "") { return key ? null : entry("").newTask; },
    setNewTask(value) {
      const current = entry("");
      const next = newTask(value);
      if (next === undefined || sameNewTask(current.newTask, next)) return false;
      current.newTask = next;
      current.dirty = true;
      schedule("", true);
      return true;
    },
    submission(key) { return entry(key).submissions[0] ?? null; },
    submissions(key) { return [...entry(key).submissions]; },
    appendSubmission(key, value) { return insertSubmission(key, value); },
    insertSubmission,
    updateSubmissionState(key, id, state) {
      const current = entry(key);
      const item = current.submissions.find((candidate) => candidate.id === id);
      if (!item || !["prepared", "posting"].includes(state)) return false;
      if (item.state === state) return true;
      current.submissions = current.submissions.map((candidate) =>
        candidate.id === id ? { ...candidate, state } : candidate);
      current.dirty = true;
      schedule(key, true);
      return true;
    },
    stageSubmission(key, value) {
      const current = entry(key);
      if (current.submissions.length) return false;
      return insertSubmission(key, { ...value, state: "posting" });
    },
    clearSubmission(key, id) {
      const current = entry(key);
      if (!current.submissions.some((item) => item.id === id)) return false;
      current.submissions = current.submissions.filter((item) => item.id !== id);
      current.dirty = true;
      schedule(key, true);
      return true;
    },
    setRunUncertain(key, uncertain) {
      const current = entry(key);
      if (current.uncertainRun === Boolean(uncertain)) return;
      current.uncertainRun = Boolean(uncertain);
      current.dirty = true;
      schedule(key, true);
    },
    clearIfMatches(key, text, attachments = []) {
      const current = entry(key);
      if (current.text !== text ||
          !sameIds(current.attachments, imageIds(attachments))) return false;
      edit(key, "", [], true);
      return true;
    },
    restoreUnsent(key, text, attachments = []) {
      const current = entry(key);
      const submitted = imageIds(attachments);
      if (current.text === text && sameIds(current.attachments, submitted))
        return { text, attachments: submitted, merged: false };
      const otherText = Boolean(current.text && current.text !== text);
      const merged = otherText || current.attachments.some((id) =>
        !submitted.includes(id));
      const nextText = otherText && text ? `${text}\n\n${current.text}`
        : current.text || text;
      const nextAttachments = [...submitted,
        ...current.attachments.filter((id) => !submitted.includes(id))].slice(0, 4);
      edit(key, nextText, nextAttachments, true);
      return { text: nextText, attachments: nextAttachments, merged };
    },
    flush,
  });
}
