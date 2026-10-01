function sessionKey(data) {
  return JSON.stringify([data?.project_id ?? "", data?.session_id ?? ""]);
}

function bindingKey(data) {
  return JSON.stringify([data?.project_id ?? "", data?.session_id ?? "", data?.recovery_token ?? ""]);
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
  const isCurrent = (data) => bindingKey(data) === binding;
  const isBusy = (data) => flights.has(sessionKey(data));
  function select(data) {
    const next = bindingKey(data);
    if (next !== binding) choices.clear();
    binding = next;
    current = data;
    const items = new Map((data?.items ?? []).map(item => [String(item.tool_call_id), item]));
    for (const [id, action] of choices) {
      const item = items.get(id);
      if (!item || (action === "retry" && !item.tool_available)) choices.delete(id);
    }
  }
  return Object.freeze({
    select, isCurrent, isBusy,
    choice: (id) => choices.get(String(id)),
    choose(data, id, action) {
      const item = (current?.items ?? []).find(item => String(item.tool_call_id) === String(id));
      if (!isCurrent(data) || isBusy(data) || !item ||
          !["retry", "record_uncertain"].includes(action) ||
          (action === "retry" && !item.tool_available)) return false;
      choices.set(String(id), action);
      return true;
    },
    ready(data) {
      return isCurrent(data) && (data?.items ?? []).every(item => choices.has(String(item.tool_call_id)));
    },
    begin(data) {
      if (!data?.resume_required || !isCurrent(data) || isBusy(data) ||
          !(data.items ?? []).every(item => choices.has(String(item.tool_call_id)))) return null;
      const operation = Object.freeze({ data, binding, session: sessionKey(data), choices: new Map(choices) });
      flights.set(operation.session, operation);
      return operation;
    },
    finish(operation, accepted) {
      if (flights.get(operation?.session) !== operation) return false;
      flights.delete(operation.session);
      if (accepted && operation.binding === binding) choices.clear();
      return true;
    },
  });
}
