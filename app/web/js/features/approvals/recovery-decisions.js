function sessionKey(data) {
  return JSON.stringify([data?.project_id ?? "", data?.session_id ?? ""]);
}

function bindingKey(data) {
  return JSON.stringify([data?.project_id ?? "", data?.session_id ?? "", data?.recovery_token ?? ""]);
}

export function defaultRecoveryAction(item) {
  return item?.tool_available === true && item.automatic_retry_safe === true
    ? "retry" : "record_uncertain";
}

export function recoveryMatchesWorkspace(data, route) {
  return Boolean(data?.project_id && data?.session_id && route?.view === "workspace" &&
    data.project_id === route.projectId && data.session_id === route.sessionId);
}

// A choice authorizes one reviewed recovery snapshot, not every future call
// that happens to reuse its tool_call_id. Flights belong to sessions; moving
// to another session must neither block it nor transfer an older completion.
export function createRecoveryDecisions() {
  let binding = "";
  let current = null;
  const choices = new Map();
  const flights = new Map();
  const submittedBindings = new Map();
  const submittedInspections = new Map();
  const isCurrent = (data) => bindingKey(data) === binding;
  const isBusy = (data) => flights.has(sessionKey(data));
  const isSubmitted = (data) => submittedBindings.get(sessionKey(data)) === bindingKey(data);
  const ready = (data) => isCurrent(data) && !isSubmitted(data) &&
    (data?.items ?? []).every(item => choices.has(String(item.tool_call_id)));
  function select(data) {
    const next = bindingKey(data);
    const key = sessionKey(data);
    const inspection = submittedInspections.get(key);
    const refreshed = submittedBindings.get(key) === next &&
      Number.isSafeInteger(inspection) && Number.isSafeInteger(data?.inspection_id) &&
      data.inspection_id > inspection;
    if (refreshed) {
      submittedBindings.delete(key);
      submittedInspections.delete(key);
    }
    if (next !== binding || refreshed) {
      choices.clear();
      // Continue never blindly repeats an operation with unknown effects.
      // Only the host's explicit read-only descriptor permits retry by default.
      if (!isSubmitted(data)) for (const item of data?.items ?? [])
        choices.set(String(item.tool_call_id), defaultRecoveryAction(item));
    }
    binding = next;
    current = data;
    const items = new Map((data?.items ?? []).map(item => [String(item.tool_call_id), item]));
    for (const [id, action] of choices) {
      const item = items.get(id);
      if (!item) choices.delete(id);
      else if (action === "retry" && !item.tool_available) choices.set(id, "record_uncertain");
    }
  }
  return Object.freeze({
    select, isCurrent, isBusy, isSubmitted,
    choice: (id) => choices.get(String(id)),
    choose(data, id, action) {
      const item = (current?.items ?? []).find(item => String(item.tool_call_id) === String(id));
      if (!isCurrent(data) || isBusy(data) || isSubmitted(data) || !item ||
          !["retry", "record_uncertain"].includes(action) ||
          (action === "retry" && !item.tool_available)) return false;
      choices.set(String(id), action);
      return true;
    },
    ready,
    begin(data) {
      if (!data?.resume_required || !ready(data) || isBusy(data)) return null;
      const operation = Object.freeze({ data, binding, session: sessionKey(data), choices: new Map(choices) });
      flights.set(operation.session, operation);
      return operation;
    },
    finish(operation, accepted) {
      if (flights.get(operation?.session) !== operation) return false;
      flights.delete(operation.session);
      if (accepted) {
        // Keep a confirmed request locked even if refreshing its status fails
        // or the user leaves this session and returns to an older snapshot.
        submittedBindings.set(operation.session, operation.binding);
        submittedInspections.set(operation.session, operation.data.inspection_id);
        if (operation.binding === binding) choices.clear();
      }
      return true;
    },
  });
}
