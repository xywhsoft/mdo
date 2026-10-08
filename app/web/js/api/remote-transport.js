import { createSha256 } from "../utils/sha256.js";

const ID = /^[0-9a-f]{32}$/, SHA = /^[0-9a-f]{64}$/;
const WINDOW = 65536, RESPONSE_LIMIT = 96 * 1024 * 1024;
const encoder = new TextEncoder(), decoder = new TextDecoder("utf-8", { fatal: true });
const allowedHeaders = new Set(["accept", "content-type", "if-match", "if-none-match",
  "x-mdo-write-token", "x-mdo-file-name", "range", "accept-encoding"]);
export function remoteError(code, message, details = null) {
  const error = new Error(message); error.name = "RemoteError"; error.code = code; error.details = details;
  return error;
}
function randomId() {
  return [...crypto.getRandomValues(new Uint8Array(16))].map(b => b.toString(16).padStart(2, "0")).join("");
}
function digest(bytes) { const hash = createSha256(); hash.update(bytes); return hash.hex(); }
function integer(value, max = Number.MAX_SAFE_INTEGER) { return Number.isSafeInteger(value) && value >= 0 && value <= max; }
function chunk(kind, id, offset, bytes) {
  const output = new Uint8Array(29 + bytes.length), view = new DataView(output.buffer);
  output.set(encoder.encode("MDP1")); output[4] = kind;
  for (let i = 0; i < 16; i++) output[5 + i] = parseInt(id.slice(i * 2, i * 2 + 2), 16);
  view.setUint32(21, Math.floor(offset / 0x100000000)); view.setUint32(25, offset >>> 0);
  output.set(bytes, 29); return output;
}
function unpack(buffer) {
  const bytes = new Uint8Array(buffer), view = new DataView(buffer);
  if (bytes.length <= 29 || bytes.length > 29 + 65536 || decoder.decode(bytes.subarray(0, 4)) !== "MDP1" ||
      ![2, 3].includes(bytes[4])) throw remoteError("remote_protocol", "Invalid remote body frame");
  const id = [...bytes.subarray(5, 21)].map(b => b.toString(16).padStart(2, "0")).join("");
  const offset = view.getUint32(21) * 0x100000000 + view.getUint32(25);
  if (!integer(offset)) throw remoteError("remote_protocol", "Invalid remote body offset");
  return { kind: bytes[4], id, offset, bytes: bytes.subarray(29) };
}

