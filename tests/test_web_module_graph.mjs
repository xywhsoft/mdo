import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";

const checker = readFileSync(new URL("../tools/check_web_modules.mjs", import.meta.url), "utf8");

function check(files) {
  const directory = mkdtempSync(join(tmpdir(), "mdo-module-graph-"));
  try {
    const content = { "tools/check_web_modules.mjs": checker,
      "app/web/index.html": '<script type="module">import("/js/main.js");</script>',
      ...files };
    for (const [path, text] of Object.entries(content)) {
      const target = join(directory, path);
      mkdirSync(dirname(target), { recursive: true });
      writeFileSync(target, text);
    }
    const result = spawnSync(process.execPath, ["--experimental-vm-modules",
      join(directory, "tools/check_web_modules.mjs")], { encoding: "utf8" });
    return { status: result.status, output: result.stdout + result.stderr };
  } finally {
    assert.equal(dirname(directory), resolve(tmpdir()));
    rmSync(directory, { recursive: true, force: true });
  }
}

test("module checker accepts reachable static and lazy local imports", () => {
  const result = check({ "app/web/js/main.js": 'import "./shared.js"; import("./lazy.js");',
    "app/web/js/shared.js": "export const value = 1;",
    "app/web/js/lazy.js": "export const lazy = 2;" });
  assert.equal(result.status, 0, result.output);
  assert.match(result.output, /3 modules parsed and reachable/);
});

test("module checker rejects orphan modules and missing import targets", () => {
  for (const [files, message] of [
    [{ "app/web/js/main.js": "export const main = 1;",
      "app/web/js/orphan.js": "export const orphan = 2;" }, /Unreachable frontend modules/],
    [{ "app/web/js/main.js": 'import "./missing.js";' }, /Missing frontend module/],
    [{ "app/web/js/main.js": 'import "../outside.js";' }, /Nonlocal frontend import/],
  ]) {
    const result = check(files);
    assert.notEqual(result.status, 0);
    assert.match(result.output, message);
  }
});
