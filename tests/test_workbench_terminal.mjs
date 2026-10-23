import test from "node:test";
import assert from "node:assert/strict";
import { createTerminalInput, terminalBytes } from "../app/web/js/features/workbench/terminal-io.js";
const lease = { id: "pty-id", epoch: "epoch", client: "view", control_token: "ctl", input_epoch: "inp", next_sequence: 1 };
const pause = () => new Promise(resolve => setTimeout(resolve, 5));
const until = async condition => { for(let i=0;i<200&&!condition();i++)await pause();assert.ok(condition()); };
const ack = params => ({ sequence:params.sequence, accepted:atob(params.base64).length, epoch:params.epoch });

test("Unicode is encoded once, chunks stay ordered under backpressure, queue is bounded", async () => {
 const sent=[],data=new TextEncoder().encode("中文🌙".repeat(3000));let failures=2;
 const io=createTerminalInput({lease, retryMs:1, chunkSize:101, write:async p=>{ if(failures-->0)throw {code:"terminal_input_backpressure"};sent.push(p);return ack(p); },fault:error=>assert.fail(error)});
 assert.throws(()=>io.enqueue(new Uint8Array(65537)),/64 KiB/);assert.equal(sent.length,0);
 io.enqueue(data);await until(()=>io.pending===0);
 assert.equal(new TextDecoder().decode(Uint8Array.from(sent.flatMap(p=>[...atob(p.base64)].map(c=>c.charCodeAt(0))))),new TextDecoder().decode(data));
 assert.deepEqual(sent.map(p=>p.sequence),sent.map((_,i)=>i+1));io.stop();
});
test("a lost ACK drops queued input, stops admission and never retries or reacquires", async()=>{
 let release,calls=0,failure;
 const io=createTerminalInput({lease,chunkSize:2,write:()=>{calls++;return new Promise((_,reject)=>{release=reject;});},fault:error=>{failure=error;}});
 io.enqueue(new Uint8Array([1,2,3,4,5]));assert.equal(io.pending,5);release(new Error("connection lost"));await until(()=>failure);
 assert.equal(calls,1);assert.equal(io.pending,0);assert.equal(io.enqueue(new Uint8Array([13])),false);await pause();assert.equal(calls,1);
});
test("disposing a view cancels only its input; late ACK cannot dispatch its queued tail", async()=>{
 let release,calls=0;
 const io=createTerminalInput({lease,chunkSize:1,write:params=>{calls++;return new Promise(resolve=>{release=()=>resolve(ack(params));});},fault:()=>assert.fail("disposed input published error")});
 io.enqueue(new Uint8Array([1,2,3]));io.stop();release();await pause();assert.equal(calls,1);assert.equal(io.pending,0);
});
test("a wrong receipt cannot advance the input sequence", async()=>{
 let calls=0,failure;
 const io=createTerminalInput({lease,chunkSize:1,write:async p=>{calls++;return {...ack(p),epoch:"other"};},fault:e=>{failure=e;}});
 io.enqueue(new Uint8Array([1,2]));await until(()=>failure);assert.equal(calls,1);assert.ok(io.stopped);
});
test("terminal byte cursors expose a gap and reject reordered/truncated transport",()=>{
 const base64=btoa("abc"),reply={offset:3,next:6,base64,gap:true};assert.deepEqual(terminalBytes(reply,0),new Uint8Array([97,98,99]));
 assert.throws(()=>terminalBytes({...reply,gap:false},0),/不连续/);assert.throws(()=>terminalBytes({...reply,next:9},0),/不连续/);assert.throws(()=>terminalBytes(reply,5),/不连续/);
});
