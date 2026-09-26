import { t } from "../../i18n.js";

function owner(key) {
  const [projectId, sessionId] = key.split("/");
  return { projectId, sessionId };
}

function matches(item, submission) {
  return item?.id === submission.id &&
    item.text === submission.text.trim() &&
    item.priority === submission.interrupt &&
    JSON.stringify(item.attachments ?? []) ===
      JSON.stringify(submission.attachments);
}

// The draft is the write-ahead log. Only its first entry may enter the queue;
// later entries remain durable and ordered while that request is unresolved.
export function createSubmissionController({ draftStore, promptQueue,
  onPersisted, onPromoted, onReview, onRestored, onChange }) {
  const pumping = new Set();
  const releasing = new Set();
  const reviewed = new Map();

  function requestReview(key, submission, error) {
    if (reviewed.get(key) === submission.id) return;
    reviewed.set(key, submission.id);
    onReview(key, submission, error);
  }

  async function release(key, submission) {
    releasing.add(key);
    onChange(key);
    try {
      if (!draftStore.clearSubmission(key, submission.id)) return false;
      if (await draftStore.flush(key)) {
        reviewed.delete(key);
        return true;
      }
      draftStore.insertSubmission(key, submission, 0);
      return false;
    } finally {
      releasing.delete(key);
      onChange(key);
    }
  }

  async function pump(key) {
    if (!key || pumping.has(key)) return;
    pumping.add(key);
    onChange(key);
    try {
      const { projectId, sessionId } = owner(key);
      while (draftStore.submissions(key).length) {
        const submission = draftStore.submissions(key)[0];
        if (promptQueue.hasStaged(projectId, sessionId)) return;
        if (submission.state === "posting") {
          requestReview(key, submission);
          return;
        }
        if (!draftStore.updateSubmissionState(key, submission.id, "posting") ||
            !await draftStore.flush(key)) {
          requestReview(key, submission);
          return;
        }
        const posting = { ...submission, state: "posting" };
        try {
          if (!await promptQueue.stage(projectId, sessionId, posting,
            { first: posting.interrupt }))
            throw new Error(t("composer.queueFull", {},
              "待发送队列已满（最多 20 条）"));
          const item = promptQueue.find(projectId, sessionId, posting.id);
          if (!matches(item, posting) || !await release(key, posting)) {
            requestReview(key, posting);
            return;
          }
          await promptQueue.promote(projectId, sessionId, posting.id);
          onPromoted(key, posting);
        } catch (error) {
          requestReview(key, posting, error);
          return;
        }
      }
    } finally {
      pumping.delete(key);
      onChange(key);
    }
  }

  async function reconcile(key) {
    if (!key || pumping.has(key)) return false;
    if (!await draftStore.ensureLoaded(key)) return false;
    const first = draftStore.submissions(key)[0];
    if (!first) return true;
    const { projectId, sessionId } = owner(key);
    const item = promptQueue.find(projectId, sessionId, first.id);
    if (item) {
      if (!matches(item, first) || !await release(key, first)) {
        requestReview(key, first);
        return false;
      }
      // A surviving staged item needs a deliberate continue after a reload.
      // A pending/sending item already has its place in the queue.
      if (item.state !== "staged") void pump(key);
      return true;
    }
    if (first.state === "posting") {
      requestReview(key, first);
      return false;
    }
    void pump(key);
    return true;
  }

  async function submit(key, text, attachments, interrupt) {
    if (!key || releasing.has(key))
      throw new Error(t("composer.submissionBusy"));
    const submission = { id: promptQueue.newId(), text,
      attachments: [...attachments], interrupt, state: "prepared" };
    draftStore.capture(key, text, attachments);
    if (!draftStore.appendSubmission(key, submission))
      throw new Error(t("composer.queueFull", {},
        "待发送队列已满（最多 20 条）"));
    onChange(key);
    if (!await draftStore.flush(key)) {
      requestReview(key, submission);
      return false;
    }
    onPersisted(key, submission);
    void pump(key);
    return true;
  }

  async function review(key) {
    if (!key || pumping.has(key) || releasing.has(key)) return false;
    const first = draftStore.submissions(key)[0];
    if (!first) return true;
    reviewed.delete(key);
    const { projectId, sessionId } = owner(key);
    await promptQueue.select(projectId, sessionId);
    if (promptQueue.find(projectId, sessionId, first.id))
      return reconcile(key);
    if (first.state !== "posting") {
      void pump(key);
      return true;
    }
    if (draftStore.submissions(key).length > 1) {
      // The user has checked the trace and explicitly retries the same ID.
      draftStore.updateSubmissionState(key, first.id, "prepared");
      if (!await draftStore.flush(key)) return false;
      void pump(key);
      return true;
    }
    const restored = draftStore.restoreUnsent(key, first.text,
      first.attachments);
    if (!await release(key, first)) return false;
    onRestored(key, restored);
    return true;
  }

  return Object.freeze({
    submit, pump, reconcile, review,
    isBusy(key) { return pumping.has(key); },
    isReleasing(key) { return releasing.has(key); },
  });
}
