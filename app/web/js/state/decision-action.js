import { t as uiErrorText } from "../i18n.js";
import { ApiError, api, currentPageWriteToken } from "../api/client.js";
import { createRequestRecovery } from "../api/request-recovery.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { targetState } from "../api/target.js";

const stamp = () => JSON.stringify([targetState().selected, targetState().runtimeChanged,
  currentPageWriteToken()]);
const transient = error => !["home_restart_required", "write_token_required", "write_token_invalid",
  "write_token_conflict", "decision_conflict", "ask_not_found", "approval_not_found"].includes(error?.code) &&
  (isTransientReadError(error) || ["remote_result_unconfirmed", "invalid_response"].includes(error?.code));
const notApplied = error => ["decisions_unavailable", "asks_unavailable",
  "approvals_unavailable"].includes(error?.code);

// These three PUT endpoints retain exact acknowledgements in the current
// host/write generation. Repeating one decision cannot answer a new question,
// extend a grant or execute the tool twice. No other writes use this helper.
export async function runDecisionAction({ path, id, field, value,
  send = (route, body, options) => api.put(route, body, options),
  epoch = stamp, token = currentPageWriteToken(), recoveryOptions } = {}) {
  const number = String(id ?? "");
  const askPath = /^\/(?:projects\/[A-Za-z0-9_-][A-Za-z0-9._-]*\/sessions\/[A-Za-z0-9_-][A-Za-z0-9._-]*|tasks\/[1-9][0-9]*)\/asks\/([1-9][0-9]*)$/;
  const match = field === "answer" ? askPath.exec(path) : field === "decision"
    ? /^\/approvals\/([1-9][0-9]*)$/.exec(path) : null;
  if (!match || match[1] !== number || typeof value !== "string" || !value ||
      (field === "decision" && !["allow", "allow_run", "deny"].includes(value)))
    throw new TypeError(uiErrorText("error.detail.invalidRecoverableDecision", {}, "Invalid recoverable decision"));
  const recovery = createRequestRecovery(recoveryOptions), original = epoch();
  const body = Object.freeze({ [field]: value });
  const uncertain = new Set();
  function assertOwner() {
    recovery.assertActive();
    if (epoch() !== original) throw new DOMException("Decision target changed", "AbortError");
  }
  try {
    return await recovery.request(async signal => {
      assertOwner();
      const attempt = {};
      // A timeout may settle recovery before send settles. Assume delivery is
      // uncertain until an exact acknowledgement or an explicit refusal arrives.
      uncertain.add(attempt);
      let reply;
      try { reply = await send(path, body, { signal }); }
      catch (error) {
        if (notApplied(error)) uncertain.delete(attempt);
        throw error;
      }
      assertOwner();
      if (token && reply?.writeToken !== token)
        throw new ApiError(uiErrorText("error.detail.decisionServiceRestarted", {}, "Decision service restarted"), { code: "write_token_conflict" });
      if (String(reply?.data?.id) !== String(id) || reply?.data?.[field] !== value)
        throw new ApiError(uiErrorText("error.detail.invalidDecisionAcknowledgement", {}, "Invalid decision acknowledgement"), { code: "invalid_response" });
      return reply.data;
    }, { retryable: transient });
  } catch (error) {
    if (uncertain.size && transient(error)) {
      const final = new ApiError(uiErrorText("error.detail.decisionAcknowledgementCouldNotBeConfirmed", {}, "Decision acknowledgement could not be confirmed"), {
        code: field === "answer" ? "ask_answer_unconfirmed" : "approval_decision_unconfirmed",
      });
      final.cause = error; throw final;
    }
    throw error;
  } finally { recovery.dispose(); }
}
