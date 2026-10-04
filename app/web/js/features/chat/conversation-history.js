// The navigation index contains small UTF-8 summaries, never model context.
// Event IDs identify turns across reconnects, edits and reused run IDs.
export function mergeConversationTurns(retained = [], additions = []) {
  const turns = new Map(retained.map(turn => [turn.first_event_id, turn]));
  for (const turn of additions) {
    if (Number.isSafeInteger(turn.first_event_id) && turn.first_event_id > 0)
      turns.set(turn.first_event_id, { ...turns.get(turn.first_event_id), ...turn });
  }
  return [...turns.values()].sort((a, b) => a.first_event_id - b.first_event_id);
}

export function summarizeConversationEvents(events) {
  const turns = [];
  let current = null;
  let modelTurn = null;
  for (const event of events) {
    if (event.kind === "agent_start" && !(event.agent_depth > 0) &&
        (event.user_message_sequence > 0 || !(event.schema_version >= 3))) {
      current = { first_event_id: event.event_id, end_event_id: event.event_id,
        question: (event.text ?? "").slice(0, 200), answer: "", time: event.time, state: "running" };
      turns.push(current);
      modelTurn = null;
    }
    if (!current) continue;
    current.end_event_id = event.event_id;
    if (event.agent_depth > 0) continue;
    if (event.kind === "model_text_delta") {
      if (event.agent_turn !== modelTurn) { current.answer = ""; modelTurn = event.agent_turn; }
      current.answer = (current.answer + (event.text ?? "")).slice(0, 200);
    } else if (event.kind === "agent_done" || event.kind === "error") {
      current.state = event.kind === "error" ? "failed" : event.success ? "done" : "cancelled";
      if (!current.answer) current.answer = (event.text ?? "").slice(0, 200);
    }
  }
  return turns;
}

export function conversationGroups(items) {
  const groups = [];
  let turn = null;
  for (const item of items) {
    if (item.kind === "user") {
      turn = { key: `turn-${item.sourceEventId}`, firstEventId: item.sourceEventId,
        user: item, process: [], answer: null, state: "running" };
      groups.push(turn);
    } else if (!turn) groups.push({ key: item.key, standalone: item });
    else turn.process.push(item);
  }
  for (const group of groups) {
    if (group.standalone) continue;
    // The final main-Agent answer alone stays outside the process disclosure.
    // Intermediate model answers and all sub-Agent work remain expandable.
    const answers = group.process.filter(item => item.kind === "assistant" && !(item.agentDepth > 0));
    group.answer = answers.at(-1) ?? group.process.filter(item => item.kind === "error" &&
      !(item.agentDepth > 0)).at(-1) ?? null;
    if (group.answer) {
      group.process = group.process.filter(item => item !== group.answer);
      group.state = group.answer.turnState ?? "running";
    } else group.state = group.user.turnState ?? "running";
  }
  return groups;
}
