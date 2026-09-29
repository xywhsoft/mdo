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

function profile(value) {
  if (value == null) return null;
  if (typeof value !== "object" || Array.isArray(value) ||
      Object.keys(value).length !== 3 ||
      !["read-only", "balanced", "full-access"].includes(
        value.permission_profile)) return undefined;
  const fields = [["model_id", 128], ["reasoning_effort", 32],
    ["permission_profile", 32]];
  if (fields.some(([name, limit]) => typeof value[name] !== "string" ||
      !value[name] || value[name].includes("\0") ||
      new TextEncoder().encode(value[name]).length > limit)) return undefined;
  return { model_id: value.model_id,
    reasoning_effort: value.reasoning_effort,
    permission_profile: value.permission_profile };
}

function sameProfile(a, b) {
  return (!a && !b) || (a && b && a.model_id === b.model_id &&
    a.reasoning_effort === b.reasoning_effort &&
    a.permission_profile === b.permission_profile);
}

function submission(value) {
  if (!value || !/^[0-9a-f]{32}$/.test(value.id) ||
      typeof value.text !== "string" || typeof value.interrupt !== "boolean" ||
      !["prepared", "posting", "rejected"].includes(value.state))
    return null;
  const attachments = imageIds(value.attachments);
  const snapshot = profile(value.profile);
  if (!Array.isArray(value.attachments) ||
      snapshot === undefined ||
      (Object.hasOwn(value, "profile") && snapshot === null) ||
      attachments.length !== value.attachments.length ||
      attachments.length > 4 ||
      new TextEncoder().encode(value.text).length > MAX_DRAFT_BYTES ||
      (!value.text && !attachments.length)) return null;
  return { id: value.id, text: value.text, attachments,
    interrupt: value.interrupt, state: value.state,
    ...(snapshot ? { profile: snapshot } : {}) };
}

function sameSubmission(a, b) {
  return (!a && !b) || (a && b && a.id === b.id && a.text === b.text &&
    a.interrupt === b.interrupt && a.state === b.state &&
    sameIds(a.attachments, b.attachments) && sameProfile(a.profile, b.profile));
}

function sameSubmissionPayload(a, b) {
  return a?.id === b?.id && a?.text === b?.text &&
    a?.interrupt === b?.interrupt && sameIds(a.attachments, b.attachments) &&
    sameProfile(a.profile, b.profile);
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
  if (key.startsWith("project:"))
    return `/projects/${resourceId(key.slice("project:".length), "project")}/draft`;
  const [projectId, sessionId, extra] = key.split("/");
  if (extra !== undefined) throw new TypeError("Invalid draft owner");
  return `/projects/${resourceId(projectId, "project")}/sessions/${resourceId(sessionId, "session")}/draft`;
}

export function projectDraftKey(projectId) {
  return `project:${resourceId(projectId, "project")}`;
}

