// Attach only to the caller's independently installed Android QA WebView.
import { readFile } from 'node:fs/promises';
import { setTimeout as delay } from 'node:timers/promises';
const [port, devicePort, fixture] = process.argv.slice(2);
const target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json())
  .find(row => row.type === 'page' && row.url.startsWith(`http://127.0.0.1:${devicePort}/`));
if (!target) throw Error('The selected QA WebView is not available');
const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
const pending = new Map(); let sequence = 0;
const timer = setTimeout(() => { socket.close(); process.exitCode = 1; }, 40000);
socket.onmessage = ({ data }) => {
  const value = JSON.parse(data), waiter = pending.get(value.id);
  if (waiter) { pending.delete(value.id); value.error ? waiter.reject(value.error) : waiter.resolve(value.result); }
};
socket.onclose = () => { for (const waiter of pending.values()) waiter.reject(Error('QA WebView closed')); pending.clear(); };
const call = (method, params = {}) => new Promise((resolve, reject) => {
  const id = ++sequence; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params }));
});
try {
  await call('Page.enable'); await call('Runtime.enable');
  await call('Page.addScriptToEvaluateOnNewDocument', { source: await readFile(fixture, 'utf8') });
  await call('Page.reload', { ignoreCache: true });
  let report, state;
  for (let i = 0; i < 550; ++i) {
    await delay(60);
    const result = await call('Runtime.evaluate', { expression: 'JSON.stringify({report:document.querySelector("#result")?.textContent,stage:globalThis.__qaStage})', returnByValue: true });
    state = JSON.parse(result.result?.value ?? 'null');
    if (state?.report) { report = JSON.parse(state.report); break; }
  }
  if (!report) throw Error('Mobile directory fixture timed out: ' + JSON.stringify(state));
  process.stdout.write(JSON.stringify(report));
  if (!report.passed) process.exitCode = 1;
} finally { clearTimeout(timer); socket.close(); }
