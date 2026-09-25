// Parse every shipped ES module without executing browser-only code.
// `node --check` does not reliably validate this module graph on all hosts.
import { readFileSync, readdirSync } from "node:fs";
import { join, relative } from "node:path";
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
for (const path of files) {
  try {
    new vm.SourceTextModule(readFileSync(path, "utf8"), { identifier: path });
  } catch (error) {
    throw new Error(`${relative(root, path)}: ${error.message}`, { cause: error });
  }
}
console.log(`frontend module syntax: ${files.length} modules parsed`);
