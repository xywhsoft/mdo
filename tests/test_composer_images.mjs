import assert from "node:assert/strict";
import test from "node:test";

import { imageTransferFiles, imageUploadType } from
  "../app/web/js/features/chat/composer-images.js";

test("OS files with no MIME retain supported image uploads", () => {
  assert.equal(imageUploadType({ name: "capture.PNG", type: "" }), "image/png");
  assert.equal(imageUploadType({ name: "photo.JpEg", type: "" }), "image/jpeg");
  assert.equal(imageUploadType({ name: "frame.webp", type: "" }), "image/webp");
  assert.equal(imageUploadType({ name: "capture.PNG",
    type: "application/octet-stream" }), "image/png");
  assert.equal(imageUploadType({ name: "notes.txt", type: "" }), "");
});

test("a supplied MIME is authoritative over the filename", () => {
  assert.equal(imageUploadType({ name: "photo.txt", type: "image/png" }),
    "image/png");
  assert.equal(imageUploadType({ name: "photo.png", type: "text/plain" }), "");
  assert.equal(imageUploadType({ name: "photo.png", type: "image/gif" }), "");
});

test("a partially readable items view cannot hide files from a multi-image paste", () => {
  const first = { name: "first.png", type: "image/png" };
  const second = { name: "second.png", type: "image/png" };
  const entries = imageTransferFiles({ files: [first, second], items: [
    { kind: "file", type: "image/png", getAsFile: () => first },
    { kind: "file", type: "image/png", getAsFile: () => null },
  ] });
  assert.deepEqual(entries, [{ file: first, mime: "image/png" },
    { file: second, mime: "image/png" }]);
});

test("both transfer views upload each selected file once, including matching metadata", () => {
  const first = { name: "capture.png", type: "image/png", size: 4 };
  const second = { ...first };
  const entries = imageTransferFiles({ files: [first, second], items: [
    { kind: "file", type: "image/png", getAsFile: () => ({ ...first }) },
    { kind: "file", type: "image/png", getAsFile: () => ({ ...second }) },
  ] });
  assert.equal(entries.length, 2);
  assert.equal(entries[0].file, first);
  assert.equal(entries[1].file, second);
});

test("file-item MIME hints fill unspecified types without overriding a supplied type", () => {
  const files = [{ name: "blob", type: "" },
    { name: "blob", type: "application/octet-stream" },
    { name: "capture.png", type: "text/plain" }];
  const entries = imageTransferFiles({ files, items: [
    { kind: "string", type: "text/plain" },
    { kind: "file", type: "image/png" },
    { kind: "file", type: "image/webp" },
    { kind: "file", type: "image/png" },
  ] });
  assert.deepEqual(entries.map((entry) => entry.mime), ["image/png", "image/webp", ""]);
});

test("items-only transfers retain files and MIME hints while skipping unreadable entries", () => {
  const image = { name: "blob", type: "" };
  const legacy = { name: "blob", type: "" };
  const text = { name: "notes.txt", type: "text/plain" };
  const entries = imageTransferFiles({ items: [
    { kind: "string", getAsFile() { throw new Error("not a file"); } },
    { kind: "file", type: "image/png", getAsFile: () => image },
    { type: "image/jpeg", getAsFile: () => legacy },
    { kind: "file", type: "image/png", getAsFile: () => null },
    { kind: "file", type: "text/plain", getAsFile: () => text },
  ] });
  assert.deepEqual(entries, [{ file: image, mime: "image/png" },
    { file: legacy, mime: "image/jpeg" }, { file: text, mime: "" }]);
});

test("ordinary text and absent transfers contain no image files", () => {
  assert.deepEqual(imageTransferFiles(null), []);
  assert.deepEqual(imageTransferFiles({ files: [],
    items: [{ kind: "string", type: "text/plain", getAsFile: () => null }] }), []);
});
