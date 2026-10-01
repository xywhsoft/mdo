import assert from "node:assert/strict";
import test from "node:test";

import { loadSessionMarkdownImages } from
  "../app/web/js/features/sessions/session-export-images.js";
import { formatSessionMarkdown } from
  "../app/web/js/features/sessions/session-export.js";

const session = { project_id: "default", id: "S1", title: "Images" };
const ids = ["a".repeat(32), "b".repeat(32), "c".repeat(32)];
const png = Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jG1kAAAAASUVORK5CYII=", "base64");
const metadata = (id, extra = {}) => ({ id, schema_version: 2,
  mime_type: "image/png", size: png.length, file_name: '截图 [1] "100%".png', ...extra });
const transcript = (attachments, repeat = false) => ({ events: [
  { event_id: 1, kind: "agent_start", agent_depth: 0, run_id: 1,
    user_message_sequence: 1, text: "", attachments },
  ...(repeat ? [{ event_id: 2, kind: "agent_start", agent_depth: 0,
    run_id: 2, user_message_sequence: 2, text: "", attachments }] : []),
] });
const imageResponse = (bytes = png, headers = {}) => new Response(bytes,
  { headers: { "Content-Type": "image/png", ...headers } });

async function withFetch(fetch, body) {
  const original = globalThis.fetch;
  globalThis.fetch = fetch;
  try { await body(); } finally { globalThis.fetch = original; }
}

test("pure-image Markdown keeps original bytes/names and deduplicates repeated image payloads", async () => {
  const requests = [];
  const source = transcript([ids[0]], true);
  await withFetch(async (url, options) => {
    requests.push(String(url));
    assert.equal(options.credentials, "same-origin");
    assert.equal(options.cache, "no-store");
    return String(url).endsWith("/info")
      ? Response.json({ ok: true, data: metadata(ids[0]) }) : imageResponse();
  }, async () => {
    const exported = await loadSessionMarkdownImages(session, source);
    assert.equal(exported.imagesIncomplete, false);
    assert.equal(source.images, undefined);
    assert.equal(exported.images.get(ids[0]).name, metadata(ids[0]).file_name);
    assert.deepEqual(Buffer.from(exported.images.get(ids[0]).dataUrl.split(",")[1], "base64"), png);
    assert.deepEqual(requests, [
      `/api/v1/projects/default/sessions/S1/attachments/${ids[0]}/info`,
      `/api/v1/projects/default/sessions/S1/attachments/${ids[0]}`,
    ]);
    const markdown = formatSessionMarkdown(session, exported);
    assert.equal((markdown.match(/\]: data:image\/png;base64,/g) || []).length, 1);
    assert.equal(markdown.split('![截图 \\[1\\] "100%"\\.png]').length - 1, 2);
    assert.doesNotMatch(markdown, /部分图片未包含|\[Image attachment\]|blob:|http:\/\/localhost/);
  });
});

test("a missing image does not prevent later images, and partial Markdown retains its traceable ID", async () => {
  await withFetch(async (url) => {
    if (String(url).includes(ids[0])) return Response.json({ ok: false,
      error: { code: "attachment_not_found" } }, { status: 404 });
    return String(url).endsWith("/info") ? Response.json({ ok: true,
      data: metadata(ids[1], { schema_version: 1, file_name: undefined }) }) : imageResponse();
  }, async () => {
    const exported = await loadSessionMarkdownImages(session, transcript(ids.slice(0, 2)));
    assert.equal(exported.imagesIncomplete, true);
    assert.equal(exported.images.get(ids[1]).name, ids[1]);
    const markdown = formatSessionMarkdown(session, exported);
    assert.ok(markdown.includes(`- ${ids[0]}`));
    assert.match(markdown, /部分图片未包含/);
    assert.ok(markdown.includes(`[mdo-image-${ids[1]}]: data:image/png;base64,`));
  });
});

test("image export rejects wrong identities, unsafe names, oversized metadata and mismatched streams", async () => {
  const cases = [
    [metadata(ids[1]), imageResponse()],
    [metadata(ids[0], { file_name: "../escape.png" }), imageResponse()],
    [metadata(ids[0], { size: 8 * 1024 * 1024 + 1 }), imageResponse()],
    [metadata(ids[0]), imageResponse(png.subarray(1))],
    [metadata(ids[0]), imageResponse(Buffer.concat([png, Buffer.from([0])]))],
    [metadata(ids[0]), imageResponse(png, { "Content-Type": "text/html" })],
    [metadata(ids[0]), imageResponse(png, { "Content-Length": String(png.length + 1) })],
  ];
  for (const [data, response] of cases) {
    await withFetch(async (url) => String(url).endsWith("/info")
      ? Response.json({ ok: true, data }) : response, async () => {
      const exported = await loadSessionMarkdownImages(session, transcript([ids[0]]));
      assert.equal(exported.images.size, 0);
      assert.equal(exported.imagesIncomplete, true);
    });
  }
});

test("image count and transfer budgets fence further payload reads, including failed reservations", async () => {
  for (const [bounds, shortRead] of [
    [{ maxImages: 1 }, false], [{ maxBytes: png.length }, false],
    [{ maxBytes: png.length }, true],
  ]) {
    const imageRequests = [];
    await withFetch(async (url) => {
      const id = String(url).includes(ids[0]) ? ids[0] : ids[1];
      if (String(url).endsWith("/info")) return Response.json({ ok: true, data: metadata(id) });
      imageRequests.push(String(url));
      return imageResponse(shortRead ? png.subarray(1) : png);
    }, async () => {
      const exported = await loadSessionMarkdownImages(session,
        transcript(ids.slice(0, 2)), bounds);
      assert.equal(imageRequests.length, 1);
      assert.equal(exported.images.size, shortRead ? 0 : 1);
      assert.equal(exported.imagesIncomplete, true);
    });
  }
});

test("the export deadline cancels an outstanding image and never starts later image reads", async () => {
  const requests = [];
  let aborted = false;
  await withFetch(async (url, { signal }) => {
    requests.push(String(url));
    if (String(url).endsWith("/info")) return Response.json({ ok: true, data: metadata(ids[0]) });
    return new Promise((resolve, reject) => signal.addEventListener("abort", () => {
      aborted = true;
      reject(new DOMException("Aborted", "AbortError"));
    }, { once: true }));
  }, async () => {
    const exported = await loadSessionMarkdownImages(session,
      transcript(ids.slice(0, 2)), { timeoutMs: 10 });
    assert.equal(aborted, true);
    assert.equal(requests.length, 2);
    assert.equal(exported.images.size, 0);
    assert.equal(exported.imagesIncomplete, true);
  });
});
