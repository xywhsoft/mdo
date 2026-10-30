// Only selected, newly added profiles are probed. Calls are sequential so a
// catalog with hundreds of entries cannot create a burst of billed requests.
export async function verifyModelSetup(models, { request, onProgress = () => {}, signal } = {}) {
  const results = new Map();
  for (const [index, model] of models.entries()) {
    signal?.throwIfAborted();
    const result = { verified: false, needsTools: model.capabilities.includes("tool-call-output") };
    for (const phase of result.needsTools ? ["reply", "tools"] : ["reply"]) {
      signal?.throwIfAborted();
      onProgress({ model, index, total: models.length, phase, state: "running", result });
      try {
        const response = await request(model.id, phase === "tools", signal);
        signal?.throwIfAborted();
        if (response.verified !== true) throw new Error("Model verification did not pass");
        result[phase] = { state: "passed", latency_ms: response.latency_ms };
        if (response.verification) result.evidence = response.verification;
      } catch (cause) {
        if (signal?.aborted || cause.name === "AbortError") throw cause;
        result[phase] = { state: "failed", code: cause.code || "model_test_failed" };
        result.error = cause; break;
      }
    }
    result.verified = result.reply?.state === "passed" && (!result.needsTools || result.tools?.state === "passed");
    results.set(model.id, result);
    onProgress({ model, index, total: models.length, state: "complete", result });
  }
  return results;
}
