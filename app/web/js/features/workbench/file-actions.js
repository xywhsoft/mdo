export const fileName = path => path.split(/[\\/]/).filter(Boolean).at(-1) || path;
export const fileParent = path => { const parent = path.replace(/[\\/][^\\/]+[\\/]?$/, "") || "/"; return /^[a-z]:$/i.test(parent) ? `${parent}/` : parent; };
export const fileJoin = (path, name) => `${path.replace(/[\\/]$/, "")}/${name}`;
const windows = path => /^(?:[a-z]:[\\/]|\\\\)/i.test(path);
export function filePathKey(path) {
  const key = String(path || "").replaceAll("\\", "/").replace(/\/$/, "");
  // Preserve entry case: Windows can also host case-sensitive directories.
  return windows(path) ? key.replace(/^[A-Z]:/, drive => drive.toLowerCase()) : key;
}
export function relocatedPath(path, source, destination, directory = false) {
  const key = filePathKey(path), from = filePathKey(source);
  if (key === from) return destination;
  return directory && key.startsWith(`${from}/`) ? destination.replace(/[\\/]$/, "") + path.slice(source.replace(/[\\/]$/, "").length) : path;
}
export function validateEntryName(value, parent) {
  if (!value || [".", ".."].includes(value) || /[\\/\x00-\x1f\x7f]/.test(value)) throw new Error("请输入单个文件名，名称不能包含路径分隔符或控制字符。");
  if (windows(parent)) {
    // Win32 also reserves COM/LPT with superscript 1/2/3, even with extensions.
    if (/[<>:"|?*]/.test(value) || /[. ]$/.test(value) || /^(con|prn|aux|nul|com[1-9¹²³]|lpt[1-9¹²³])(?:\.|$)/i.test(value)) throw new Error("这个名称不能用于 Windows 文件，请更换名称。");
    if (value.length > 255) throw new Error("文件名超过 255 个字符。");
  } else if (new TextEncoder().encode(value).length > 255) throw new Error("文件名超过 255 字节。");
  return value;
}

/** Only explicit UI actions mutate files. Metadata conflicts refresh the view
 * and require a new user decision; uncertain operations are never replayed. */
export function createFileActions({ sdk, context, signal, directory, events, state = () => {}, created = () => {}, refresh = () => {}, ask = (label, value) => prompt(label, value), approve = label => confirm(label) }) {
  let busy = false;
  const perform = async (kind, entry, execute) => {
    if (busy) throw new Error("文件操作尚未完成，请稍候。");
    busy = true; state(true);
    try {
      const result = await execute();
      events?.dispatchEvent(new CustomEvent("changed", { detail: { ...entry, kind, project: context.project } }));
      return result;
    } catch (error) {
      await Promise.resolve().then(refresh).catch(() => {});
      if (error.status === 412) throw new Error("条目已发生变化，已刷新列表；请重新确认后操作。原有编辑仍然保留。");
      if (error.status === 409 && kind !== "remove") throw new Error("同名条目已存在或正在使用，没有覆盖它的内容。");
      if (error.details?.may_have_completed) throw new Error(`操作结果尚需核对，请刷新列表检查后再决定：${error.message}`);
      throw error;
    } finally { busy = false; state(false); }
  };
  const protectedEntry = entry => {
    if (!entry?.path || filePathKey(entry.path) === filePathKey(directory())) throw new Error("不能重命名或删除当前浏览根目录，请从它的父目录操作。");
    if (!entry.revision) throw new Error("文件状态尚未读取，请刷新后再操作。");
  };
  return {
    get busy() { return busy; },
    async create(parent = directory(), folder = false) {
      const value = ask(`${folder ? "新建文件夹" : "新建文件"}\n${parent}`, ""); if (value === null) return;
      const path = fileJoin(parent, validateEntryName(value, parent));
      const result = await perform("create", { path, directory: folder }, () => sdk.call(folder ? "files.mkdir" : "files.create", { path, space: "native", ...(folder ? { exclusive: true } : {}) }, { signal }));
      if (!signal.aborted) await created(path, folder, result);
    },
    async rename(entry) {
      protectedEntry(entry); const value = ask(`重命名\n${entry.path}`, fileName(entry.path)); if (value === null || value === fileName(entry.path)) return;
      const destination = fileJoin(fileParent(entry.path), validateEntryName(value, entry.path));
      await perform("move", { path: entry.path, destination, directory: entry.type === 2 }, () => sdk.call("files.move", { path: entry.path, destination, space: "native", revision: entry.revision, replace: false }, { signal }));
    },
    async remove(entry) {
      protectedEntry(entry);
      if (!approve(`永久删除${entry.type === 2 ? "这个文件夹及其中内容" : "这个条目"}？\n${entry.path}\n未保存的编辑会保留为本机草稿。`)) return;
      await perform("remove", { path: entry.path, directory: entry.type === 2 }, () => sdk.call("files.remove", { path: entry.path, space: "native", revision: entry.revision, recursive: entry.type === 2 }, { signal }));
    },
  };
}
