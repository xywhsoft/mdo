import assert from "node:assert/strict";
import { createRemoteTransport } from "../app/web/js/api/remote-transport.js";
import { createSha256 } from "../app/web/js/utils/sha256.js";

const device = "d".repeat(32), runtime = "e".repeat(32), encoder = new TextEncoder();
const token = "a".repeat(32) + "-0";
const ticket = { path: "/api/v1/devices/connect", protocol: "xadmin.device-relay.v1",
  ticket: "a".repeat(64), payload_limit: 262123 };
const hello = { type: "hello", version: 1, runtime_id: runtime, mode: "control",
  upload_limit: 8388608, chunk_limit: 65536, window_bytes: 65536, live: true, live_limit: 2097152 };
function digest(bytes) { const hash = createSha256(); hash.update(bytes); return hash.hex(); }
function frame(id, offset, bytes, kind = 2) {
  const result = new Uint8Array(29 + bytes.length), view = new DataView(result.buffer);
  result.set(encoder.encode("MDP1")); result[4] = kind;
  for (let i = 0; i < 16; i++) result[5+i] = parseInt(id.slice(i*2,i*2+2),16);
  view.setUint32(21,Math.floor(offset/0x100000000)); view.setUint32(25,offset>>>0); result.set(bytes,29);
  return result.buffer;
}
class Socket {
  readyState = 1; sent = [];
  send(value) { this.sent.push(typeof value === "string" ? JSON.parse(value) : value.slice()); }
  close() { this.readyState = 3; }
  receive(value) { this.onmessage({ data: value instanceof ArrayBuffer ? value : JSON.stringify(value) }); }
}
let serial = 0, sockets = [], states = [];
const transport = createRemoteTransport({ id: () => (++serial).toString(16).padStart(32,"0"),
  socket(url, protocols) {
    assert.equal(url,"wss://ai.xywhsoft.com/api/v1/devices/connect");
    assert.deepEqual(protocols,[ticket.protocol,"xadmin.ticket."+ticket.ticket]);
    const socket = new Socket(); sockets.push(socket); return socket;
  }, onState: value => states.push(value) });
async function connect(mode = "control") {
  const promise = transport.connect({ origin: "https://ai.xywhsoft.com", ticket, deviceId: device, mode });
  const socket = sockets.at(-1);
  socket.receive({ type: "ready", version: 1, peer_id: "f".repeat(32), device_id: device, mode, payload_limit: 262123 });
  socket.receive({ ...hello, mode }); await promise; return socket;
}
function request(socket) { return socket.sent.filter(v => v.type === "request").at(-1); }
function response(socket, req, text, status = 200) {
  const bytes = encoder.encode(text);
  socket.receive({ type: "response", id: req.id, status, headers: [["Content-Type","application/json"],["Content-Length",String(bytes.length)]] });
  if (req.method !== "HEAD" && bytes.length) socket.receive(frame(req.id,0,bytes));
  socket.receive({ type: "end", id: req.id, bytes: req.method === "HEAD" ? 0 : bytes.length });
}

let socket = await connect();
const one = transport.fetch("/api/v1/settings"), first = request(socket);
const two = transport.fetch("/api/v1/projects"), second = request(socket);
response(socket,second,'{"target":2}'); response(socket,first,'{"target":1}');
assert.equal(socket.sent.filter(v => v.type === "download_ack").length,0,"ACK must wait for body consumption");
assert.deepEqual(await (await one).json(),{ target:1 }); assert.deepEqual(await (await two).json(),{ target:2 });
assert.equal(socket.sent.filter(v => v.type === "download_ack").length,2);

const body = new Uint8Array(78000).map((_,i) => i % 255);
const write = transport.fetch("/api/v1/projects",{ method:"POST",body, headers:{"X-Mdo-Write-Token":token} });
const mutation = request(socket); assert.equal(mutation.sequence,1); assert.equal(mutation.sha256,digest(body));
socket.receive({ type:"request_ready",id:mutation.id,offset:0 });
let binary = socket.sent.at(-1); assert.equal(binary.length,65536+29); assert.equal(binary[4],1);
socket.receive({ type:"upload_ack",id:mutation.id,offset:65536 });
assert.equal(socket.sent.at(-1).length,78000-65536+29);
socket.receive({ type:"upload_ack",id:mutation.id,offset:78000 });
response(socket,mutation,'{"created":true}',201); assert.equal((await (await write).json()).created,true);
assert.equal(body[1],1,"transport must not clear caller-owned input");

const head = transport.fetch("/api/v1/file",{method:"HEAD"}); response(socket,request(socket),"not transferred");
assert.equal(await (await head).text(),"");
await assert.rejects(transport.fetch("https://other.invalid/api/v1/settings"),error => error.code === "remote_scope");
await assert.rejects(transport.fetch("/api/v1/connector/ticket"),error => error.code === "remote_scope");
await assert.rejects(transport.fetch("/api/v1/settings",{headers:{Authorization:"untrusted"}}),TypeError);

