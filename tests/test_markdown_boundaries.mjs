import test from "node:test";
import assert from "node:assert/strict";

// Parsing belongs to the displaying host even with a read-only remote target.
globalThis.sessionStorage = { getItem: key => key === "mdo.target.v1" ? JSON.stringify({
  id: "d".repeat(32), owner: "2", name: "Remote", mode: "view",
}) : null };
const { markdownAddress, markdownResourceUrl } = await import("../app/web/js/features/chat/markdown-assets.js");
const { isTargetImagePath } = await import("../app/web/js/api/target-image.js");
const { parseMarkdown } = await import("../app/web/js/features/chat/markdown-transport.js");

test("Markdown native file addresses preserve Unicode, spaces and line navigation", () => {
  assert.deepEqual(markdownAddress("./中文%20file.txt#L3-L6"), { kind: "file", path: "./中文 file.txt", line: 3 });
  assert.deepEqual(markdownAddress("D:\\工作区\\file.c:12:4"), { kind: "file", path: "D:/工作区/file.c", line: 12 });
  assert.deepEqual(markdownAddress("file:///D:/work/a%20b.c:7"), { kind: "file", path: "D:/work/a b.c", line: 7 });
  assert.deepEqual(markdownAddress("/tmp/a.txt:2"), { kind: "file", path: "/tmp/a.txt", line: 2 });
  assert.equal(markdownAddress("#中文标题").kind, "anchor");
  assert.equal(markdownAddress("//example.com/a").url, "https://example.com/a");
});

test("executable, encoded, network-file and invalid image addresses are rejected", () => {
  for (const address of ["javascript:alert(1)", "java%73cript:alert(1)", "vbscript:x", "data:text/html,hello",
    "file://server/share/a.png", "\\\\server\\share\\a.png", "a%00.png", "a\nb", "a%zz.png", "x:0", "x:9007199254740992"])
    assert.equal(markdownAddress(address), null, address);
  for (const address of ["data:image/svg+xml;base64,PHN2Zz4=", "mailto:a@example.com", "#heading"])
    assert.equal(markdownAddress(address, true), null, address);
  assert.equal(markdownAddress("data:image/png;base64,aGVsbG8=", true).kind, "web");
});

test("target image transport admits only bounded, scoped image resources", () => {
  const owner = { projectId: "default", sessionId: "s123" };
  const url = markdownResourceUrl(owner, "中文 图片.png");
  assert(isTargetImagePath(url));
  assert(isTargetImagePath("/api/v1/projects/default/sessions/s123/attachments/" + "a".repeat(32)));
  assert.equal(markdownResourceUrl({}, "a.png"), null);
  for (const path of ["https://example.com" + url, url + "&mode=image", url + "&extra=x",
    url.replace("mode=image", "mode=file"), markdownResourceUrl(owner, "a".repeat(1025)), "/api/v1/settings"])
    assert.equal(isTargetImagePath(path), false, path);
});

test("display parser refreshes its own write token without issuing a remote mutation", async () => {
  const calls = []; let bootstraps = 0;
  globalThis.fetch = async (path, options = {}) => {
    calls.push({ path, options });
    if (path === "/api/v1/bootstrap") return Response.json({ ok: true }, { headers: { "X-Mdo-Write-Token": `local-${++bootstraps}` } });
    const token = options.headers["X-Mdo-Write-Token"];
    if (token === "local-1") return Response.json({ ok: false, error: { code: "write_token_conflict" } }, { status: 409 });
    assert.equal(token, "local-2");
    assert.deepEqual(JSON.parse(options.body), { method: "markdown.parse", params: { source: "# hello" }, context: {} });
    return Response.json({ ok: true, data: { engine: "md4c", schema_version: 1, nodes: [] } });
  };
  assert.equal((await parseMarkdown("# hello")).engine, "md4c");
  assert.deepEqual(calls.map(call => call.path), ["/api/v1/bootstrap", "/api/v1/application/call", "/api/v1/bootstrap", "/api/v1/application/call"]);
});
