// Event times cover the complete model call, including recovery. Transport
// diagnostics describe only the final HTTP attempt and must stay separate.
export function createModelTimingTracker() {
  const calls = new Map();
  return (event, key) => {
    const now = Number(event.time) / 1000;
    if (event.kind === "model_start") calls.set(key, { started: now, retryWaitMs: 0, attempts: 1 });
    const call = calls.get(key);
    if (!call || !Number.isFinite(now)) return null;
    if (call.waitAt != null && (event.kind === "model_retry" || event.kind === "model_done" ||
        ["model_text_delta", "model_reasoning_delta"].includes(event.kind) && event.text)) {
      call.retryWaitMs += Math.max(0, Math.min(now - call.waitAt, call.waitFor)); delete call.waitAt;
    }
    if (event.kind === "model_retry") {
      call.attempts = Math.max(call.attempts, Number(event.model_attempts || 0) + 1);
      if (event.model_retry_after_ms > 0) { call.waitAt = now; call.waitFor = Number(event.model_retry_after_ms); }
    }
    if (event.text?.trim()) {
      const field = event.kind === "model_reasoning_delta" ? "firstReasoningMs" : event.kind === "model_text_delta" ? "firstTextMs" : "";
      if (field && call[field] == null) call[field] = Math.max(0, now - call.started);
    }
    if (event.kind !== "model_done") return null;
    const result = { ...call, elapsedMs: Math.max(0, now - call.started), requestId: event.model_request_id || "",
      attempts: Math.max(call.attempts, Number(event.model_attempts || 1)), transport: event.model_timing || null };
    delete result.started; delete result.waitFor; delete result.waitAt; calls.delete(key); return result;
  };
}
