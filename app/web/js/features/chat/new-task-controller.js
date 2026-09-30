import { t } from "../../i18n.js";
import { projectDraftKey } from "./draft-store.js";
import { SESSION_TITLE_UTF8_LIMIT, sessionTitleUtf8Bytes } from "../sessions/session-title.js";

export function taskTitle(text, fallback = "") {
  const characters = Array.from(text.trim().split(/\r?\n/, 1)[0])
    .slice(0, 80);
  while (characters.length &&
      sessionTitleUtf8Bytes(characters.join("")) > SESSION_TITLE_UTF8_LIMIT)
    characters.pop();
  return characters.join("") || fallback;
}

function sameSubmission(a, b) {
  return a.id === b.id && a.text === b.text &&
    a.interrupt === b.interrupt && a.state === "prepared" &&
    JSON.stringify(a.attachments) === JSON.stringify(b.attachments) &&
    JSON.stringify(a.profile ?? null) === JSON.stringify(b.profile ?? null);
}

// Keep the unsent new-task intent in the global Home draft until its entire
// ordered list is copied to the new session draft. No queue POST happens here.
export function createNewTaskController({ draftStore, newId, createSession,
  findSession, onPersisted, onMigrated, onReview, onChange,
  isWritePaused = () => false }) {
  let pumping = false;
  let preparing = false;
  let migrating = false;
  let blocked = false;
  let createRejected = false;

  function changed() { onChange(); }

  async function sessionFor(task) {
    try {
      return await createSession({ project_id: task.project_id,
        client_session_id: task.session_id, title: task.title,
        agent_id: task.agent_id, model_id: task.model_id,
        reasoning_effort: task.reasoning_effort,
        permission_profile: task.permission_profile });
    } catch (error) {
      if (error?.status >= 400 && error.status < 500) throw error;
      // A lost create response is safe to inspect because the ID is stable.
      const existing = await findSession(task.project_id, task.session_id)
        .catch(() => null);
      if (existing) return existing;
      throw error;
    }
  }

  async function copyToSession(task, fresh) {
    const key = `${task.project_id}/${task.session_id}`;
    if (!await draftStore.ensureLoaded(key))
      throw new Error(t("composer.newTaskCopyFailed"));
    const source = draftStore.submissions("");
    const target = draftStore.submissions(key);
    if (target.length > source.length ||
        (!fresh && !target.length && source.length) ||
        target.some((item, index) =>
          !sameSubmission(item, source[index])))
      throw new Error(t("composer.newTaskReviewCopy"));
    for (const item of source.slice(target.length))
      if (!draftStore.appendSubmission(key, item))
        throw new Error(t("composer.newTaskCopyFailed"));
    const text = draftStore.text("");
    if (draftStore.text(key) && draftStore.text(key) !== text)
      throw new Error(t("composer.newTaskReviewCopy"));
    draftStore.edit(key, text, [], true);
    if (!await draftStore.flush(key))
      throw new Error(t("composer.newTaskCopyFailed"));
    for (const item of source) draftStore.clearSubmission("", item.id);
    draftStore.setNewTask(null);
    draftStore.clear("");
    if (!await draftStore.flush("")) {
      // The target stays prepared. A later inspection can complete the move.
      draftStore.setNewTask(task);
      for (const item of source) draftStore.appendSubmission("", item);
      draftStore.edit("", text, [], true);
      throw new Error(t("composer.newTaskReviewCopy"));
    }
    return key;
  }

  async function pump(reviewedCopy = false) {
    if (isWritePaused()) return false;
    if (pumping || blocked) return;
    const task = draftStore.newTask();
    if (!task) return;
    if (task.phase === "rejected") {
      createRejected = true;
      blocked = true;
      onReview();
      changed();
      return;
    }
    pumping = true;
    preparing = true;
    changed();
    try {
      const firstText = draftStore.submissions("")[0]?.text ??
        draftStore.text("");
      if (!await draftStore.flush(""))
        throw new Error(t("composer.newTaskSaveFailed"));
      // Once the global journal is durable, remove the exact submitted text
      // from its project editor. If the app exits here, the next pump repeats
      // this comparison before creating or replaying the session.
      const projectKey = projectDraftKey(task.project_id);
      if (!await draftStore.ensureLoaded(projectKey))
        throw new Error(t("composer.newTaskSaveFailed"));
      if (firstText && draftStore.text(projectKey) === firstText)
        draftStore.clear(projectKey);
      if (!await draftStore.flush(projectKey))
        throw new Error(t("composer.newTaskSaveFailed"));
      preparing = false;
      changed();
      try { await sessionFor(task); }
      catch (error) {
        createRejected = task.phase === "creating" &&
          error?.status >= 400 && error.status < 500;
        if (createRejected) {
          // Persist the definitive rejection before showing the retry action.
          // A reload must never retry a request that the server rejected.
          draftStore.setNewTask({ ...task, phase: "rejected" });
          if (!await draftStore.flush(""))
            throw new Error(t("composer.newTaskSaveFailed"));
        }
        throw error;
      }
      migrating = true;
      changed();
      const fresh = task.phase === "creating";
      if (fresh) {
        draftStore.setNewTask({ ...task, phase: "copying" });
        if (!await draftStore.flush(""))
          throw new Error(t("composer.newTaskCopyFailed"));
      }
      const key = await copyToSession({ ...task, phase: "copying" },
        fresh || reviewedCopy);
      createRejected = false;
      onMigrated(key);
    } catch (error) {
      blocked = true;
      onReview(error);
    } finally {
      preparing = false;
      migrating = false;
      pumping = false;
      changed();
    }
  }

  async function submit({ projectId, text, profile, fromComposer = true }) {
    if (isWritePaused()) return false;
    if (blocked || preparing || migrating)
      throw new Error(t("composer.newTaskBusy"));
    if (!await draftStore.ensureLoaded(""))
      throw new Error(t("composer.newTaskSaveFailed"));
    let task = draftStore.newTask();
    if (task && task.project_id !== projectId)
      throw new Error(t("composer.newTaskOtherProject"));
    if (task?.phase === "copying")
      throw new Error(t("composer.newTaskBusy"));
    if (!task && fromComposer) {
      const projectKey = projectDraftKey(projectId);
      if (!await draftStore.ensureLoaded(projectKey))
        throw new Error(t("composer.newTaskSaveFailed"));
      draftStore.capture(projectKey, text);
      if (!await draftStore.flush(projectKey))
        throw new Error(t("composer.newTaskSaveFailed"));
    }
    const id = task && !draftStore.submissions("").length
      ? task.session_id : newId();
    const item = { id, text, attachments: [], interrupt: false,
      state: "prepared", profile: { ...profile } };
    if (!task) {
      task = { project_id: projectId, session_id: id,
        title: taskTitle(text),
        agent_id: "mdo.default", model_id: profile.model_id,
        reasoning_effort: profile.reasoning_effort,
        permission_profile: profile.permission_profile,
        phase: "creating" };
      if (!draftStore.setNewTask(task))
        throw new Error(t("composer.newTaskSaveFailed"));
    }
    if (fromComposer) draftStore.capture("", text);
    if (!draftStore.appendSubmission("", item))
      throw new Error(t("composer.queueFull"));
    // Hand the editor to the next prompt immediately. The in-memory journal
    // remains dirty until the portable Home confirms this snapshot.
    onPersisted(item, projectId);
    changed();
    if (!await draftStore.flush("")) {
      blocked = true;
      onReview(new Error(t("composer.newTaskSaveFailed")));
      return false;
    }
    void pump();
    return true;
  }

  async function reconcile() {
    if (isWritePaused()) return false;
    if (!await draftStore.ensureLoaded("")) return false;
    if (draftStore.newTask()) await pump();
    return true;
  }

  async function createForAttachment({ projectId, title, profile }) {
    if (isWritePaused()) throw new Error(t("error.purgeReviewRequired"));
    if (blocked || migrating || pumping)
      throw new Error(t("composer.newTaskBusy"));
    if (!await draftStore.ensureLoaded(""))
      throw new Error(t("composer.newTaskSaveFailed"));
    if (draftStore.newTask() || draftStore.submissions("").length)
      throw new Error(t("composer.newTaskBusy"));
    const projectKey = projectDraftKey(projectId);
    if (!await draftStore.ensureLoaded(projectKey) ||
        !await draftStore.flush(projectKey))
      throw new Error(t("composer.newTaskSaveFailed"));
    if (draftStore.text(projectKey))
      draftStore.capture("", draftStore.text(projectKey));
    const sessionId = newId();
    if (!draftStore.setNewTask({ project_id: projectId,
      session_id: sessionId, title, agent_id: "mdo.default",
      model_id: profile.model_id,
      reasoning_effort: profile.reasoning_effort,
      permission_profile: profile.permission_profile,
      phase: "creating" }))
      throw new Error(t("composer.newTaskSaveFailed"));
    await pump();
    if (blocked) throw new Error(t("composer.newTaskReview"));
    return { projectId, sessionId };
  }

  async function review(profile) {
    if (isWritePaused()) return false;
    if (createRejected) {
      if (!draftStore.reseedNewTask(newId(), profile))
        throw new Error(t("composer.newTaskSaveFailed"));
      createRejected = false;
    }
    blocked = false;
    await pump(true);
    return !blocked;
  }

  return Object.freeze({ submit, reconcile, review, createForAttachment,
    isBusy() { return pumping || (blocked && !createRejected); },
    isPreparing() { return preparing; },
    isBlocked() { return blocked; },
    canChangeProfile() { return blocked && createRejected; },
    isMigrating() { return migrating; },
  });
}
