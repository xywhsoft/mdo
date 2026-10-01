import assert from "node:assert/strict";
import test from "node:test";
import { searchResultTop } from "../app/web/js/features/chat/timeline.js";

test("search keeps role and time when a complete first text line also fits", () => {
  const row = { top: 100 };
  const content = { top: 124, height: 200 };
  assert.equal(searchResultTop(row, content, 100, 200, 21), 100);
  assert.equal(searchResultTop(row, content, 100, 145, 21), 100);
});

test("cramped search reveals text instead of spending the viewport on metadata", () => {
  const row = { top: 100 };
  const content = { top: 124, height: 40 };
  assert.equal(searchResultTop(row, content, 100, 131, 21), 124);
  assert.equal(searchResultTop(row, content, 100, 145, 32), 124);
});

test("short text and rows without a separate body retain their natural position", () => {
  const row = { top: 100 };
  assert.equal(searchResultTop(row, { top: 124, height: 10 }, 100, 134, 21), 100);
  assert.equal(searchResultTop(row, { top: 100, height: 100 }, 100, 120, 21), 100);
});
