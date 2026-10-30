import { currentLocale, setUiAttribute as writeUiAttribute, uiMessage } from "../../i18n.js";

import { element as make, iconButton } from "./controls.js";
import { renderDiffRows, diffLabels, diffProvenance } from "./diff-render.js";
import { changeStats } from "./changes-model.js";

// Five complete sections at a time, each with one <=300-line/512-KiB page.
// Replacing the batch/page bounds DOM and cached text even in huge repositories.
export function mountContinuousChanges(root, { state, scope, range, selected, split, read, openFile, addContext, context, onSelect, onError, signal }) {
  let generation = 0, controller, current = 0, stopped = false, reads = Promise.resolve();
  const pager = make("nav", "wb-continuous-pager"), sections = make("div", "wb-continuous-sections");
  root.dataset.split = String(split); root.replaceChildren(pager, sections);
  const action = (glyph, label, run) => iconButton(glyph, label, run, error => { if (!stopped && error.name !== "AbortError") onError(error); });
  const choose = path => { selected = path; onSelect(path); };
  const group = async at => {
    controller?.abort(); controller = new AbortController(); const request = ++generation, abort = controller.signal;
    current = Math.max(0, Math.min(Math.floor(at / 5) * 5, Math.floor((state.files.length - 1) / 5) * 5));
    sections.replaceChildren(); root.scrollTop = 0;
    const previous = action("arrow-left", uiMessage("ui.continuousChanges.previousFileGroup", {}, "上一组文件"), () => group(current - 5)), next = action("arrow-right", uiMessage("ui.continuousChanges.nextFileGroup", {}, "下一组文件"), () => group(current + 5));
    previous.disabled = !current; next.disabled = current + 5 >= state.files.length;
    pager.replaceChildren(previous, make("span", "", uiMessage("ui.continuousChanges.filesValue1Value2Value3EachFileIsPagedSeparately", { value1: current + 1, value2: Math.min(current + 5, state.files.length), value3: state.files.length }, "文件 {value1}–{value2} / {value3} · 每个文件独立分页")), next);
    const live = () => !stopped && !abort.aborted && request === generation;
    const blocks = state.files.slice(current, current + 5).map(file => {
      const section = make("section", "wb-continuous-file"), header = make("header", "wb-change-filebar"), title = make("span", "wb-file-path", file.path);
      section.dataset.path = file.path; writeUiAttribute(section, "aria-label", uiMessage("ui.continuousChanges.fileDiffValue1", { value1: file.path }, "文件差异：{value1}")); section.tabIndex = -1; writeUiAttribute(title, "title", file.old_path ? `${file.old_path} → ${file.path}` : file.path);
      const body = make("div", "wb-continuous-diff"), footer = make("div", "wb-file-info");
      let page, selection, busy = false; const previousOffsets = [];
      const quote = action("review", uiMessage("ui.continuousChanges.quoteDiffValue1", { value1: file.path }, "引用差异：{value1}"), () => {
        if (!page) return;
        const rows = page.rows.filter(row => !selection || row.offset >= selection[0] && row.offset <= selection[1]);
        const text = document.getSelection(), content = text?.toString() && body.contains(text.anchorNode) && body.contains(text.focusNode) ? text.toString() : rows.map(row => row.text ?? `[Base64: ${row.base64}]`).join("\n");
        addContext({ title: `${file.path} · ${range}`, content: `${diffProvenance(state, file.path, page, rows, range)}\n\n${content}`, source: `git:${state.repository}/${file.path}`, context });
      }); quote.onpointerdown = event => event.preventDefault(); quote.disabled = true;
      const open = action("file", uiMessage("ui.continuousChanges.openWorkspaceFileValue1", { value1: file.path }, "打开工作区文件：{value1}"), () => {
        choose(file.path); const row = page?.rows.find(row => row.new_line && (!selection || row.offset >= selection[0]));
        return openFile(`${state.repository.replace(/[\\/]$/, "")}/${file.path}`, row?.new_line || 1);
      }); open.disabled = Boolean(file.directory) || file.status.includes("D") && !file.worktree_recreated;
      header.append(title, make("span", "wb-change-stats", changeStats(file)), open, quote); section.append(header, body, footer); sections.append(section);
      const select = (row, extend) => {
        choose(file.path); selection = extend && selection ? [Math.min(selection[0], row.offset), Math.max(selection[1], row.offset)] : [row.offset, row.offset];
        if (row.kind === "hunk") { const at = page.rows.indexOf(row), next = page.rows.slice(at + 1).find(row => row.kind === "hunk"); selection = [row.offset, next ? next.offset - 1 : page.rows.at(-1).offset]; }
        for (const node of body.querySelectorAll("[data-offset]")) node.classList.toggle("wb-change-selected", Number(node.dataset.offset) >= selection[0] && Number(node.dataset.offset) <= selection[1]);
      };
      const load = async (offset = 0) => {
        if (busy || !live()) return; busy = true; selection = null; quote.disabled = true; footer.replaceChildren(); body.replaceChildren(make("p", "wb-change-message", uiMessage("ui.continuousChanges.readingFileDiff", {}, "正在读取文件差异…")));
        try {
          const value = await (reads = reads.catch(() => {}).then(() => { if (!live()) throw new DOMException("Cancelled", "AbortError"); return read(file.path, offset, abort); }));
          if (!live()) return; page = value; body.replaceChildren();
          if (split) { const labels = make("div", "wb-change-labels"); diffLabels(scope).forEach(label => labels.append(make("span", "", label))); body.append(labels); }
          renderDiffRows(body, page, { split, path: file.path, select }); quote.disabled = !page.rows.length;
          if (!page.rows.length) body.append(make("p", "wb-change-message", uiMessage("ui.continuousChanges.noTextDifferencesInThisScopeOnlyFileModes", {}, "此范围没有文本差异，可能只有模式变化。")));
          if (page.binary) body.append(make("p", "wb-change-message", uiMessage("ui.continuousChanges.binaryFilesHaveNoTextDiff", {}, "二进制文件无法显示文本差异")));
          const back = action("arrow-left", uiMessage("ui.continuousChanges.previousDiffPageValue1", { value1: file.path }, "上一页差异：{value1}"), () => { choose(file.path); return load(previousOffsets.pop()); }); back.disabled = !previousOffsets.length;
          const next = action("arrow-right", uiMessage("ui.continuousChanges.nextDiffPageValue1", { value1: file.path }, "下一页差异：{value1}"), () => { choose(file.path); previousOffsets.push(page.offset); return load(page.next); }); next.disabled = !page.more;
          footer.replaceChildren(back, make("span", "", uiMessage("ui.continuousChanges.diffLinesValue1Value2Value3", { value1: page.total ? page.offset + 1 : 0, value2: page.next, value3: page.total.toLocaleString(currentLocale()) }, "差异行 {value1}–{value2} / {value3}")), next);
        } catch (error) { if (live() && error.name !== "AbortError") onError(error); }
        finally { busy = false; }
      };
      return { path: file.path, section, load };
    });
    // Serial admission keeps full-repository metadata work bounded to one read.
    for (const block of blocks) { if (!live()) return; await block.load(); }
    if (live()) { const block = blocks.find(block => block.path === selected); block?.section.scrollIntoView({ block: "start" }); }
  };
  const select = path => {
    const index = state.files.findIndex(file => file.path === path); if (index < 0) return;
    selected = path;
    const section = [...sections.children].find(node => node.dataset.path === path);
    if (section) section.scrollIntoView({ block: "start" }); else void group(index);
  };
  const dispose = () => { stopped = true; controller?.abort(); signal.removeEventListener("abort", dispose); };
  signal.addEventListener("abort", dispose, { once: true });
  void group(Math.max(0, state.files.findIndex(file => file.path === selected)));
  return { select, dispose };
}
