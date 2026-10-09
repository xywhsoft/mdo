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
      const call = `${event.run_id}:${event.agent_turn}`;
      if (call !== modelTurn) { current.answer = ""; modelTurn = call; }
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
        user: item, entries: [], state: item.turnState ?? "running" };
      groups.push(turn);
    } else if (!turn) groups.push({ key: item.key, standalone: item });
    else {
      if (!(item.agentDepth > 0)) turn.state = item.turnState ?? "running";
      // Normal main-Agent text is public conversation, even when tools or
      // another reply follow it. Only explicit execution records are folded.
      if (item.agentDepth > 0 || ["reasoning", "tool", "task"].includes(item.kind)) {
        let process = turn.entries.at(-1);
        if (process?.kind !== "process") {
          process = { key: `process-${turn.firstEventId}-${item.key}`,
            kind: "process", items: [], state: "running" };
          turn.entries.push(process);
        }
        process.items.push(item);
      } else turn.entries.push(item);
    }
  }
  for (const group of groups) {
    if (group.standalone) continue;
    for (const entry of group.entries) {
      if (entry.kind === "process") entry.state = group.state === "running" &&
        entry.items.some(item => item.state === "running") ? "running" : "done";
    }
  }
  return groups;
}
