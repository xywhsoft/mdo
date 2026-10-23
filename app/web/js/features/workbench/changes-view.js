import { element as make, icon, iconButton, popupMenu } from "./controls.js";
import { changesTree, changeStats } from "./changes-model.js";
import { renderDiffRows, diffLabels, diffProvenance } from "./diff-render.js";
import { mountContinuousChanges } from "./continuous-changes.js";
import { readOperation } from "./read-operation.js";
import { toast } from "../../utils/dom.js";

export async function mountChangesView(root, { sdk, context, params = {}, viewState = {}, signal, update, addContext, openFile }) {
  root.classList.add("wb-changes-view");
  const saved = viewState.changes || {}, collapsed = new Set(saved.collapsed || []);
  const treeId = `changes-${crypto.randomUUID()}`;
  let repository = saved.repository || params.repository || ".", state, selected = saved.selected || "", page, split = Boolean(saved.split), treeWidth = Math.max(150, Math.min(360, saved.treeWidth || 200));
  let operation, generation = 0, saveTimer, treeRows = [], focused = 0, selectedRows = null, previousPages = [], targetOffset = 0, continuous = Boolean(saved.continuous), multi;
  const bar = make("div", "wb-changes-toolbar"), scope = make("select", "wb-compare-scope"), base = make("select", "wb-compare-base"), summary = make("span", "wb-changes-summary");
  scope.setAttribute("aria-label", "变更比较范围"); base.setAttribute("aria-label", "比较基准分支"); base.hidden = true;
  for (const [value, label] of [["workspace", "工作区"], ["staged", "已暂存"], ["unstaged", "未暂存"], ["branch", "分支 → HEAD"], ["session", "本会话"]]) {
    const option = make("option", "", label); option.value = value; scope.append(option);
  }
  scope.value = ["workspace", "staged", "unstaged", "branch", "session"].includes(saved.scope) ? saved.scope : "workspace";
  const errorToast = error => { if (error.name !== "AbortError" && !signal.aborted) toast(error.message, "error"); };
  const action = (glyph, label, run) => iconButton(glyph, label, run, errorToast);
  const refresh = action("refresh", "刷新变更", () => refreshChanges()), toggleTree = action("folder", "切换变更文件树", () => { tree.hidden = !tree.hidden; remember(); });
  const more = action("more", "变更更多操作", () => {});
  bar.append(scope, base, summary, refresh, toggleTree, more);
  const body = make("div", "wb-files-body"), main = make("div", "wb-file-main"), fileBar = make("div", "wb-change-filebar"), fileName = make("span", "wb-file-path"), fileStats = make("span", "wb-change-stats");
  const diff = make("div", "wb-changes-diff"), info = make("div", "wb-file-info"), tree = make("aside", "wb-file-tree wb-changes-tree"), search = make("input", "wb-file-search");
  const treeList = make("div", "wb-changes-tree-list"), treeCanvas = make("div", "wb-changes-tree-canvas"), treeTitle = make("div", "wb-tree-heading");
  search.placeholder = "筛选变更文件…"; search.setAttribute("aria-label", "筛选变更文件");
  treeList.setAttribute("role", "tree"); treeList.setAttribute("aria-label", "变更文件"); treeList.tabIndex = 0; treeList.append(treeCanvas);
  const open = action("file", "打开工作区文件", () => {
    const file = state?.files.find(file => file.path === selected); if (!file || file.directory) return;
    const chosen = page?.rows.find(row => row.new_line && (!selectedRows || row.offset >= selectedRows[0]));
    return openFile(`${state.repository.replace(/[\\/]$/, "")}/${file.path}`, chosen?.new_line || 1);
  });
  const quote = action("review", "引用所选差异", () => quoteChanges()); quote.onpointerdown = event => event.preventDefault();
  const previous = action("arrow-left", "上一页差异", () => { if (previousPages.length) return loadDiff(previousPages.pop(), false); });
  const next = action("arrow-right", "下一页差异", () => { if (page?.more) { previousPages.push(page.offset); return loadDiff(page.next, false); } });
  fileBar.append(fileName, fileStats, open, quote); main.append(fileBar, diff, info);
  treeTitle.append(make("span", "", "变更文件")); tree.append(search, treeTitle, treeList); tree.style.width = `${treeWidth}px`; tree.hidden = Boolean(saved.treeHidden);
  body.append(main, tree); root.append(bar, body);
  const remember = () => {
    clearTimeout(saveTimer); saveTimer = setTimeout(() => {
      if (!signal.aborted) void update({ view_state: { ...viewState, changes: { repository, scope: scope.value, base: base.value, selected, split, continuous, treeHidden: tree.hidden, treeWidth, collapsed: [...collapsed].slice(0, 256) } } }).catch(errorToast);
    }, 350);
  };
  const comparison = () => ({ repository, scope: scope.value, ...(scope.value === "branch" ? { base: base.value } : {}) });
  const cancel = () => { operation?.abort(); operation = null; multi?.dispose(); multi = null; };
  const task = () => {
    cancel(); const controller = new AbortController(); operation = controller;
    return controller;
  };
  const fail = (error, retry) => {
    if (signal.aborted || error.name === "AbortError") return;
    const explanation = error.code === "git_baseline_missing" ? "这个会话没有已捕获的比较基线。请选择工作区或分支比较。" : error.code === "git_baseline_unavailable" ? `会话开始时未能捕获完整基线，不能回溯补建。请选择工作区或分支比较。${error.message}` : error.code === "git_baseline_context_changed" ? "该会话的原始仓库或工作目录已变化，不能沿用旧基线。请选择工作区或分支比较。" : error.message;
    multi?.dispose(); multi = null; page = null; diff.replaceChildren(make("p", "wb-error wb-change-message", explanation));
    info.replaceChildren(action("refresh", "重新读取变更", retry)); controls();
  };
  const controls = () => {
    const file = state?.files.find(file => file.path === selected);
    fileBar.hidden = continuous; fileName.textContent = file ? file.path : "变更"; fileName.title = file?.old_path ? `${file.old_path} → ${file.path}` : selected;
    fileStats.textContent = file ? changeStats(file) : "";
    open.disabled = !file || Boolean(file.directory) || file.status.includes("D") && !file.worktree_recreated;
    quote.disabled = !page?.rows.length; previous.disabled = !previousPages.length; next.disabled = !page?.more;
    summary.textContent = state ? `${state.files.length} 文件 · +${state.additions} −${state.deletions}${state.unmeasured_files ? ` · ${state.unmeasured_files} 未统计` : ""}` : "";
    summary.title = state ? `${state.repository}\n${state.branch || "分离 HEAD"}${state.unborn ? " · 尚无首次提交" : ""}${scope.value === "branch" ? `\n${state.base} → ${state.head}（提交之间）` : ""}` : "";
    if (state?.scope === "session") summary.title += `\n基线：${new Date(state.baseline_captured_at_us / 1000).toLocaleString()}\n原始工作区字节；显示会话启动后的净变化，也包含其他程序的修改。`;
  };
  const selectRow = (row, extend) => {
    selectedRows = extend && selectedRows ? [Math.min(selectedRows[0], row.offset), Math.max(selectedRows[0], row.offset)] : [row.offset, row.offset];
    if (row.kind === "hunk") {
      const at = page.rows.indexOf(row), end = page.rows.slice(at + 1).findIndex(item => item.kind === "hunk");
      selectedRows = [row.offset, end < 0 ? page.rows.at(-1).offset : page.rows[at + end].offset];
    }
    for (const node of diff.querySelectorAll("[data-offset]")) node.classList.toggle("wb-change-selected", Number(node.dataset.offset) >= selectedRows[0] && Number(node.dataset.offset) <= selectedRows[1]);
  };
  const renderDiff = () => {
    const scrollTop = diff.scrollTop; diff.replaceChildren(); diff.dataset.split = String(split);
    if (!page) return;
    if (split) {
      const labels = make("div", "wb-change-labels"); diffLabels(scope.value).forEach(label => labels.append(make("span", "", label))); diff.append(labels);
    }
    if (!page.rows.length) diff.append(make("p", "wb-change-message", "此范围没有可显示的文本差异（可能只有文件模式或索引状态变化）。"));
    renderDiffRows(diff, page, { split, path: selected, select: selectRow });
    diff.scrollTop = scrollTop;
    info.replaceChildren(previous, make("span", "", `差异行 ${page.total ? page.offset + 1 : 0}–${page.next} / ${page.total.toLocaleString()} · +${page.additions} −${page.deletions}`), next);
    if (page.binary) info.append(make("span", "", "二进制文件无法显示文本差异"));
    if (state.files.find(file => file.path === selected)?.worktree_recreated) info.append(make("span", "", "暂存删除后在原路径重建；显示 HEAD → 当前文件"));
    if (scope.value === "branch") info.append(make("span", "", "选定提交 → HEAD；打开文件显示当前工作区内容"));
    controls();
  };
  const quoteChanges = () => {
    if (!page) return;
    const selection = document.getSelection(), selectedText = selection?.toString();
    const rows = page.rows.filter(row => !selectedRows || row.offset >= selectedRows[0] && row.offset <= selectedRows[1]);
    const text = selectedText && diff.contains(selection.anchorNode) && diff.contains(selection.focusNode) ? selectedText : rows.map(row => row.text ?? `[Base64: ${row.base64}]`).join("\n");
    const range = scope.options[scope.selectedIndex].text;
    const provenance = diffProvenance(state, selected, page, rows, range);
    addContext({ title: `${selected} · ${range}`, content: `${provenance}\n\n${text}`, source: `git:${state.repository}/${selected}`, context });
  };
  const showContinuous = () => {
    cancel(); page = null; info.replaceChildren(); controls(); if (!state?.files.length) return;
    const snapshot = state, parameters = comparison();
    multi = mountContinuousChanges(diff, { state: snapshot, scope: scope.value, range: scope.options[scope.selectedIndex].text,
      selected, split, context, signal, openFile, addContext,
      read: (path, offset, signal) => readOperation(sdk, "changes.diff", { ...parameters, path, revision: snapshot.revision, offset, limit: 300 }, signal),
      onSelect(path) { selected = path; renderTreeRows(); remember(); }, onError: error => fail(error, refreshChanges),
    });
  };
  async function loadDiff(offset = 0, reset = true) {
    if (!state || !selected) return;
    if (continuous) { if (multi) multi.select(selected); else showContinuous(); remember(); return; }
    const controller = task(), request = ++generation, path = selected;
    targetOffset = offset; if (reset) previousPages = []; selectedRows = null; page = null; controls();
    diff.replaceChildren(make("p", "wb-change-message", "正在读取文件差异…")); info.replaceChildren(); renderTreeRows(); remember();
    try {
      const result = await readOperation(sdk, "changes.diff", { ...comparison(), path, revision: state.revision, offset, limit: 300 }, controller.signal);
      if (signal.aborted || request !== generation) return;
      page = result; diff.scrollTop = 0; renderDiff();
    } catch (error) { if (request === generation) fail(error, refreshChanges); }
  }
  const renderTreeRows = () => {
    const first = Math.max(0, Math.floor(treeList.scrollTop / 28) - 8), last = Math.min(treeRows.length, first + Math.ceil((treeList.clientHeight || 600) / 28) + 16);
    treeCanvas.style.height = `${treeRows.length * 28}px`; treeCanvas.replaceChildren();
    for (let at = first; at < last; at++) {
      const item = treeRows[at], button = make("button", "wb-tree-item wb-change-tree-item"), label = make("span", "wb-change-tree-name", item.name); button.type = "button";
      button.style.top = `${at * 28}px`; button.style.paddingLeft = `${8 + item.depth * 12}px`; button.id = `${treeId}-${at}`;
      button.tabIndex = -1; button.setAttribute("role", "treeitem"); button.setAttribute("aria-level", String(item.depth + 1)); button.setAttribute("aria-selected", String(item.path === selected));
      if (item.directory) button.setAttribute("aria-expanded", String(!collapsed.has(item.path) || Boolean(search.value.trim())));
      button.append(icon(item.directory ? "folder" : "file"), label);
      if (item.file) { const stats = make("span", "wb-change-tree-stats", changeStats(item.file)); button.append(stats); button.title = `${item.path}\n${item.file.status} · ${stats.textContent}${item.file.old_path ? `\n原路径：${item.file.old_path}` : ""}`; }
      else button.title = item.path;
      button.onclick = () => { focused = at; treeList.focus(); activate(item); };
      button.ondblclick = () => { if (item.file && !item.file.directory && (!item.file.status.includes("D") || item.file.worktree_recreated)) void openFile(`${state.repository}/${item.path}`, 1).catch(errorToast); };
      treeCanvas.append(button); if (at === focused) treeList.setAttribute("aria-activedescendant", button.id);
    }
  };
  const filterTree = () => { treeRows = changesTree(state?.files || [], collapsed, search.value); focused = Math.min(focused, Math.max(0, treeRows.length - 1)); treeList.removeAttribute("aria-activedescendant"); renderTreeRows(); };
  const activate = item => {
    if (item.directory) { if (collapsed.has(item.path)) collapsed.delete(item.path); else collapsed.add(item.path); filterTree(); remember(); }
    else { selected = item.path; if (root.clientWidth < 400) tree.hidden = true; void loadDiff().catch(errorToast); }
  };
  treeList.onscroll = renderTreeRows; search.oninput = () => { treeList.scrollTop = 0; filterTree(); };
  treeList.onkeydown = event => {
    if (["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) {
      event.preventDefault(); focused = event.key === "Home" ? 0 : event.key === "End" ? treeRows.length - 1 : Math.max(0, Math.min(treeRows.length - 1, focused + (event.key === "ArrowDown" ? 1 : -1)));
      if (focused * 28 < treeList.scrollTop) treeList.scrollTop = focused * 28;
      else if ((focused + 1) * 28 > treeList.scrollTop + treeList.clientHeight) treeList.scrollTop = (focused + 1) * 28 - treeList.clientHeight;
      renderTreeRows();
    } else if (["Enter", " ", "ArrowRight", "ArrowLeft"].includes(event.key) && treeRows[focused]) {
      event.preventDefault(); const item = treeRows[focused];
      if (event.key === "ArrowLeft" && item.directory) { collapsed.add(item.path); filterTree(); remember(); }
      else if (event.key === "ArrowRight" && item.directory) { collapsed.delete(item.path); filterTree(); remember(); }
      else if (event.key === "Enter" || event.key === " ") activate(item);
    }
  };
  const separator = make("div", "wb-file-tree-resize"); separator.tabIndex = 0; separator.setAttribute("role", "separator"); separator.setAttribute("aria-label", "调整变更文件树宽度"); separator.setAttribute("aria-orientation", "vertical"); tree.prepend(separator);
  const setWidth = width => { treeWidth = Math.max(150, Math.min(360, Math.max(150, root.clientWidth - 160), width)); tree.style.width = `${treeWidth}px`; };
  let pointerX = 0, pointerWidth = 0;
  separator.onpointerdown = event => { pointerX = event.clientX; pointerWidth = treeWidth; separator.setPointerCapture(event.pointerId); event.preventDefault(); };
  separator.onpointermove = event => { if (separator.hasPointerCapture(event.pointerId)) setWidth(pointerWidth + pointerX - event.clientX); };
  separator.onpointerup = event => { if (separator.hasPointerCapture(event.pointerId)) { separator.releasePointerCapture(event.pointerId); remember(); } };
  separator.onkeydown = event => { if (["ArrowLeft", "ArrowRight"].includes(event.key)) { event.preventDefault(); setWidth(treeWidth + (event.key === "ArrowLeft" ? 20 : -20)); remember(); } };
  async function refreshChanges() {
    const controller = task(), request = ++generation; page = null; state = null; controls(); filterTree(); diff.replaceChildren(make("p", "wb-change-message", "正在读取项目变更…"));
    base.hidden = scope.value !== "branch";
    try {
      if (scope.value === "branch") {
        const old = base.value || saved.base, refs = await readOperation(sdk, "changes.branches", { repository }, controller.signal);
        if (request !== generation || signal.aborted) return;
        base.replaceChildren(); for (const ref of refs) { const option = make("option", "", ref.name); option.value = ref.ref; base.append(option); }
        base.value = refs.some(ref => ref.ref === old) ? old : refs.find(ref => ref.ref.startsWith("refs/remotes/") && !ref.ref.endsWith("/HEAD"))?.ref || refs[0]?.ref || "";
        if (!base.value) throw new Error("当前仓库没有可比较的分支或提交。");
      }
      const result = await readOperation(sdk, "changes.list", comparison(), controller.signal);
      if (request !== generation || signal.aborted) return;
      state = result; repository = result.repository; controls(); filterTree();
      if (!state.files.length) { selected = ""; diff.replaceChildren(make("p", "wb-change-message", "此比较范围没有变更。")); info.replaceChildren(); controls(); remember(); return; }
      if (!state.files.some(file => file.path === selected)) selected = state.files[0].path;
      await loadDiff();
    } catch (error) { if (request === generation) fail(error, refreshChanges); }
  }
  scope.onchange = () => void refreshChanges().catch(errorToast); base.onchange = () => void refreshChanges().catch(errorToast);
  const menu = popupMenu(more, () => [
    { label: "连续浏览文件", icon: "file", checked: continuous, execute: () => { continuous = !continuous; if (continuous) showContinuous(); else void loadDiff(); remember(); } },
    { label: "左右对照", icon: "split", checked: split, execute: () => { split = !split; if (continuous) showContinuous(); else renderDiff(); remember(); } },
    { label: "跳到差异行…", icon: "search", disabled: !page?.total, execute: () => {
      const value = prompt(`输入差异行（1–${page.total}，包括文件头）`, String(targetOffset + 1)); if (value === null) return;
      const line = Number(value); if (!Number.isSafeInteger(line) || line < 1 || line > page.total) throw new Error("请输入范围内的整数行号。"); return loadDiff(line - 1);
    } },
    { label: "选择仓库目录…", icon: "folder", execute: () => {
      const value = prompt("仓库目录（所属设备）", repository); if (!value?.trim()) return; repository = value.trim(); selected = ""; return refreshChanges();
    } },
  ], { signal, onError: errorToast });
  let previousWidth = 0;
  const observer = new ResizeObserver(() => { const width = root.clientWidth; if (!width) return; renderTreeRows(); if (width < 400 && width !== previousWidth && selected) tree.hidden = true; previousWidth = width; }); observer.observe(root); observer.observe(treeList);
  const abort = () => { cancel(); observer.disconnect(); clearTimeout(saveTimer); menu.dispose(); }; signal.addEventListener("abort", abort, { once: true });
  await refreshChanges(); return abort;
}
