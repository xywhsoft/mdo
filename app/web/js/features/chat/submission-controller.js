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
      JSON.stringify(submission.attachments) &&
    ((!item.profile && !submission.profile) ||
      (item.profile && submission.profile &&
        item.profile.model_id === submission.profile.model_id &&
        item.profile.reasoning_effort === submission.profile.reasoning_effort &&
        item.profile.permission_profile ===
          submission.profile.permission_profile));
}

// The draft is the write-ahead log. Only its first entry may enter the queue;
// later entries remain durable and ordered while that request is unresolved.
export function createSubmissionController({ draftStore, promptQueue,
  onPersisted, onPromoted, onConsumed, onReview, onRestored, onChange }) {
  const pumping = new Set();
  const releasing = new Set();
  const submitting = new Set();
  const unacknowledged = new Map();
  const reviewed = new Map();

  function acknowledge(key, submission) {
    if (unacknowledged.get(key)?.id !== submission.id) return;
    unacknowledged.delete(key);
    onPersisted(key, submission);
    onChange(key);
  }

  async function verifyUnacknowledged(key) {
    const submission = unacknowledged.get(key);
    if (!submission) return;
    const { projectId, sessionId } = owner(key);
    let confirmed = draftStore.isSubmissionDurable?.(key, submission) ?? false;
    if (!confirmed) confirmed = matches(promptQueue.find(projectId,
      sessionId, submission.id), submission);
    if (!confirmed) {
      try { confirmed = Boolean(await promptQueue.receipt(projectId,
        sessionId, submission.id)); }
      catch { /* An unreadable receipt is not proof of acceptance. */ }
    }
    if (!confirmed) return;
    acknowledge(key, submission);
    try { await draftStore.refreshSessionSubmissions(key); }
    catch { /* The next bounded refresh can clear a stale draft conflict. */ }
  }

  function requestReview(key, submission, error) {
    if (reviewed.get(key) === submission.id) return;
    reviewed.set(key, submission.id);
    onReview(key, submission, error);
  }

  async function release(key, submission) {
    releasing.add(key);
    onChange(key);
    try {
      if (await draftStore.removeSessionSubmission(key, submission.id)) {
        reviewed.delete(key);
        return true;
      }
      return false;
    } finally {
      releasing.delete(key);
      onChange(key);
    }
  }

  async function consumeReceipt(key, submission, receipt) {
    if (!receipt || !await release(key, submission)) return false;
    onConsumed?.(key, receipt);
    return true;
  }

  async function pump(key) {
    if (!key || pumping.has(key)) return;
    pumping.add(key);
    onChange(key);
    try {
      const { projectId, sessionId } = owner(key);
      if (!await draftStore.refreshSessionSubmissions(key)) return;
      while (draftStore.submissions(key).length) {
        const submission = draftStore.submissions(key)[0];
        if (promptQueue.hasStaged(projectId, sessionId)) return;
        if (submission.state === "posting" || submission.state === "rejected") {
          requestReview(key, submission);
          return;
        }
        try {
          if (!await draftStore.changeSessionSubmissionState(key,
              submission.id, "posting")) {
            // Another tab may have claimed this intent and be staging it now.
            // Queue reconciliation will resolve its outcome; this tab must
            // neither send a second POST nor report a premature failure.
            const current = draftStore.submissions(key).find((item) =>
              item.id === submission.id);
            if (current && current.state !== "posting")
              requestReview(key, current);
            return;
          }
        } catch (error) {
          requestReview(key, submission, error);
          return;
        }
        const posting = { ...submission, state: "posting" };
        let admissionPending = true;
        try {
          if (!await promptQueue.stage(projectId, sessionId, posting,
            { first: posting.interrupt })) {
            const rejection = new Error(t("composer.queueFull", {},
              "待发送队列已满（最多 20 条）"));
            rejection.code = "queue_full";
            rejection.status = 422;
            throw rejection;
          }
          admissionPending = false;
          const item = promptQueue.find(projectId, sessionId, posting.id);
          if (!item) {
            if (await consumeReceipt(key, posting,
                await promptQueue.receipt(projectId, sessionId, posting.id)))
              continue;
            requestReview(key, posting);
            return;
          }
          if (!matches(item, posting) || !await release(key, posting)) {
            requestReview(key, posting);
            return;
          }
          await promptQueue.promote(projectId, sessionId, posting.id);
          onPromoted(key, posting);
        } catch (error) {
          if (error?.code === "queue_item_consumed") {
            try {
              if (await consumeReceipt(key, posting,
                  await promptQueue.receipt(projectId, sessionId,
                    posting.id))) continue;
            } catch { /* Keep the original admission error for review. */ }
          }
          // A queue-full 422 is a definite rejection. Persist that fact so a
          // refresh does not mislabel it as an admission with lost response.
          // Other failures remain uncertain until the queue can be checked.
          if (admissionPending && error?.status === 422 &&
              error?.code === "queue_full") {
            let rejected = false;
            try { rejected = await draftStore.changeSessionSubmissionState(key,
              posting.id, "rejected"); }
            catch { /* Keep the original admission error for review. */ }
            if (rejected) {
              requestReview(key, { ...posting, state: "rejected" }, error);
              return;
            }
          }
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
    if (!await draftStore.refreshSessionSubmissions(key)) return false;
    await verifyUnacknowledged(key);
    onChange(key);
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
    try {
      if (await consumeReceipt(key, first,
          await promptQueue.receipt(projectId, sessionId, first.id))) {
        void pump(key);
        return true;
      }
    } catch (error) {
      requestReview(key, first, error);
      return false;
    }
    if (first.state === "posting" || first.state === "rejected") {
      requestReview(key, first);
      return false;
    }
    void pump(key);
    return true;
  }

  async function submit(key, text, attachments, interrupt) {
    if (!key || releasing.has(key) || submitting.has(key) ||
        unacknowledged.has(key))
      throw new Error(t("composer.submissionBusy"));
    submitting.add(key);
    try {
      const submission = { id: promptQueue.newId(), text,
        attachments: [...attachments], interrupt, state: "prepared" };
      draftStore.capture(key, text, attachments);
      if (!draftStore.appendSubmission(key, submission))
        throw new Error(t("composer.queueFull", {},
          "待发送队列已满（最多 20 条）"));
      unacknowledged.set(key, submission);
      onChange(key);
      if (!await draftStore.flush(key)) {
        let confirmed = false;
        try { confirmed = await draftStore.persistUnconfirmedSubmission(key,
          submission); }
        catch { /* Query the queue and receipt before reporting uncertainty. */ }
        if (!confirmed) {
          // A peer may already have moved the ID out of the draft. Its queue
          // item or accepted run receipt is durable proof of this submission.
          try {
            const { projectId, sessionId } = owner(key);
            await promptQueue.select(projectId, sessionId);
            confirmed = matches(promptQueue.find(projectId, sessionId,
              submission.id), submission) || Boolean(await promptQueue.receipt(
                projectId, sessionId, submission.id));
          } catch { /* Keep the source text when durable proof is unavailable. */ }
        }
        if (confirmed) {
          acknowledge(key, submission);
          void pump(key);
          return true;
        }
        requestReview(key, submission);
        return false;
      }
      acknowledge(key, submission);
      void pump(key);
      return true;
    } finally {
      submitting.delete(key);
    }
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
    try {
      if (await consumeReceipt(key, first,
          await promptQueue.receipt(projectId, sessionId, first.id))) {
        void pump(key);
        return true;
      }
    } catch (error) {
      requestReview(key, first, error);
      return false;
    }
    if (first.state !== "posting" && first.state !== "rejected") {
      void pump(key);
      return true;
    }
    if (draftStore.submissions(key).length > 1) {
      // The user has checked the trace and explicitly retries the same ID.
      if (!await draftStore.changeSessionSubmissionState(key,
          first.id, "prepared")) return false;
      void pump(key);
      return true;
    }
    if (!await release(key, first)) return false;
    if (unacknowledged.get(key)?.id === first.id)
      unacknowledged.delete(key);
    const restored = draftStore.restoreUnsent(key, first.text,
      first.attachments);
    onRestored(key, restored);
    return true;
  }

  return Object.freeze({
    submit, pump, reconcile, review,
    isBusy(key) { return pumping.has(key); },
    isReleasing(key) { return releasing.has(key); },
  });
}