export function createDraftStore({ onRestore, onError, onSaved, onLoaded = () => {} }) {
  const entries = new Map();
  const encoder = new TextEncoder();
  let selected = "";

  function entry(key) {
    let value = entries.get(key);
    if (!value) {
      value = { text: "", attachments: [], revision: 0, loaded: false, dirty: false,
        uncertainRun: false, submissions: [], newTask: null,
        composerProfile: null, profileEdited: false, conflict: false,
        unpersisted: new Set(), error: null, oversized: false, loading: null,
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
        const storedProfile = profile(response.data.composer_profile);
        if (storedNewTask === undefined || storedProfile === undefined ||
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
        for (const item of storedSubmissions) current.unpersisted.delete(item.id);
        current.newTask ??= storedNewTask;
        if (!current.profileEdited) current.composerProfile = storedProfile;
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
        const stagedProfile = current.composerProfile &&
          { ...current.composerProfile };
        if (encoder.encode(text).length > MAX_DRAFT_BYTES) {
          current.oversized = true;
          current.error = Object.assign(new Error(t("draft.tooLarge", {},
            "草稿超过 64 KiB 保存上限")), { code: "draft_too_large" });
          if (selected === key) onError(current.error);
          return;
        }
        current.oversized = false;
        current.dirty = false;
        try {
          const body = { revision: current.revision, text, attachments,
            run_admission_uncertain: uncertainRun,
            submissions: stagedSubmissions,
            composer_profile: stagedProfile };
          if (!key) body.new_task = stagedNewTask;
          const keepalive = encoder.encode(JSON.stringify(body)).length <= 60 * 1024;
          const response = await api.put(endpoint(key), body, { keepalive });
          current.revision = Number(response.data.revision);
          current.error = null;
          if (sameProfile(current.composerProfile, stagedProfile))
            current.profileEdited = false;
          for (const item of stagedSubmissions)
            current.unpersisted.delete(item.id);
          if (current.text !== text || !sameIds(current.attachments, attachments) ||
              current.uncertainRun !== uncertainRun ||
              !sameSubmissions(current.submissions, stagedSubmissions) ||
              !sameProfile(current.composerProfile, stagedProfile) ||
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
      if (current.dirty && !current.conflict && !current.oversized && current.loaded)
        schedule(key);
    }
    return current.loaded && !current.dirty && !current.conflict;
  }

  function applySessionResponse(key, data) {
    const current = entry(key);
    const remoteSubmissions = submissions(data?.submissions);
    const remoteProfile = profile(data?.composer_profile);
    const revision = Number(data?.revision);
    if (!remoteSubmissions || remoteProfile === undefined ||
        !Number.isSafeInteger(revision) ||
        revision < 0 || typeof data.text !== "string" ||
        !Array.isArray(data.attachments) ||
        imageIds(data.attachments).length !== data.attachments.length)
      throw new Error(t("draft.submissionConflict"));
    if (revision < current.revision) return [...current.submissions];
    const sameRevision = revision === current.revision;
    const remoteAttachments = imageIds(data.attachments);
    const localOnly = current.submissions.filter((item) =>
      current.unpersisted.has(item.id) &&
      !remoteSubmissions.some((remote) => remote.id === item.id));
    const combined = submissions([...remoteSubmissions, ...localOnly]);
    if (!combined || current.submissions.some((item) =>
        current.unpersisted.has(item.id) &&
        remoteSubmissions.some((remote) => remote.id === item.id &&
          !sameSubmissionPayload(remote, item))))
      throw new Error(t("draft.submissionConflict"));
    const sameText = current.text === data.text &&
      sameIds(current.attachments, remoteAttachments) &&
      sameProfile(current.composerProfile, remoteProfile);
    const savingLocalEdit = Boolean(current.saving);
    const hadLocalEdit = current.dirty || current.conflict || savingLocalEdit;
    current.revision = revision;
    current.loaded = true;
    current.submissions = combined;
    for (const item of remoteSubmissions) current.unpersisted.delete(item.id);
    current.uncertainRun ||= data.run_admission_uncertain === true;
    if (!savingLocalEdit && !localOnly.length && sameText && current.uncertainRun ===
        (data.run_admission_uncertain === true)) {
      window.clearTimeout(current.timer);
      current.timer = 0;
      current.dirty = false;
      current.conflict = false;
      current.error = null;
      if (selected === key) onSaved();
    } else if (!localOnly.length && !hadLocalEdit) {
      current.text = data.text;
      current.attachments = remoteAttachments;
      current.composerProfile = remoteProfile;
      current.profileEdited = false;
      current.error = null;
      if (selected === key) {
        onRestore(current.text, [...current.attachments],
          current.uncertainRun, current.submissions[0] ?? null,
          [...current.submissions]);
        onSaved();
      }
    } else if (savingLocalEdit) {
      // flush() temporarily clears dirty while its PUT is in flight. A
      // concurrent response must not restore the pre-save composer text.
    } else if (sameRevision && current.dirty && !current.conflict) {
      // A queue poll can read the last saved draft during this tab's debounce.
      // An unchanged revision cannot prove another writer changed the draft.
      // Keep the local edit and let its scheduled PUT persist it.
    } else if (!current.conflict && !data.text &&
               !remoteAttachments.length) {
      current.dirty = true;
      schedule(key, true);
    } else {
      current.conflict = true;
      current.error = Object.assign(new Error(t("draft.submissionConflict")),
        { code: "draft_conflict" });
      if (selected === key) onError(current.error);
    }
    return combined;
  }

  async function refreshSessionSubmissions(key) {
    const current = entry(key);
    if (current.saving) await current.saving;
    if (!current.loaded) await load(key);
    if (!current.loaded) return false;
    const response = await api.get(endpoint(key));
    applySessionResponse(key, response.data);
    return true;
  }

  async function changeSessionSubmissionState(key, id, state) {
    const current = entry(key);
    if (current.saving) await current.saving;
    const path = `${endpoint(key)}/submissions/${resourceId(id, "submission")}`;
    let response;
    try { response = await api.put(path, { state }); }
    catch (error) {
      if (!["network_error", "draft_state_conflict",
            "draft_submission_not_found"].includes(error?.code)) throw error;
      response = await api.get(endpoint(key));
    }
    const items = applySessionResponse(key, response.data);
    return items.some((item) => item.id === id && item.state === state);
  }

  async function removeSessionSubmission(key, id) {
    const current = entry(key);
    if (current.saving) await current.saving;
    const path = `${endpoint(key)}/submissions/${resourceId(id, "submission")}`;
    let response;
    try { response = await api.delete(path); }
    catch (error) {
      if (error?.code !== "network_error") throw error;
      response = await api.get(endpoint(key));
    }
    const items = applySessionResponse(key, response.data);
    return !items.some((item) => item.id === id);
  }

  function edit(key, text, attachments = entry(key).attachments, immediate = false) {
    const current = entry(key);
    const ids = key ? imageIds(attachments) : [];
    if (current.text === text && sameIds(current.attachments, ids) &&
        !current.dirty) return;
    current.text = text;
    current.attachments = ids;
    current.dirty = true;
    current.oversized = false;
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
    else if (current.dirty && !current.oversized) schedule(key, true);
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
    current.unpersisted.add(next.id);
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
    composerProfile(key) { return entry(key).composerProfile &&
      { ...entry(key).composerProfile }; },
    profileSaveState(key) {
      const current = entry(key);
      return !current.profileEdited ? "saved" :
        current.error || current.conflict ? "error" : "saving";
    },
    setComposerProfile(key, value) {
      const current = entry(key);
      const next = profile(value);
      if (next === undefined || sameProfile(current.composerProfile, next))
        return false;
      current.composerProfile = next;
      current.profileEdited = true;
      current.dirty = true;
      schedule(key, true);
      return true;
    },
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
    reseedNewTask(sessionId, selectedProfile) {
      const current = entry("");
      const task = current.newTask;
      const first = current.submissions[0];
      if (!task || task.phase !== "creating" ||
          (first && first.id !== task.session_id) ||
          current.submissions.some((item) => item.id === sessionId)) return false;
      const nextTask = newTask({ ...task, session_id: sessionId,
        model_id: selectedProfile.model_id,
        reasoning_effort: selectedProfile.reasoning_effort,
        permission_profile: selectedProfile.permission_profile });
      // A definite create rejection accepted no task. The user's reviewed
      // profile replaces every still-pending initial-task snapshot together.
      const nextItems = current.submissions.map((item, index) => ({
        ...item, id: index === 0 ? sessionId : item.id,
        profile: { ...selectedProfile },
      }));
      if (!nextTask || !submissions(nextItems)) return false;
      current.newTask = nextTask;
      current.submissions = nextItems;
      current.dirty = true;
      schedule("", true);
      return true;
    },
    submission(key) { return entry(key).submissions[0] ?? null; },
    submissions(key) { return [...entry(key).submissions]; },
    isSubmissionDurable(key, value) {
      const current = entry(key);
      return !current.unpersisted.has(value.id) &&
        current.submissions.some((item) => sameSubmissionPayload(item, value));
    },
    appendSubmission(key, value) { return insertSubmission(key, value); },
    refreshSessionSubmissions,
    changeSessionSubmissionState,
    removeSessionSubmission,
    async persistUnconfirmedSubmission(key, value) {
      const current = entry(key);
      if (!key || !submission(value) || value.state !== "prepared")
        return false;
      const local = current.submissions.find((item) => item.id === value.id);
      if (local && !sameSubmissionPayload(local, value)) return false;
      const path = `${endpoint(key)}/submissions`;
      let response;
      // Another tab may already have saved and advanced this ID. Check the
      // durable record even if a concurrent refresh cleared unpersisted.
      try { response = await api.get(endpoint(key)); }
      catch { /* The keyed append below is safe to retry after a lost GET. */ }
      if (response) {
        const existing = submissions(response.data?.submissions)?.find(
          (item) => item.id === value.id);
        if (existing) {
          if (!sameSubmissionPayload(existing, value)) return false;
          applySessionResponse(key, response.data);
          return true;
        }
      }
      if (!current.unpersisted.has(value.id)) return false;
      try { response = await api.post(path, value); }
      catch (error) {
        // A failed draft PUT or a lost append response may already have
        // committed. Inspect the same ID before making any new intent.
        try { response = await api.get(endpoint(key)); }
        catch { current.error = error; if (selected === key) onError(error); return false; }
      }
      const remote = response.data;
      const persisted = submissions(remote?.submissions);
      const match = persisted?.find((item) => item.id === value.id);
      if (!match || !sameSubmissionPayload(match, value)) return false;
      applySessionResponse(key, remote);
      return true;
    },
    insertSubmission,
    updateSubmissionState(key, id, state) {
      const current = entry(key);
      const item = current.submissions.find((candidate) => candidate.id === id);
      if (!item || !["prepared", "posting", "rejected"].includes(state)) return false;
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
      current.unpersisted.delete(id);
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