const reads = Array.from({length:4},() => transport.fetch("/api/v1/settings"));
const active = socket.sent.filter(v => v.type === "request").slice(-3);
assert.equal(active.length,3);
const before = socket.sent.filter(v => v.type === "request").length;
response(socket,active[0],"{}"); await (await reads[0]).json();
assert.equal(socket.sent.filter(v => v.type === "request").length,before+1,"fourth read waits for bounded admission");
for (const req of [active[1],active[2],request(socket)]) response(socket,req,"{}");
for (const promise of reads.slice(1)) await (await promise).json();

const live = transport.liveSocket(token); let received = [];
live.onmessage = value => received.push(JSON.parse(value.data));
const liveId = socket.sent.at(-1).id;
socket.receive({type:"live_opened",id:liveId}); assert.equal(live.readyState,1);
const large = encoder.encode(JSON.stringify({type:"ready",version:1,text:"图".repeat(100000)}));
socket.receive({type:"live_event",id:liveId,sequence:1,offset:0,bytes:large.length,sha256:digest(large)});
for (let at=0;at<large.length;at+=16384) socket.receive(frame(liveId,at,large.subarray(at,at+16384),3));
assert.equal(received.length,0,"partial event must not reach existing UI handlers");
socket.receive({type:"live_event_end",id:liveId,sequence:1,offset:large.length});
assert.equal(received[0].text,"图".repeat(100000));
live.send(JSON.stringify({type:"pong"})); assert.equal(socket.sent.at(-1).type,"live_send");
const next = encoder.encode('{"type":"changed"}');
socket.receive({type:"live_event",id:liveId,sequence:2,offset:large.length,bytes:next.length,sha256:digest(next)});
socket.receive(frame(liveId,large.length,next,3));
socket.receive({type:"live_event_end",id:liveId,sequence:2,offset:large.length+next.length});
assert.equal(received[1].type,"changed"); live.close();

const lost = transport.fetch("/api/v1/projects",{method:"POST"}); const oldWrite = request(socket);
const lostAssertion = assert.rejects(lost,error => error.code === "remote_result_unconfirmed" && error.details.id === oldWrite.id);
const oldSocket = socket; transport.close(); await lostAssertion;
const meta = transport.uncertain()[0]; assert.equal(meta.id,oldWrite.id);
socket = await connect(); const requestsBefore = socket.sent.filter(v => v.type === "request").length;
oldSocket.receive({ type:"response",id:oldWrite.id,status:201,headers:[] });
assert.equal(socket.sent.filter(v => v.type === "request").length,requestsBefore,"reconnect never replays a mutation");
const check = transport.queryReceipt(meta);
socket.receive({type:"receipt",id:meta.id,result:"duplicate",state:"done",status:201});
assert.equal((await check).state,"done");

const prepare = new Blob(["prepared body"]); let finishBody;
prepare.arrayBuffer = () => new Promise(resolve => { finishBody = resolve; });
const oldPrepare = transport.fetch("/api/v1/projects",{method:"POST",body:prepare});
const prepareAssertion = assert.rejects(oldPrepare,error => error.code === "remote_offline");
transport.close(); socket = await connect(); finishBody(encoder.encode("prepared body").buffer); await prepareAssertion;
assert.equal(socket.sent.filter(v => v.type === "request").length,0,"late upload preparation stays on its old connection");

const abort = new AbortController(); const cancelled = transport.fetch("/api/v1/settings",{signal:abort.signal});
const cancelAssertion = assert.rejects(cancelled,error => error.name === "AbortError");
abort.abort(); await cancelAssertion; assert.equal(socket.sent.at(-1).type,"cancel");
transport.close(); socket = await connect("view");
await assert.rejects(transport.fetch("/api/v1/projects",{method:"POST"}),error => error.code === "read_only");
const view = transport.fetch("/api/v1/settings"); response(socket,request(socket),"{}"); await (await view).json();

const malformed = transport.fetch("/api/v1/settings"); const bad = request(socket);
socket.receive({type:"response",id:bad.id,status:200,headers:[]});
const malformedResponse = await malformed;
const malformedAssertion = assert.rejects(malformedResponse.text(),error => error.code === "remote_protocol");
socket.receive(frame(bad.id,1,encoder.encode("invalid offset"))); await malformedAssertion;
assert.equal(transport.state(),null);
transport.close();
console.log("PASS target HTTP multiplexing, consumer ACK, binary upload, bounds/scope, live SHA/Unicode, lost-write receipt, reconnect/late response, cancellation and view mode");
