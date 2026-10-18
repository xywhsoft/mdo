// Real browser, production transport + connector, actual native gateway/relay.
// Playwright is an explicit test-only tool path; no product dependency is added.
import assert from "node:assert/strict";
import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
let input = "";
for await (const part of process.stdin) input += part;
const config = JSON.parse(input), { chromium } = require(config.playwright);
const origin = new URL(config.url);
assert.equal(origin.protocol, "http:"); assert.equal(origin.hostname, "127.0.0.1");
let browser, watchdog;
try {
  browser = await chromium.launch({ executablePath: config.browser, headless: true, timeout: 15000,
    args: ["--disable-gpu", "--disable-background-networking", "--no-first-run"] });
  watchdog = setTimeout(() => { void browser.close().catch(() => {}); }, 45000);
  const page = await browser.newPage(), errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.goto(config.url, { waitUntil: "load", timeout: 15000 });
  const result = await page.evaluate(async config => {
    const { createRemoteTransport } = await import("/js/api/remote-transport.js");
    const { connectorDirectSocket } = await import("/js/api/connector.js");
    const check = (value, message) => { if (!value) throw new Error(message); };
    const started = performance.now(), elapsed = () => Math.round(performance.now() - started);
    const states = [], attempts = [], requests = [], controls = [];
    const until = async (test, label, limit = 20000) => {
      const end = performance.now() + limit;
      while (!test()) { check(performance.now() < end, label); await new Promise(resolve => setTimeout(resolve, 20)); }
    };
    function observe(socket, route) {
      const send = socket.send.bind(socket);
      socket.send = value => {
        if (typeof value === "string") {
          const message = JSON.parse(value);
          if (message.type === "request") requests.push({ route, id: message.id, method: message.method, path: message.path });
        }
        send(value);
      };
      socket.addEventListener("message", event => {
        if (typeof event.data !== "string") return;
        const value = JSON.parse(event.data);
        if (value.type === "rudp_forward") {
          const payload = value.payload;
          controls.push({ type: payload.type, family: payload.family, families: payload.families });
        }
      });
      return socket;
    }
    const transport = createRemoteTransport({
      socket: (...args) => observe(new WebSocket(...args), "relay"),
      directSocket: async (offer, options) => {
        const route = offer.transport === "xrudp" ? "xrudp" : "lan";
        const attempt = { route, start_ms: elapsed(), closed_ms: null }; attempts.push(attempt);
        // Reachability policy only: preserve the actual fresh TLS identity and
        // authorization while putting both local fixture peers on loopback.
        const actual = route === "lan" ? { ...offer, addresses: ["127.0.0.1"],
          ...(config.lan ? {} : { port: config.silentPort }) } : offer;
        const socket = observe(await connectorDirectSocket(actual, options), route);
        attempt.socket = socket;
        socket.addEventListener("close", () => { attempt.closed_ms = elapsed(); });
        return socket;
      },
      onState: state => { if (state.connected) states.push({ route: state.route, at_ms: elapsed() }); },
    });
    try {
      await transport.connect(config.connection);
      await until(() => attempts[0]?.route === "xrudp", "native UDP attempt did not start", 3000);
      const boot = await fetch("/api/v1/project-purge-intent", { cache: "no-store" });
      const token = boot.headers.get("X-Mdo-Write-Token"); check(token, "missing native write token");
      async function write(id) {
        const response = await transport.fetch("/api/v1/projects", { method: "POST",
          headers: { "Content-Type": "application/json", "X-Mdo-Write-Token": token },
          body: JSON.stringify({ id, name: id }) });
        const value = await response.json(); check(response.status === 201 && value.data.id === id, "project write failed");
      }
      const early = config.prefix + "-early", late = config.prefix + "-late";
      await write(early);
      check(requests.filter(value => value.method === "POST").at(-1).route === "relay", "write during negotiation must stay on relay");
      await until(() => attempts.length === 2 && (config.lan ? states.at(-1)?.route === "lan" : attempts[1].closed_ms !== null),
        "both UDP families blocked but automatic fallback never completed");
      check(attempts.map(value => value.route).join(",") === "xrudp,lan", "unexpected direct retry or path order");
      check(attempts[0].closed_ms !== null && attempts[1].start_ms <= 14000, "native UDP attempt escaped its overall budget");
      check(controls.some(value => value.type === "rudp_init" && value.families?.join(",") === "4,6"), "both native families were not negotiated");
      for (const family of [4, 6]) check(controls.some(value => value.type === "rudp_candidates" && value.family === family), "missing native mapping family");
      check(!controls.some(value => value.type === "rudp_select"), "blackholed UDP was incorrectly selected");
      await write(late);
      const finalRoute = config.lan ? "lan" : "relay", posts = requests.filter(value => value.method === "POST");
      check(posts.length === 2 && new Set(posts.map(value => value.id)).size === 2, "a write was retried or duplicated");
      check(posts[1].route === finalRoute && states.at(-1).route === finalRoute, "fallback write used the wrong route");
      check(!requests.some(value => value.route === "xrudp"), "business entered an unconfirmed UDP path");
      const response = await transport.fetch("/api/v1/projects"), value = await response.json();
      check(response.status === 200, "fallback read failed");
      for (const id of [early, late]) check(value.data.items.filter(item => item.id === id).length === 1, "target project missing or duplicated");
      return { lan: config.lan, final_route: finalRoute, native_families: [4, 6], no_udp_business: true,
        early_write_route: posts[0].route, late_write_route: posts[1].route, writes: posts.length,
        unique_request_ids: new Set(posts.map(value => value.id)).size, target_projects: [early, late],
        attempts: attempts.map(({ route, start_ms, closed_ms }) => ({ route, start_ms, closed_ms })), elapsed_ms: elapsed() };
    } finally { transport.close(); }
  }, config);
  assert.deepEqual(errors, []);
  process.stdout.write(JSON.stringify(result) + "\n");
} finally { clearTimeout(watchdog); if (browser) await browser.close(); }
