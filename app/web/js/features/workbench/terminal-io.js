import { t as uiErrorText, t as uiText } from "../../i18n.js";

/** Ordered, bounded keyboard admission. A failed ACK is uncertain, never an
 * invitation to replay commands using another lease or a new service epoch. */
export function createTerminalInput({ lease, write, fault, changed = () => {}, capacity = 65536, chunkSize = 8192, retryMs = 100, retryLimit = 40 }) {
  let queue = [], pending = 0, running = false, stopped = false, sequence = lease.next_sequence;
  const controller = new AbortController();
  const stop = () => { stopped = true; controller.abort(); queue = []; pending = 0; changed(0); };
  const pump = async () => {
    if (running || stopped) return; running = true;
    try {
      while (queue.length && !stopped) {
        const bytes = queue[0], params = { id: lease.id, epoch: lease.epoch, client: lease.client, control_token: lease.control_token, input_epoch: lease.input_epoch, sequence, base64: btoa(String.fromCharCode(...bytes)) };
        let receipt;
        for (let retries = 0; !stopped; retries++) {
          try { receipt = await write(params, { signal: controller.signal }); break; }
          catch (error) {
            // 429 guarantees this sequence was NOT admitted. No transport or
            // other error is retried, even when it appears transient.
            if (error.code !== "terminal_input_backpressure" || retries >= retryLimit) throw error;
            await new Promise(resolve => setTimeout(resolve, retryMs));
          }
        }
        if (stopped) break;
        if (receipt.sequence !== sequence || receipt.accepted !== bytes.length || receipt.epoch !== lease.epoch) throw new Error(uiText("ui.terminalIo.terminalInputReceiptMismatch", {}, "终端输入收据不一致"));
        queue.shift(); pending -= bytes.length; sequence++; changed(pending);
      }
    } catch (error) { if (!stopped) { stop(); fault(error); } }
    finally { running = false; }
  };
  return {
    enqueue(bytes) {
      if (stopped) return false;
      if (!(bytes instanceof Uint8Array)) throw new TypeError(uiErrorText("error.detail.terminalInputMustBeBytes", {}, "Terminal input must be bytes"));
      if (bytes.length + pending > capacity) throw new Error(uiText("ui.terminalView.pastedContentExceedsThe64KibInputQueueAnd", {}, "粘贴内容超出 64 KiB 输入队列，尚未发送；请分段粘贴。"));
      // Encode a complete onData event before splitting: surrogate pairs and
      // UTF-8 multibyte sequences survive arbitrary transport chunk boundaries.
      for (let at = 0; at < bytes.length; at += chunkSize) queue.push(bytes.slice(at, at + chunkSize));
      pending += bytes.length; changed(pending); void pump(); return true;
    },
    stop, get pending() { return pending; }, get stopped() { return stopped; },
  };
}

export function terminalBytes(result, cursor) {
  const data = Uint8Array.from(atob(result.base64 || ""), value => value.charCodeAt(0));
  if (!Number.isSafeInteger(result.offset) || !Number.isSafeInteger(result.next) || result.offset < cursor || result.next !== result.offset + data.length || (!result.gap && result.offset !== cursor))
    throw new Error(uiText("ui.terminalIo.terminalOutputRangeIsDiscontinuousReconnectToViewIt", {}, "终端输出范围不连续，请重新连接查看。"));
  return data;
}
