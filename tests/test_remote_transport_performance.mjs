import assert from "node:assert/strict";
import test from "node:test";
import { gzipSync } from "node:zlib";
import {createRemoteTransport} from "../app/web/js/api/remote-transport.js";
const device="d".repeat(32), runtime="e".repeat(32);
function fixture(window=262144) {
  let serial=0; const sent=[];
  const sock={readyState:1,send(value){sent.push(typeof value==="string"?JSON.parse(value):value);},close(){this.readyState=3;},
    receive(value){this.onmessage({data:typeof value==="object" && !(value instanceof ArrayBuffer)?JSON.stringify(value):value});}};
  const transport=createRemoteTransport({id:()=> (++serial).toString(16).padStart(32,"0"),socket:()=>sock});
  const connected=transport.connect({origin:"https://ai.xywhsoft.com",deviceId:device,
    ticket:{path:"/api/v1/devices/connect",protocol:"xadmin.device-relay.v1",ticket:"a".repeat(64),payload_limit:262123}});
  sock.receive({type:"ready",version:1,device_id:device,mode:"control",peer_id:"f".repeat(32),payload_limit:262123});
  sock.receive({type:"hello",version:1,runtime_id:runtime,mode:"control",upload_limit:8388608,chunk_limit:65536,
    window_bytes:65536,window_max:window,live:true,live_limit:2097152});
  const requests=()=>sent.filter(e=>e.type==="request");
  function frame(id,offset,bytes) {
    const out=new Uint8Array(29+bytes.length); const view=new DataView(out.buffer);
    out.set(new TextEncoder().encode("MDP1"));out[4]=2;
    for(let i=0;i<16;i++)out[5+i]=parseInt(id.slice(i*2,i*2+2),16);
    view.setUint32(25,offset);out.set(bytes,29);sock.receive(out.buffer);
  }
  function response(req,bytes,headers=[]) {
    sock.receive({type:"response",id:req.id,status:200,headers:[["Content-Length",String(bytes.length)],...headers]});
    for(let at=0;at<bytes.length;at+=65536)frame(req.id,at,bytes.subarray(at,at+65536));
    sock.receive({type:"end",id:req.id,bytes:bytes.length});
  }
  return {transport,sock,sent,requests,connected,response};
}
test("a negotiated window admits 128 KiB before consumption, retaining consumer ACKs",async()=>{
  const f=fixture();await f.connected;
  try {
    const pending=f.transport.fetch("/api/v1/file");const req=f.requests()[0];
    assert.equal(req.window_bytes,262144);
    const data=new Uint8Array(128*1024).fill(7);f.response(req,data);
    assert.equal(f.sent.filter(v=>v.type==="download_ack").length,0);
    assert.deepEqual(new Uint8Array(await (await pending).arrayBuffer()),data);
    assert.equal(f.sent.filter(v=>v.type==="download_ack").at(-1).offset,data.length);
  } finally {f.transport.close();}
});
test("gzip is decoded once and a truncated gzip cannot become valid JSON",async()=>{
  const f=fixture();await f.connected;
  try {
    const pending=f.transport.fetch("/api/v1/projects/qa/sessions/a/conversation");const req=f.requests()[0];
    assert.ok(req.headers.some(([n,v])=>n==="accept-encoding"&&v==="gzip"));
    f.response(req,gzipSync('{"items":"'+"x".repeat(40000)+'"}'),[["Content-Encoding","gzip"]]);
    const response=await pending;assert.equal(response.headers.get("Content-Encoding"),null);
    assert.equal((await response.json()).items.length,40000);
    const broken=f.transport.fetch("/api/v1/projects/qa/sessions/a/conversation");
    f.response(f.requests().at(-1),gzipSync('{}').subarray(0,12),[["Content-Encoding","gzip"]]);
    await assert.rejects((await broken).json());
  } finally {f.transport.close();}
});
test("foreground requests bypass queued history and cancelled reads leave the queue",async()=>{
  const f=fixture();await f.connected;
  try {
    const active=Array.from({length:3},()=>f.transport.fetch("/api/v1/settings"));
    const abort=new AbortController();const older=f.transport.fetch("/api/v1/projects/qa/sessions/a/conversation?before=4",{signal:abort.signal});
    const rejected=assert.rejects(older,{name:"AbortError"});
    const latest=f.transport.fetch("/api/v1/projects/qa/sessions/b/conversation?limit=4");
    abort.abort();await rejected;
    f.response(f.requests()[0],new TextEncoder().encode('{}'));await (await active[0]).json();
    assert.ok(f.requests().at(-1).path.includes("/b/"));assert.equal(f.requests().length,4);
    for(let i=1;i<3;i++){f.response(f.requests()[i],new TextEncoder().encode('{}'));await (await active[i]).json();}
    f.response(f.requests()[3],new TextEncoder().encode('{}'));await (await latest).json();
  } finally {f.transport.close();}
});
