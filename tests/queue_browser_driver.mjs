// Bounded CDP runner using Node's built-in WebSocket; no browser dependencies.
import { spawn } from "node:child_process";
import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { setTimeout as delay } from "node:timers/promises";

const [browser, profile, url, width, screenshot, testScript] = process.argv.slice(2);
const child = spawn(browser, ["--headless=new", "--disable-gpu", "--no-first-run",
  "--disable-background-networking", "--disable-extensions", "--remote-debugging-port=0",
  `--user-data-dir=${profile}`, `--window-size=${width},900`, "about:blank"],
{ windowsHide: true, stdio: "ignore" });
const timeout = setTimeout(() => { child.kill(); process.exitCode = 1; }, testScript ? 45000 : 25000);
let socket;
try {
  let port;
  for (let i = 0; i < 100 && !port; ++i) {
    try { port = (await readFile(join(profile, "DevToolsActivePort"), "utf8")).split("\n")[0]; }
    catch { await delay(50); }
  }
  if (!port) throw new Error("Browser debugging port unavailable");
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  socket = new WebSocket(targets.find(target => target.type === "page").webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
  let sequence = 0;
  const pending = new Map();
  const diagnostics = [];
  socket.onmessage = event => {
    const response = JSON.parse(event.data);
    if (response.method === "Runtime.exceptionThrown") diagnostics.push(response.params?.exceptionDetails);
    if (response.method === "Runtime.consoleAPICalled" && response.params?.type === "error") diagnostics.push(response.params?.args);
    const waiter = pending.get(response.id);
    if (waiter) { pending.delete(response.id); response.error ? waiter.reject(response.error) : waiter.resolve(response.result); }
  };
  const call = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++sequence; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params }));
  });
  await call("Runtime.enable");
  await call("Page.enable");
  await call("Emulation.setDeviceMetricsOverride", { width: Number(width), height: 900, deviceScaleFactor: 1, mobile: false });
  if (testScript) await call("Page.addScriptToEvaluateOnNewDocument", { source: await readFile(testScript, "utf8") });
  await call("Page.navigate", { url });
  let report;
  for (let i = 0; i < (testScript ? 700 : 160) && !report; ++i) {
    await delay(50);
    const result = await call("Runtime.evaluate", { expression: "document.querySelector('#result')?.textContent", returnByValue: true });
    const candidate = result.result?.value;
    try { if (typeof JSON.parse(candidate).passed === "boolean") report = candidate; } catch { /* Fixture is still running. */ }
  }
  if (!report) {
    const state = await call("Runtime.evaluate", { expression: "JSON.stringify({url:location.href,ready:document.readyState,body:document.body?.innerText.slice(-1800),result:document.querySelector('#result')?.textContent})", returnByValue: true });
    throw new Error("Queue browser fixture did not finish: " + state.result?.value + "; " + JSON.stringify(diagnostics));
  }
  const shot = await call("Page.captureScreenshot", { format: "png" });
  await writeFile(screenshot, Buffer.from(shot.data, "base64"));
  process.stdout.write(report);
  if (testScript && !JSON.parse(report).passed) process.stderr.write(JSON.stringify(diagnostics));
} finally {
  clearTimeout(timeout); socket?.close(); child.kill();
  await new Promise(resolve => child.exitCode !== null ? resolve() : child.once("exit", resolve));
}
