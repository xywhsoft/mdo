const memoryFileTools = new Set(["read", "write", "edit", "ls", "glob", "grep"]);
function memoryOnlyTool(item) {
  if (item.kind !== "tool" || item.inputTruncated || !memoryFileTools.has(item.role)) return false;
  let args;
  try { args = JSON.parse(item.inputText); } catch { return false; }
  const paths = typeof args?.path === "string" ? [args.path] : args?.paths;
  return Array.isArray(paths) && paths.length > 0 && paths.every(path => {
    if (typeof path !== "string") return false;
    const normalized = path.replaceAll("\\", "/");
    return /^@memory-(global|project)(\/|$)/.test(normalized) &&
      !normalized.split("/").some(part => part === "." || part === "..");
  });
}

// Do not use prose keywords or response length. A tail consisting exclusively
// of explicitly scoped memory-file calls and their follow-up text can keep
// the public reply immediately before those calls as the copy target. Mixed
// tools, shell commands and unmarked ordinary text remain ordinary replies.
export function primaryTurnReply(rows) {
  const publicReply = item => item.kind === "assistant" && !(item.agentDepth > 0) &&
    !item.executionDetail && item.text?.trim();
  const last = rows.findLastIndex(publicReply);
  if (last < 0) return null;
  let memoryStart = -1, wroteMemory = false;
  for (let index = last - 1; index >= 0; --index) {
    const item = rows[index];
    if (item.agentDepth > 0 || item.executionDetail) continue;
    if (item.runKey !== rows[last].runKey || item.runEpoch !== rows[last].runEpoch) break;
    if (item.kind === "tool") {
      if (!memoryOnlyTool(item)) break;
      memoryStart = index;
      wroteMemory ||= item.role === "write" || item.role === "edit";
    } else if (["user", "error"].includes(item.kind)) break;
  }
  return (!wroteMemory ? null : rows.slice(0, memoryStart).findLast(publicReply)) ?? rows[last];
}

// One action target per user turn, including any resumed attempts. A model
// finishing one call is not proof that its enclosing Agent turn has ended.
export function conversationTurnActions(group, nextGroup) {
  const rows = [...(group.user ? [group.user] : []),
    ...group.entries.flatMap(entry => entry.kind === "process" ? entry.items : [entry])];
  const roots = rows.filter(item => !(item.agentDepth > 0));
  const latestEpoch = Math.max(0, ...roots.map(item => item.runEpoch || 0));
  const current = roots.filter(item => (item.runEpoch || 0) === latestEpoch && item.kind !== "changes");
  if (!current.length || current.some(item => !["done", "failed", "cancelled"].includes(item.turnState))) return null;
  const primaryReply = primaryTurnReply(roots);
  const target = [...roots].reverse().find(item => item.retryPrompt && !item.executionDetail &&
    (!group.firstEventId || item.retryPrompt.sourceEventId === group.firstEventId));
  const user = group.user;
  const retryPrompt = target?.retryPrompt ?? (Number.isSafeInteger(user?.userMessageSequence) &&
    user.userMessageSequence > 0 ? { sequence: user.userMessageSequence, sourceEventId: user.sourceEventId,
      text: user.text, attachments: user.attachments, textTruncated: user.textTruncated, copySpans: user.copySpans } : null);
  if (!primaryReply && !retryPrompt) return null;
  const usage = current.findLast(item => item.kind === "assistant" && item.runUsage) ??
    current.findLast(item => item.kind === "assistant" && !item.executionDetail) ?? {};
  return { key: `turn-actions-${group.firstEventId || target?.retryPrompt.sourceEventId || group.key}`,
    kind: "turn-actions", state: current.at(-1).turnState, primaryReply,
    retryPrompt, historyReadOnly: roots.some(item => item.historyReadOnly),
    ...(target && "forkThroughSequence" in target ? { forkThroughSequence: target.forkThroughSequence }
      : retryPrompt && user ? { forkThroughSequence: nextGroup?.user?.userMessageSequence > retryPrompt.sequence
        ? nextGroup.user.userMessageSequence - 1 : null } : {}),
    runUsage: usage.runUsage, inputTokens: usage.inputTokens, outputTokens: usage.outputTokens,
    modelDurationSeconds: usage.modelDurationSeconds, toolStageSeconds: usage.toolStageSeconds,
    tokensPerSecond: usage.tokensPerSecond };
}
