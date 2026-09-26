import assert from "node:assert/strict";
import test from "node:test";

import { imageUploadType } from
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
