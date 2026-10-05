// Parse and check reachability without executing browser-only code.
// `node --check` does not reliably validate this module graph on all hosts.
import { readFileSync, readdirSync } from "node:fs";
import { dirname, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const root = fileURLToPath(new URL("../app/web/js/", import.meta.url));

function modules(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const path = join(directory, entry.name);
    return entry.isDirectory() ? modules(path) : entry.name.endsWith(".js") ? [path] : [];
  });
}

const files = modules(root).sort();
if (!files.length) throw new Error("No frontend modules found");
const graph = new Map();
for (const path of files) {
  try {
    const source = readFileSync(path, "utf8");
    const module = new vm.SourceTextModule(source, { identifier: path });
    const lazy = [...source.matchAll(/\bimport\s*\(\s*["']([^"']+)["']\s*\)/g)]
      .map((match) => match[1]);
    graph.set(path, [...module.dependencySpecifiers, ...lazy]);
  } catch (error) {
    throw new Error(`${relative(root, path)}: ${error.message}`, { cause: error });
  }
}
// index.html imports main.js; keep the graph rooted in the actual page entry.
const index = readFileSync(join(root, "../index.html"), "utf8");
const entries = [...index.matchAll(/\bimport\s*\(\s*["'](\/js\/[^"']+)["']\s*\)/g)]
  .map((match) => resolve(root, match[1].slice("/js/".length)));
if (!entries.length) throw new Error("No frontend entry imports in index.html");
const reached = new Set();
const pending = [...entries];
while (pending.length) {
  const path = pending.pop();
  if (reached.has(path)) continue;
  if (!graph.has(path)) throw new Error(`Missing frontend module: ${relative(root, path)}`);
  reached.add(path);
  for (const specifier of graph.get(path)) {
    const target = specifier.startsWith("/js/")
      ? resolve(root, specifier.slice("/js/".length))
      : specifier.startsWith(".") ? resolve(dirname(path), specifier) : null;
    if (!target || !target.startsWith(root + (root.endsWith(sep) ? "" : sep)))
      throw new Error(`Nonlocal frontend import: ${relative(root, path)} -> ${specifier}`);
    pending.push(target);
  }
}
const orphans = files.filter((path) => !reached.has(path));
if (orphans.length)
  throw new Error(`Unreachable frontend modules: ${orphans.map((path) => relative(root, path)).join(", ")}`);
console.log(`frontend module graph: ${files.length} modules parsed and reachable`);