// A transport owns one authenticated controller connection. HTTP bodies are
// consumed through ReadableStream; ACK follows consumption, not WS receipt.
// Reconnect never retries a write. Only metadata is retained for receipt checks.
export function createRemoteTransport({ socket = (...args) => new WebSocket(...args),
  id = randomId, setTimer = setTimeout, clearTimer = clearTimeout, onState = () => {} } = {}) {
  const client = id(), requests = new Map(), waiting = [], lives = new Map(), uncertain = new Map();
  let connection = null, hello = null, sequence = 0, admitted = 0, generation = 0;
  let connectReject = null, connectTimer = 0;
  const send = value => {
    if (connection?.readyState !== 1) throw remoteError("remote_offline", "目标设备连接已断开");
    connection.send(typeof value === "string" || value instanceof Uint8Array ? value : JSON.stringify(value));
  };
  function release(entry) {
    if (!requests.delete(entry.id)) return;
    const queued = waiting.indexOf(entry); if (queued >= 0) waiting.splice(queued, 1);
    clearTimer(entry.timer); entry.signal?.removeEventListener("abort", entry.abort);
    entry.upload?.fill(0); entry.upload = null; entry.queue.length = 0;
    if (entry.admitted) admitted--;
    drain();
  }
  function reject(entry, error) {
    if (!requests.has(entry.id)) return;
    entry.controller?.error(error); entry.reject(error); release(entry);
  }
  function receipt(entry) {
    return { id: entry.id, runtime_id: entry.runtime, client_id: client, sequence: entry.sequence };
  }
  function remember(entry, meta = receipt(entry)) {
    uncertain.set(entry.id, meta);
    while (uncertain.size > 64) uncertain.delete(uncertain.keys().next().value);
    return meta;
  }
  function cancel(entry) {
    if (!entry.admitted || !hello) return;
    try { send({ type: "cancel", id: entry.id }); }
    catch (error) { disconnect(error); }
  }
  function disconnect(error = remoteError("remote_offline", "目标设备离线，操作不会转到本机")) {
    const old = connection; connection = null; hello = null; generation++;
    clearTimer(connectTimer); connectReject?.(error); connectReject = null;
    for (const entry of [...requests.values()]) {
      let failure = error;
      if (entry.write && entry.admitted) {
        const meta = remember(entry);
        failure = remoteError("remote_result_unconfirmed", "远程写入的结果尚未确认，请核对目标状态，勿重复提交", meta);
      }
      reject(entry, failure);
    }
    waiting.length = 0;
    for (const live of [...lives.values()]) live.fail(error);
    old?.close(); onState({ connected: false, error });
  }
  function pull(entry) {
    if (!entry.controller) return;
    if (entry.demand && entry.queue.length) {
      const bytes = entry.queue.shift(); entry.demand = false; entry.consumed += bytes.length;
      entry.controller.enqueue(bytes);
      send({ type: "download_ack", id: entry.id, offset: entry.consumed });
    }
    if (entry.ended && !entry.queue.length) { entry.controller.close(); release(entry); }
  }
  function admit(entry) {
    if (!requests.has(entry.id)) return;
    if (entry.signal?.aborted) { entry.abort(); return; }
    if (entry.write && sequence === Number.MAX_SAFE_INTEGER) {
      reject(entry, remoteError("remote_capacity", "Remote write sequence exhausted")); return;
    }
    entry.admitted = true; entry.runtime = hello.runtime_id; admitted++;
    entry.sequence = entry.write ? ++sequence : 0;
    send({ type: "request", id: entry.id, runtime_id: entry.runtime, client_id: client,
      sequence: entry.sequence, method: entry.method, path: entry.path, headers: entry.headers,
      bytes: entry.upload.length, sha256: digest(entry.upload),
      ...(hello.window_max ? {window_bytes: entry.window} : {}) });
  }
  function drain() {
    if (!hello || !connection) return;
    try { while (admitted < 3 && waiting.length) admit(waiting.shift()); }
    catch (error) { disconnect(error); }
  }
  function upload(entry, offset) {
    if (!entry.upload || offset !== entry.sent) throw remoteError("remote_protocol", "Invalid upload acknowledgement");
    if (offset === entry.upload.length) { entry.upload.fill(0); entry.upload = null; return; }
    const end = Math.min(offset + hello.chunk_limit, entry.upload.length);
    send(chunk(1, entry.id, offset, entry.upload.subarray(offset, end))); entry.sent = end;
  }
  function receive(value) {
    const entry = requests.get(value.id);
    if (!entry) return; // A cancelled request can already have queued frames.
    if (value.type === "request_ready" || value.type === "upload_ack") upload(entry, value.offset);
    else if (value.type === "response") {
      if (entry.controller || !integer(value.status, 599) || value.status < 200 ||
          !Array.isArray(value.headers) || value.headers.length > 64) throw remoteError("remote_protocol", "Invalid response head");
      const headers = new Headers(value.headers), length = headers.get("Content-Length");
      if (length !== null && (!/^(0|[1-9][0-9]*)$/.test(length) || !integer(Number(length), RESPONSE_LIMIT)))
        throw remoteError("remote_protocol", "Invalid response length");
      entry.length = entry.method === "HEAD" ? 0 : length === null ? null : Number(length);
      const empty = entry.method === "HEAD" || [204, 205, 304].includes(value.status);
      if (empty) entry.length = 0;
      const body = new ReadableStream({ start(controller) { entry.controller = controller; },
        pull() { entry.demand = true; pull(entry); }, cancel() { entry.abort(); } }, { highWaterMark: 0 });
      const coding = headers.get("Content-Encoding");
      let decoded = body;
      if (!empty && coding) {
        if (coding !== "gzip" || typeof DecompressionStream !== "function")
          throw remoteError("remote_protocol", "Unsupported response compression");
        let bytes = 0;
        decoded = body.pipeThrough(new DecompressionStream("gzip")).pipeThrough(new TransformStream({
          transform(chunk, controller) {
            bytes += chunk.length;
            if (bytes > RESPONSE_LIMIT) throw remoteError("remote_protocol", "Decoded response exceeds limit");
            controller.enqueue(chunk);
          },
        }));
        headers.delete("Content-Encoding"); headers.delete("Content-Length");
      }
      entry.resolve(new Response(empty ? null : decoded, { status: value.status, headers }));
    } else if (value.type === "end") {
      if (!entry.controller || value.bytes !== entry.received || (entry.length !== null && value.bytes !== entry.length))
        throw remoteError("remote_protocol", "Incomplete remote response");
      entry.ended = true; pull(entry);
    } else if (value.type === "receipt" || value.type === "error") {
      const meta = { ...receipt(entry), ...value };
      if (entry.query) { entry.resolve(value); release(entry); }
      else {
        const unknown = entry.write && value.outcome !== "not_started";
        if (unknown) remember(entry, meta);
        reject(entry, remoteError(unknown ? "remote_result_unconfirmed" : value.code || "remote_request_failed",
          unknown ? "远程写入的结果尚未确认，请核对后再操作" : "目标设备拒绝了此操作", meta));
      }
    } else throw remoteError("remote_protocol", "Unexpected remote response");
  }
  function receiveBody(frame) {
    if (frame.kind === 3) { lives.get(frame.id)?.body(frame); return; }
    const entry = requests.get(frame.id); if (!entry) return;
    if (!entry.controller || entry.ended || frame.offset !== entry.received ||
        frame.bytes.length > RESPONSE_LIMIT - entry.received ||
        entry.received + frame.bytes.length - entry.consumed > entry.window ||
        (entry.length !== null && frame.bytes.length > entry.length - entry.received))
      throw remoteError("remote_protocol", "Invalid remote response body");
    entry.received += frame.bytes.length; entry.queue.push(frame.bytes); pull(entry);
  }
  async function fetchTarget(path, options = {}) {
    const epoch = generation, runtime = hello?.runtime_id;
    if (!hello) throw remoteError("remote_offline", "目标设备离线，操作不会转到本机");
    if (typeof path !== "string" || !path.startsWith("/api/v1/") || path.startsWith("/api/v1/connector/"))
      throw remoteError("remote_scope", "This endpoint is outside the selected target");
    const method = (options.method || "GET").toUpperCase(), write = !["GET", "HEAD"].includes(method);
    if (!["GET", "HEAD", "POST", "PUT", "PATCH", "DELETE"].includes(method)) throw new TypeError("Unsupported remote method");
    if (write && hello.mode !== "control") throw remoteError("read_only", "当前设备连接为只读模式");
    if (requests.size >= 64 || sequence === Number.MAX_SAFE_INTEGER) throw remoteError("remote_capacity", "Wait for remote operations to finish");
    const headerValues = new Headers(options.headers);
    if (!write && typeof DecompressionStream === "function") headerValues.set("Accept-Encoding", "gzip");
    const headers = [...headerValues].map(pair => pair);
    if (headers.some(([name]) => !allowedHeaders.has(name))) throw new TypeError("Unsupported remote header");
    const body = options.body;
    let bytes;
    if (body == null) bytes = new Uint8Array();
    else if (typeof body === "string") bytes = encoder.encode(body);
    else if (body instanceof Uint8Array) bytes = body.slice();
    else if (body instanceof ArrayBuffer) bytes = new Uint8Array(body.slice(0));
    else if (body instanceof Blob && body.size <= hello.upload_limit) bytes = new Uint8Array(await body.arrayBuffer());
    else throw new TypeError("Unsupported remote body");
    if (epoch !== generation || runtime !== hello?.runtime_id) { bytes.fill(0); throw remoteError("remote_offline", "Target connection changed"); }
    if (bytes.length > hello.upload_limit || (!write && bytes.length)) { bytes.fill(0); throw new TypeError("Remote body exceeds its limit"); }
    if (requests.size >= 64) { bytes.fill(0); throw remoteError("remote_capacity", "Wait for remote operations to finish"); }
    return new Promise((resolve, rejectPromise) => {
      const entry = { id: id(), method, path, headers, write, upload: bytes, queue: [],
        window: hello.window_max || WINDOW,
        priority: write ? 3 : path.includes("before=") ? 0 : /\/(conversation|bootstrap|project-purge-intent)(\?|$)/.test(path) ? 2 : 1,
        sent: 0, received: 0, consumed: 0, resolve, reject: rejectPromise, signal: options.signal };
      entry.abort = () => {
        if (!requests.has(entry.id)) return;
        const error = new DOMException("Target request cancelled", "AbortError");
        if (entry.write && entry.admitted) error.details = remember(entry);
        cancel(entry);
        reject(entry, error);
      };
      requests.set(entry.id, entry);
      entry.signal?.addEventListener("abort", entry.abort, { once: true });
      entry.timer = setTimer(() => {
        if (entry.write && entry.admitted) remember(entry);
        const error = remoteError(entry.write && entry.admitted ? "remote_result_unconfirmed" : "remote_timeout", "Remote request deadline exceeded", receipt(entry));
        cancel(entry); reject(entry, error);
      }, 75000);
      if (entry.signal?.aborted) entry.abort(); else {
        waiting.push(entry); waiting.sort((a,b) => b.priority - a.priority); drain();
      }
    });
  }
  function liveSocket(token) {
    const liveId = id(); let offset = 0, eventSequence = 0, event = null;
    const current = { readyState: 0, onopen: null, onmessage: null, onerror: null, onclose: null,
      send(data) {
        if (current.readyState !== 1 || typeof data !== "string" || encoder.encode(data).length > 4096)
          throw remoteError("remote_live", "Live channel is unavailable");
        send({ type: "live_send", id: liveId, data });
      },
      close() {
        if (current.readyState >= 2) return;
        current.readyState = 3; lives.delete(liveId); event = null;
        if (hello) { try { send({ type: "live_close", id: liveId }); } catch (error) { disconnect(error); } }
      },
      fail(error) { current.close(); current.onclose?.({ code: error.code === "remote_revoked" ? 1008 : 1006 }); },
      text(value) {
        if (value.type === "live_opened") { current.readyState = 1; current.onopen?.({}); }
        else if (value.type === "live_closed" || value.type === "error") current.fail(remoteError(value.code || "remote_live", "Live channel closed"));
        else if (value.type === "live_event") {
          if (event || value.offset !== offset || value.sequence !== eventSequence + 1 ||
              !integer(value.bytes, hello.live_limit) || !SHA.test(value.sha256)) throw remoteError("remote_protocol", "Invalid live event");
          event = { ...value, data: new Uint8Array(value.bytes), received: 0, hash: createSha256() };
        } else if (value.type === "live_event_end") {
          if (!event || value.sequence !== event.sequence || value.offset !== offset ||
              event.received !== event.bytes || event.hash.hex() !== event.sha256) throw remoteError("remote_protocol", "Incomplete live event");
          const data = decoder.decode(event.data); eventSequence = event.sequence; event = null;
          current.onmessage?.({ data });
        } else throw remoteError("remote_protocol", "Unexpected live event");
      },
      body(frame) {
        if (!event || frame.offset !== offset || frame.bytes.length > event.bytes - event.received)
          throw remoteError("remote_protocol", "Invalid live event body");
        event.data.set(frame.bytes, event.received); event.hash.update(frame.bytes);
        event.received += frame.bytes.length; offset += frame.bytes.length;
        send({ type: "live_ack", id: liveId, offset });
      },
    };
    if (!hello?.live || lives.size) throw remoteError("remote_live", "Live subscription is unavailable");
    lives.set(liveId, current);
    send({ type: "live_open", id: liveId, runtime_id: hello.runtime_id, token });
    return current;
  }
  return {
    fetch: fetchTarget, liveSocket, close: disconnect,
    state: () => hello, uncertain: () => [...uncertain.values()],
    queryReceipt(meta) {
      if (!hello || requests.has(meta.id) || !ID.test(meta.id) || !ID.test(meta.runtime_id) || meta.client_id !== client || !integer(meta.sequence) || !meta.sequence)
        return Promise.reject(remoteError("remote_receipt", "Receipt is unavailable"));
      return new Promise((resolve, rejectPromise) => {
        const entry = { ...meta, query: true, queue: [], resolve, reject: rejectPromise };
        requests.set(meta.id, entry); entry.timer = setTimer(() => reject(entry, remoteError("remote_timeout", "Receipt deadline exceeded")), 5000);
        try { send({ ...meta, type: "receipt" }); } catch (error) { disconnect(error); }
      });
    },
    connect({ origin, ticket, deviceId, mode = "control" }) {
      disconnect();
      return new Promise((resolve, rejectPromise) => {
        connectReject = rejectPromise; let relayReady = false;
        try {
          const url = new URL(origin);
          if (!["http:", "https:"].includes(url.protocol) || url.username || url.password || url.pathname !== "/" || url.search || url.hash ||
              !ID.test(deviceId) || !["control", "view"].includes(mode) || ticket.path !== "/api/v1/devices/connect" ||
              ticket.protocol !== "xadmin.device-relay.v1" || !SHA.test(ticket.ticket) || ticket.payload_limit !== 262123)
            throw remoteError("remote_protocol", "Invalid device ticket");
          url.pathname = ticket.path; url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
          const active = socket(url.href, [ticket.protocol, `xadmin.ticket.${ticket.ticket}`]);
          connection = active; active.binaryType = "arraybuffer";
          connectTimer = setTimer(() => disconnect(remoteError("remote_timeout", "Target handshake deadline exceeded")), 10000);
          active.onmessage = ({ data }) => {
            if (active !== connection) return;
            try {
              if (data instanceof ArrayBuffer) { if (!hello) throw remoteError("remote_protocol", "Body before handshake"); receiveBody(unpack(data)); return; }
              if (typeof data !== "string" || encoder.encode(data).length > 262123) throw remoteError("remote_protocol", "Invalid remote message");
              const value = JSON.parse(data);
              if (!hello) {
                if (!relayReady && value.type === "ready" && value.version === 1 && value.device_id === deviceId &&
                    value.mode === mode && ID.test(value.peer_id) && value.payload_limit === 262123) { relayReady = true; return; }
                if (!relayReady || value.type !== "hello" || value.version !== 1 || !ID.test(value.runtime_id) || value.mode !== mode ||
                    value.upload_limit !== 8388608 || value.chunk_limit !== WINDOW || value.window_bytes !== WINDOW ||
                    value.live !== true || value.live_limit !== 2097152 ||
                    (value.window_max !== undefined && (!integer(value.window_max,262144) ||
                      value.window_max < WINDOW || value.window_max % 16384))) throw remoteError("remote_protocol", "Invalid target handshake");
                hello = Object.freeze(value); clearTimer(connectTimer); connectReject = null;
                onState({ connected: true, hello }); resolve(hello); return;
              }
              if (lives.has(value.id)) lives.get(value.id).text(value); else receive(value);
            } catch (error) { disconnect(remoteError("remote_protocol", error.message)); }
          };
          active.onerror = () => { if (active === connection) disconnect(); };
          active.onclose = event => { if (active === connection) disconnect(event.code === 1008 ?
            remoteError("remote_revoked", "设备授权已撤销，请返回本机或选择其他设备") : undefined); };
        } catch (error) { disconnect(error); }
      });
    },
  };
}
