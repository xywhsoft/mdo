import { element as make, icon, iconButton, popupMenu } from "./controls.js";
import { createSourceView } from "./source-view.js";
import { fileEncoding, encodeFileText, fileBase64 } from "./file-codec.js";
import { renderSourceMarkdown } from "../../utils/source-markdown.js";
import { toast } from "../../utils/dom.js";

const join = (path, name) => `${path.replace(/[\\/]$/, "")}/${name}`;
const name = path => path.split(/[\\/]/).at(-1);
const bytes = value => Uint8Array.from(atob(value), c => c.charCodeAt(0));
const failToast = error => toast(error.message || String(error), "error");
const EDIT_LIMIT = 1024 * 1024, VIEW_LIMIT = 2 * EDIT_LIMIT, IMAGE_LIMIT = 8 * EDIT_LIMIT;
const imageType = path => /\.png$/i.test(path) ? "image/png" : /\.jpe?g$/i.test(path) ? "image/jpeg" : /\.gif$/i.test(path) ? "image/gif" : /\.webp$/i.test(path) ? "image/webp" : "";

export async function mountFilesView(root, { sdk, context, params = {}, viewState = {}, signal, update, setDirty, addContext, openFile }) {
  root.classList.add("wb-files-view");
  const saved = viewState.files || {}, expanded = new Set(saved.expanded || []);
  let format = { encoding: "utf-8", bom: false, lineEnding: "\n" };
  let directory = saved.root || params.root || (await sdk.call("files.resolve", { path: "." }, { signal })).path;
  let hidden = Boolean(saved.hidden), file = null, text = "", decoder, readOffset = 0, binary = false, image = "", imageUrl = "", editing = false, dirty = false, loading = false, saving = false, conflict = false, source, watchTimer, searchTimer, saveTimer, sequence = 0, searchSequence = 0;
  const bar = make("div", "wb-files-toolbar"), heading = make("span", "wb-file-path", "文件"), body = make("div", "wb-files-body"), main = make("div", "wb-file-main"), content = make("div", "wb-file-content"), info = make("div", "wb-file-info"), tree = make("aside", "wb-file-tree"), treeHeading = make("div", "wb-tree-heading"), treeBody = make("div", "wb-tree-body"), search = make("input", "wb-file-search");
  const action = (glyph, label, run) => iconButton(glyph, label, run, failToast);
  const edit = action("compose", "编辑文件", () => editFile()), save = action("download", "保存文件", () => saveFile()), quote = action("review", "引用到对话", () => quoteFile());
  const preview = action("file", "切换 Markdown 预览", () => { main.dataset.markdown = main.dataset.markdown ? "" : "true"; renderContent(); });
  const showTree = action("folder", "切换文件树", () => { tree.hidden = !tree.hidden; remember(); });
  const more = action("more", "文件更多操作", () => {}); heading.title = directory;
  const findBar = make("div", "wb-file-find"), find = make("input"), findStatus = make("span"); findBar.hidden = true; find.placeholder = "查找已加载内容"; find.setAttribute("aria-label", find.placeholder);
  let findAt = -1, treeWidth = Math.max(150, Math.min(360, Number(saved.treeWidth) || 190));
  const moveTo = line => {
    if (editing) { const input = content.querySelector("textarea"), at = text.split("\n").slice(0, Math.max(0, Number(line) - 1)).join("\n").length; if (document.activeElement !== find) input?.focus(); input?.setSelectionRange(at, at); }
    else { if (main.dataset.markdown) { main.dataset.markdown = ""; renderContent(); } requestAnimationFrame(() => source?.go(line)); }
  };
  const findNext = () => {
    if (!find.value) return; findAt = text.indexOf(find.value, findAt + 1); if (findAt < 0) findAt = text.indexOf(find.value);
    findStatus.textContent = findAt < 0 ? "未找到" : `第 ${text.slice(0, findAt).split("\n").length} 行`;
    if (findAt >= 0) moveTo(text.slice(0, findAt).split("\n").length);
  };
  const showFind = () => { findBar.hidden = false; find.focus(); find.select(); };
  find.oninput = () => { findAt = -1; findNext(); }; find.onkeydown = event => { if (event.key === "Enter" && !event.isComposing) { event.preventDefault(); findNext(); } if (event.key === "Escape") { findBar.hidden = true; source?.root.focus(); } };
  findBar.append(find, findStatus, action("arrow-right", "下一个匹配", findNext), action("close", "关闭查找", () => { findBar.hidden = true; }));
  root.addEventListener("keydown", event => { if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "f") { event.preventDefault(); showFind(); } }, { signal });
  quote.onpointerdown = event => event.preventDefault();
  bar.append(heading, edit, save, quote, preview, showTree, more); main.append(content, info);
  search.placeholder = "搜索文件…"; search.setAttribute("aria-label", "搜索项目文件");
  treeHeading.append(make("span", "", name(directory) || directory), action("refresh", "刷新目录", () => refreshTree()));
  const separator = make("div", "wb-file-tree-resize"); separator.tabIndex = 0; separator.setAttribute("role", "separator"); separator.setAttribute("aria-label", "调整目录树宽度"); separator.setAttribute("aria-orientation", "vertical"); tree.style.width = `${treeWidth}px`;
  let pointerX = 0, pointerWidth = 0;
  separator.onpointerdown = event => { pointerX = event.clientX; pointerWidth = treeWidth; separator.setPointerCapture(event.pointerId); event.preventDefault(); };
  const setTreeWidth = value => { treeWidth = Math.max(150, Math.min(360, Math.max(150, root.clientWidth - 150), value)); tree.style.width = `${treeWidth}px`; };
  separator.onpointermove = event => { if (separator.hasPointerCapture(event.pointerId)) setTreeWidth(pointerWidth + pointerX - event.clientX); };
  separator.onpointerup = event => { if (separator.hasPointerCapture(event.pointerId)) { separator.releasePointerCapture(event.pointerId); remember(); } };
  separator.onkeydown = event => { if (["ArrowLeft", "ArrowRight"].includes(event.key)) { event.preventDefault(); setTreeWidth(treeWidth + (event.key === "ArrowLeft" ? 20 : -20)); remember(); } };
  tree.append(separator, search, treeHeading, treeBody); body.append(main, tree); root.append(bar, findBar, body); tree.hidden = Boolean(saved.treeHidden);
  let previousWidth = 0; const sizing = new ResizeObserver(() => { const width = root.clientWidth; if (width < 400 && width !== previousWidth && file) tree.hidden = true; previousWidth = width; }); sizing.observe(root);
  const currentPath = () => file?.path || "";
  const draftKey = () => `mdo.file.draft.${JSON.stringify([context.device, context.project, currentPath()])}`;
  const remember = () => {
    clearTimeout(saveTimer); saveTimer = setTimeout(() => {
      if (!signal.aborted) void update({ view_state: { ...viewState, files: { root: directory, expanded: [...expanded].slice(0, 256), hidden, selected: currentPath(), treeHidden: tree.hidden, treeWidth } } }).catch(failToast);
    }, 350);
  };
  const draft = () => {
    if (!dirty || !file) return;
    try { localStorage.setItem(draftKey(), JSON.stringify({ revision: file.revision, text, savedAt: Date.now() })); }
    catch { info.textContent = "草稿暂未保存到本机；请保留页面并保存文件。"; }
  };
  setDirty(() => dirty);
  const controls = () => {
    heading.textContent = currentPath() ? `${dirty ? "● " : ""}${name(currentPath())}` : "文件"; heading.title = currentPath() || directory;
    edit.hidden = !file || binary || Boolean(image) || editing; edit.disabled = loading || !file || file.size > EDIT_LIMIT;
    save.hidden = !editing; save.disabled = !dirty || loading || conflict;
    quote.disabled = !file || Boolean(image) || binary;
    preview.hidden = !file || !/\.md$/i.test(file.path) || editing;
  };
  const clearImage = () => { if (imageUrl) URL.revokeObjectURL(imageUrl); imageUrl = ""; };
  const renderContent = () => {
    source?.dispose(); source = null; content.replaceChildren(); controls();
    if (!file) { content.append(make("div", "wb-file-empty", "从目录中选择一个文件")); return; }
    if (image) {
      const img = make("img", "wb-file-image"); img.src = imageUrl; img.alt = name(file.path);
      const zoom = make("input"); zoom.type = "range"; zoom.min = "25"; zoom.max = "200"; zoom.value = "100"; zoom.setAttribute("aria-label", "图片缩放"); zoom.oninput = () => { img.style.width = `${zoom.value}%`; img.style.maxWidth = "none"; };
      content.append(zoom, img); return;
    }
    if (binary) { content.append(make("p", "wb-file-empty", `二进制文件 · ${file.size.toLocaleString()} 字节`)); return; }
    if (editing) {
      const input = make("textarea", "wb-file-editor"); input.value = text; input.spellcheck = false; input.setAttribute("aria-label", `编辑 ${name(file.path)}`);
      text = input.value;
      input.oninput = () => { text = input.value; dirty = true; draft(); controls(); };
      input.onkeydown = event => { if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") { event.preventDefault(); void saveFile().catch(failToast); } };
      content.append(input); return;
    }
    if (main.dataset.markdown) { const markdown = make("article", "wb-markdown-preview"); markdown.append(renderSourceMarkdown(text)); content.append(markdown); return; }
    source = createSourceView(text, file.path, { signal, line: params.line || 1 }); content.append(source.root);
  };
  const showInfo = () => {
    info.replaceChildren();
    if (!file) return;
    if (conflict) {
      info.append(make("span", "wb-error", "文件已被外部修改，编辑内容已保留。"), action("split", "比较磁盘内容", () => compare()), action("refresh", "重新读取磁盘文件", async () => {
        if (!confirm("放弃当前编辑，使用磁盘上的版本？")) return;
        try { localStorage.removeItem(draftKey()); } catch { /* Explicit discard also suppresses restoring this draft. */ }
        dirty = false; await loadFile(file.path, { force: true, discardDraft: true });
      }));
    } else {
      info.append(make("span", "", `${file.size.toLocaleString()} 字节${readOffset < file.size ? ` · 已加载 ${readOffset.toLocaleString()} 字节` : ""}`));
      if (!editing && !image && !binary && readOffset < file.size && readOffset < VIEW_LIMIT) info.append(action("plus", "继续加载文件", () => loadMore()));
      if (readOffset >= VIEW_LIMIT && readOffset < file.size) info.append(make("span", "", "已达预览缓存上限；可在系统应用中打开完整文件。"));
    }
  };
  const readChunk = async (at, expected, path = file.path) => {
    const value = await sdk.call("files.read", { path, space: "native", offset: at, limit: 262144 }, { signal });
    if (value.revision !== expected) throw new Error("读取过程中，文件发生了变化，请重新读取。");
    return value;
  };
  const loadMore = async () => {
    if (loading || !file || readOffset >= file.size) return;
    loading = true; const request = sequence;
    try {
      const value = await readChunk(readOffset, file.revision); if (request !== sequence || signal.aborted) return;
      readOffset = value.next;
      try { text += decoder.decode(bytes(value.base64), { stream: readOffset < file.size }); }
      catch { binary = true; renderContent(); showInfo(); throw new Error("后续数据无法按文件编码解码，已停止文本预览。可在系统应用打开完整文件。"); }
      renderContent(); showInfo(); return true;
    } finally { if (request === sequence) { loading = false; controls(); } }
  };
  const loadFile = async (path, { force = false, discardDraft = false } = {}) => {
    if (saving) throw new Error("文件正在保存，请等待保存完成后再切换。");
    if (dirty && !force && !confirm("当前编辑尚未保存，仍要读取其他内容吗？本机草稿会保留。")) return;
    draft(); const request = ++sequence; loading = true; controls();
    try {
      const value = await sdk.call("files.read", { path, space: "native", offset: 0, limit: 262144 }, { signal });
      if (request !== sequence || signal.aborted) return;
      source?.dispose(); clearImage(); file = { ...value, path }; text = ""; image = ""; binary = false; editing = false; dirty = false; conflict = false; readOffset = value.next; main.dataset.markdown = "";
      const buffer = bytes(value.base64), type = imageType(path);
      format = { ...fileEncoding(buffer), lineEnding: "\n" }; decoder = new TextDecoder(format.encoding, { fatal: true });
      if (type && file.size <= IMAGE_LIMIT) {
        const chunks = [buffer];
        while (readOffset < file.size) { const chunk = await readChunk(readOffset, file.revision); if (request !== sequence) return; chunks.push(bytes(chunk.base64)); readOffset = chunk.next; }
        const blob = new Blob(chunks, { type }), bitmap = await createImageBitmap(blob);
        if (request !== sequence || signal.aborted) { bitmap.close(); return; }
        const fits = bitmap.width * bitmap.height <= 16777216; bitmap.close();
        if (!fits) throw new Error("图片超过 1600 万像素，无法在面板预览。");
        imageUrl = URL.createObjectURL(blob); image = type;
      } else {
        try { text = decoder.decode(buffer, { stream: readOffset < file.size }); format.lineEnding = text.includes("\r\n") ? "\r\n" : text.includes("\r") ? "\r" : "\n"; binary = /\u0000/.test(text); } catch { binary = true; }
      }
      if (request !== sequence || signal.aborted) return;
      try {
        const stored = JSON.parse(localStorage.getItem(draftKey()) || "null");
        if (!discardDraft && !image && !binary && stored && typeof stored.text === "string" && stored.text.replace(/\r\n/g, "\n") !== text.replace(/\r\n/g, "\n") && stored.text.length <= EDIT_LIMIT) { text = stored.text; editing = true; dirty = true; conflict = stored.revision !== file.revision; }
      } catch { /* The file remains readable when draft storage is unavailable. */ }
      renderContent(); showInfo(); if (root.clientWidth < 400) tree.hidden = true; remember();
    } catch (error) { if (!signal.aborted && request === sequence) { content.replaceChildren(make("p", "wb-error", error.message)); info.replaceChildren(action("refresh", "重新读取文件", () => loadFile(path))); } }
    finally { if (request === sequence) { loading = false; controls(); } }
  };
  const editFile = async () => {
    if (loading || !file || file.size > EDIT_LIMIT || binary || image) return;
    const request = sequence;
    while (readOffset < file.size) { if (request !== sequence || !await loadMore()) return; }
    editing = true; renderContent(); content.querySelector("textarea")?.focus();
  };
  const saveFile = async () => {
    if (!dirty || loading || !file) return;
    if (conflict) throw new Error("磁盘内容已改变，请先比较或重新读取；编辑草稿仍然保留。");
    const encoded = encodeFileText(text, format);
    if (encoded.length > EDIT_LIMIT) throw new Error("编辑内容超过 1 MiB，请使用外部编辑器保存。");
    loading = true; saving = true; controls();
    try {
      const saved = await sdk.call("files.write", { path: file.path, space: "native", revision: file.revision, base64: fileBase64(encoded) }, { signal });
      if (signal.aborted) return;
      file = { ...file, ...saved }; dirty = false; readOffset = file.size;
      try { localStorage.removeItem(draftKey()); } catch { /* A matching saved draft is ignored on reopen. */ }
      showInfo(); controls(); toast("文件已保存", "success");
    } catch (error) { if (error.status === 412) { conflict = true; draft(); showInfo(); } throw error; }
    finally { loading = false; saving = false; controls(); }
  };
  const compare = async () => {
    const latest = await sdk.call("files.read", { path: file.path, space: "native", limit: 262144 }, { signal });
    const dialog = make("dialog", "wb-file-compare"), columns = make("div", "wb-file-compare-columns");
    for (const [label, value] of [["你的编辑", text], ["磁盘内容（最多 256 KiB）", latest.text || "此范围无法作为完整 UTF-8 文本显示"]]) { const cell = make("section"), area = make("textarea"); area.readOnly = true; area.value = value; cell.append(make("h3", "", label), area); columns.append(cell); }
    dialog.append(columns, action("close", "关闭比较", () => dialog.close())); document.body.append(dialog); dialog.showModal();
    dialog.addEventListener("close", () => dialog.remove(), { once: true }); signal.addEventListener("abort", () => dialog.remove(), { once: true });
  };
  const quoteFile = () => {
    if (!file) return;
    const selected = source?.selection() || (editing && (() => { const input = content.querySelector("textarea"); return input?.selectionEnd > input?.selectionStart ? { text: text.slice(input.selectionStart, input.selectionEnd), start: text.slice(0, input.selectionStart).split("\n").length } : null; })());
    if ((selected?.text.length || 0) > 32768) throw new Error("选中内容超过 32 KiB，请缩小选择范围。");
    addContext({ title: selected ? `${name(file.path)}:${selected.start}${selected.end > selected.start ? `-${selected.end}` : ""}` : name(file.path), source: file.path, context, content: selected?.text || `文件路径：${file.path}` });
  };
  const menus = [popupMenu(more, () => [
    { label: "查找已加载内容", disabled: !file || binary || Boolean(image), execute: showFind },
    { label: "跳转到行…", disabled: !file || binary || Boolean(image), execute: () => { const line = prompt("输入行号", "1"); if (line && /^\d+$/.test(line)) moveTo(line); } },
    { label: "复制文件路径", disabled: !file, execute: () => navigator.clipboard.writeText(file.path) },
    { label: "在目标设备的系统应用打开", disabled: !file, execute: () => sdk.call("system.open", { target: file.path }, { signal }) },
    { label: "显示隐藏文件", checked: hidden, execute: () => { hidden = !hidden; remember(); return refreshTree(); } },
    { label: "打开其他目录…", execute: async () => { const path = prompt("输入要浏览的目录路径", directory); if (!path) return; directory = (await sdk.call("files.resolve", { path, space: "native" }, { signal })).path; expanded.clear(); remember(); return refreshTree(); } },
  ], { signal, onError: failToast })];
  const populate = async (parent, path, reset = true) => {
    const old = reset ? { next: 0, revision: undefined } : parent.page;
    const page = await sdk.call("files.directory", { path, space: "native", offset: old.next, limit: 128, hidden, ...(old.revision ? { revision: old.revision } : {}) }, { signal });
    if (signal.aborted) return;
    if (reset) parent.replaceChildren(); else parent.querySelector(":scope > .wb-tree-more")?.remove(); parent.page = { next: page.next, revision: page.revision };
    for (const item of page.items.sort((a, b) => Number(b.type === 2) - Number(a.type === 2) || a.name.localeCompare(b.name))) {
      const childPath = join(path, item.name), row = make("div", "wb-tree-entry"), button = make("button", "wb-tree-item"), children = make("div", "wb-tree-children");
      button.type = "button"; button.title = childPath; button.append(icon(item.type === 2 ? "folder" : "file"), document.createTextNode(item.name)); row.append(button); button.dataset.path = childPath;
      if (item.type === 2) {
        children.hidden = !expanded.has(childPath); button.setAttribute("aria-expanded", String(!children.hidden)); row.append(children);
        button.onclick = async () => { children.hidden = !children.hidden; button.setAttribute("aria-expanded", String(!children.hidden)); if (children.hidden) expanded.delete(childPath); else expanded.add(childPath); remember(); if (!children.hidden && !children.childElementCount) await populate(children, childPath).catch(failToast); };
        if (!children.hidden) void populate(children, childPath).catch(failToast);
      } else {
        button.onclick = () => void loadFile(childPath).catch(failToast); button.ondblclick = () => void openFile(childPath).catch(failToast);
      }
      parent.append(row);
    }
    if (page.more) { const next = action("plus", "加载更多文件", () => populate(parent, path, false)); next.classList.add("wb-tree-more"); parent.append(next); }
  };
  const refreshTree = async () => {
    searchSequence++; search.value = ""; treeHeading.querySelector("span").textContent = name(directory) || directory;
    try { await populate(treeBody, directory); } catch (error) { if (!signal.aborted) treeBody.replaceChildren(make("p", "wb-error", error.message), action("refresh", "重新读取目录", refreshTree)); }
  };
  search.oninput = () => {
    clearTimeout(searchTimer); const query = search.value.trim(), request = ++searchSequence;
    searchTimer = setTimeout(async () => {
      try {
        if (!query) { await refreshTree(); return; }
        const result = await sdk.call("files.search", { path: directory, space: "native", query, hidden, limit: 100 }, { signal });
        if (signal.aborted || request !== searchSequence) return;
        treeBody.replaceChildren();
        for (const item of result.items) { const path = join(directory, item.path), button = make("button", "wb-tree-item", item.path); button.type = "button"; button.title = path; button.onclick = () => void (item.type === 2 ? (expanded.add(path), refreshTree()) : loadFile(path)).catch(failToast); button.ondblclick = () => { if (item.type !== 2) void openFile(path).catch(failToast); }; treeBody.append(button); }
        if (!result.items.length) treeBody.append(make("p", "", "未找到匹配文件"));
        if (result.incomplete) treeBody.append(make("p", "wb-file-search-note", "搜索尚未覆盖全部目录或达到结果上限，请缩小搜索范围。"));
      } catch (error) { if (!signal.aborted && request === searchSequence) treeBody.replaceChildren(make("p", "wb-error", error.message)); }
    }, 250);
  };
  const watch = async () => {
    try {
      if (file && !root.hidden && !loading) {
        const metadata = await sdk.call("files.stat", { path: file.path, space: "native" }, { signal });
        if (metadata.revision !== file.revision) { if (dirty) { conflict = true; showInfo(); controls(); } else await loadFile(file.path, { force: true }); }
      }
    } catch (error) { if (!signal.aborted) { info.textContent = `文件状态暂不可用：${error.message}`; } }
    finally { if (!signal.aborted) watchTimer = setTimeout(watch, 2500); }
  };
  signal.addEventListener("abort", () => { clearTimeout(watchTimer); clearTimeout(searchTimer); clearTimeout(saveTimer); draft(); clearImage(); source?.dispose(); sizing.disconnect(); menus.forEach(menu => menu.dispose()); }, { once: true });
  controls(); renderContent(); await refreshTree();
  if (params.path || saved.selected) await loadFile(params.path || saved.selected, { force: true });
  if (params.edit && !editing && !signal.aborted) await editFile();
  if (!signal.aborted) void watch();
}
