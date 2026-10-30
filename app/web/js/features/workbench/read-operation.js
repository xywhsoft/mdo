import { t as uiText } from "../../i18n.js";
/* Long read-only inspections run outside the HTTP worker. A view owns only its
 * operation, including cancellation and release; it never cancels another tab. */
export async function readOperation(sdk, method, params, signal) {
  if (signal?.aborted) throw new DOMException("Inspection cancelled", "AbortError");
  const id = `sdk-${crypto.randomUUID().replaceAll("-", "")}`;
  let finished = false;
  try {
    let receipt = await sdk.start(method, params, id, { signal });
    for (;;) {
      if (signal?.aborted) throw new DOMException("Inspection cancelled", "AbortError");
      receipt = await sdk.wait(id, 500, { signal });
      if (!receipt.terminal) continue;
      if (receipt.state !== "succeeded") throw Object.assign(new Error(receipt.error?.message || uiText("ui.readOperation.readOperationIncomplete", {}, "读取操作未完成")), receipt.error || {});
      try { const result = await sdk.result(id, { signal }); finished = true; return result; }
      catch (error) { if (error.code !== "sdk_operation_pending") throw error; }
    }
  } finally {
    // Do not reuse the disposed view's aborted signal for cleanup. The explicit
    // id also covers a lost admission acknowledgement without replaying work.
    void (async () => {
      try {
        if (!finished) await sdk.cancel(id);
        for (let at = 0; at < 40; at++) {
          try { await sdk.call("operation.release", { id }); return; }
          catch (error) {
            if (error.code !== "sdk_operation_busy") return;
            await sdk.wait(id, 500);
          }
        }
      } catch { /* Connection loss leaves the server's bounded admission receipt. */ }
    })();
  }
}
