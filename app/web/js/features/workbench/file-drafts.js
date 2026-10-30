import { t as uiText } from "../../i18n.js";
/** Per-panel working copies. Saving one editor must not clear another editor's
 * unsaved text. A bounded metadata index makes closed-panel drafts discoverable
 * without scanning every key or deleting other working copies. */
export function createFileDrafts({ context, panel, storage = () => localStorage }) {
  const scope = [context.target || context.device || "local", context.project, context.extension || "local-client"];
  const indexKey = path => `mdo.file.draft.index2.${JSON.stringify([...scope, path])}`;
  const key = (path, id = panel) => `mdo.file.draft.v2.${JSON.stringify([...scope, path, id])}`;
  const parse = value => value === null ? null : JSON.parse(value);
  const index = path => {
    const values = parse(storage().getItem(indexKey(path))) || [];
    if (!Array.isArray(values) || values.length > 64 || values.some(value => typeof value !== "string" || value.length > 128)) throw new Error(uiText("ui.fileDrafts.draftIndexUnavailableKeepThisPageOpenAndSave", {}, "草稿索引不可用，请保留当前页面并保存文件。"));
    return values;
  };
  const valid = value => value && typeof value.text === "string" && value.text.length <= 1024 * 1024;
  return {
    read(path, fallback = "") {
      const own = parse(storage().getItem(key(path))); if (own) return valid(own) ? own : null;
      if (fallback) { const old = parse(storage().getItem(key(fallback))); if (old) return valid(old) ? old : null; }
      let latest = null;
      for (const id of index(path)) {
        let value; try { value = parse(storage().getItem(key(path, id))); } catch { continue; }
        if (valid(value) && (!latest || value.savedAt > latest.savedAt)) latest = value;
      }
      return latest;
    },
    own(path, fallback = "") {
      const own = parse(storage().getItem(key(path))); if (own) return valid(own) || own.cleared === true ? own : null;
      const old = fallback && parse(storage().getItem(key(fallback))); return valid(old) || old?.cleared === true ? old : null;
    },
    write(path, value) {
      const values = index(path);
      if (!values.includes(panel)) storage().setItem(indexKey(path), JSON.stringify([...values, panel].slice(-64)));
      storage().setItem(key(path), JSON.stringify(value));
    },
    clear(path) {
      // A small marker prevents this saved/discarded editor from importing a
      // different editor's working copy on its next page reload.
      storage().setItem(key(path), JSON.stringify({ cleared: true }));
      storage().setItem(indexKey(path), JSON.stringify(index(path).filter(id => id !== panel)));
    },
    legacy(path) {
      // Old records did not capture the displaying target/owner. Offer these
      // only through an explicit recovery action, never merge them silently.
      const value = parse(storage().getItem(`mdo.file.draft.${JSON.stringify([context.device, context.project, path])}`));
      return valid(value) ? value : null;
    },
  };
}
