import { newPromptContext } from "./prompt-context.js";
import { t } from "../../i18n.js";
import { errorMessage, toast } from "../../utils/dom.js";

export const MAX_FILE_BYTES = 8 * 1024 * 1024;
const INLINE_BYTES = 48 * 1024;
export function uploadLeaf(file, id) {
  const extension = /\.([a-z0-9]{1,16})$/i.exec(file.name ?? "")?.[1]?.toLowerCase() || "bin";
  return `${id}.${extension}`;
}
export function textFileCandidate(file) {
  return /^(text\/|application\/(json|xml|javascript|x-yaml)(;|$))/.test(file.type ?? "") ||
    /\.(txt|md|mdx|csv|tsv|jsonl?|log|ya?ml|xml|toml|ini|cfg|conf|env|[cm]?[jt]sx?|py|c|h|cpp|hpp|rs|go|java|sh|ps1|bat|css|html|sql)$/i.test(file.name ?? "") ||
    /^(makefile|dockerfile|cmakelists\.txt|\.gitignore)$/i.test(file.name ?? "");
}
export function decodeTextFile(bytes) {
  const data = new Uint8Array(bytes);
  const encoding = data[0] === 255 && data[1] === 254 ? "utf-16le" :
    data[0] === 254 && data[1] === 255 ? "utf-16be" : "utf-8";
  const text = new TextDecoder(encoding, { fatal: true }).decode(data);
  if (text.includes("\0")) throw new TypeError("Binary data");
  return text;
}

// Every ingress shares this importer. Small text is portable prompt context;
// opaque/large files keep their bytes in session storage, never in JSON/base64.
export function createFileImporter({ input, owner, add, canAdd = () => true,
  store, onBusy = () => {}, onError = error => toast(errorMessage(error), "error") }) {
  const jobs = new Set();
  async function importFiles(files) {
    if (input.disabled || input.readOnly) return false;
    const job = { origin: owner() }; jobs.add(job); onBusy();
    const current = () => owner() === job.origin;
    try {
      for (const candidate of files) {
        if (!current()) return false;
        const file = candidate.file ?? candidate;
        try {
          if (!Number.isSafeInteger(file.size) || file.size < 0 || file.size > MAX_FILE_BYTES)
            throw new Error(t("file.maxSize"));
          const label = (file.name || t("file.unnamed")).slice(0, 150);
          let context;
          if (file.size <= INLINE_BYTES && textFileCandidate(file)) {
            try { context = newPromptContext("text", label, { content: decodeTextFile(await file.arrayBuffer()) }); }
            catch { /* Non-UTF text and binary files retain their exact bytes below. */ }
          }
          if (!current()) return false;
          if (context && canAdd(context)) { if (!add(context)) return false; continue; }
          const reference = newPromptContext("file", label, { path: "@uploads/" + uploadLeaf(file, "0".repeat(32)) });
          if (!canAdd(reference)) throw new Error(t("context.limit"));
          if (!store) throw new Error(t("context.textFileLimit"));
          if (!file.size) throw new Error(t("file.empty"));
          const stored = await store(file, { current, rebind: value => { job.origin = value; onBusy(); } });
          if (!current()) return false;
          if (!/^@uploads\/[0-9a-f]{32}\.[a-z0-9]{1,16}$/.test(stored.path)) throw new Error(t("file.invalidReply"));
          if (!add({ ...reference, path: stored.path })) return false;
        } catch (error) { if (current()) onError(error); }
      }
      if (current()) input.focus({ preventScroll: true });
      return current();
    } finally { jobs.delete(job); onBusy(); }
  }
  return { importFiles, isBusy: () => [...jobs].some(job => job.origin === owner()),
    hasInFlight: () => jobs.size > 0 };
}
