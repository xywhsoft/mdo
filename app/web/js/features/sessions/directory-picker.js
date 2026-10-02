import { api } from "../../api/client.js";
import { element, errorMessage, isImeKey } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";

export function directoryChild(data, name) {
  return `${data.path}${data.path.endsWith(data.separator) ? "" : data.separator}${name}`;
}

export function createDirectoryPicker({ dialog,
  read = (path, signal) => api.get(`/workspace/directories?path=${encodeURIComponent(path)}`,
    { signal }).then((result) => result.data) }) {
  const find = (id) => dialog.querySelector(`#directory-${id}`);
  const form = find("form"), path = find("path"), list = find("list");
  const status = find("status"), parent = find("parent"), choose = find("choose");
  const shortcuts = find("shortcuts");
  let generation = 0, controller = null, current = null, select = null;
  let locations = [{ kind: "cwd", path: "" }];
  let loading = false, failure = "";

  function renderStatus() {
    status.textContent = failure || (loading ? t("directory.loading", {}, "正在读取目录…") :
      current?.truncated ? t("directory.truncated", {}, "部分目录未列出，可输入完整路径进入。") :
        current && !current.directories.length ? t("directory.empty", {}, "此目录没有子目录。") : "");
    status.classList.toggle("dialog-error-text", Boolean(failure));
    list.setAttribute("aria-busy", String(loading));
    choose.disabled = loading || !current || path.value !== current.path;
    parent.disabled = loading || !current?.parent;
  }

  function renderList() {
    shortcuts.replaceChildren(...locations.map((item) => {
      const button = element("button", { className: "secondary-button", attrs: { type: "button",
        title: item.path }, text: item.kind === "home" ? t("directory.home", {}, "用户目录") :
        t("directory.cwd", {}, "启动目录") });
      button.addEventListener("click", () => { void load(item.path, true); });
      return button;
    }));
    list.replaceChildren(...(current?.directories ?? []).slice().sort((a, b) =>
      a.localeCompare(b, undefined, { numeric: true })).map((name) => {
      const button = element("button", { className: "directory-item", attrs: { type: "button" } }, [
        element("span", { text: "▸", attrs: { "aria-hidden": "true" } }),
        element("span", { text: name }),
      ]);
      button.addEventListener("click", () => { void load(directoryChild(current, name), true); });
      return button;
    }));
    list.scrollTop = 0;
  }

  async function load(next, focusList = false) {
    const version = ++generation;
    controller?.abort();
    controller = new AbortController();
    path.value = next;
    current = null; loading = true; failure = "";
    renderList(); renderStatus();
    try {
      const data = await read(next, controller.signal);
      if (version !== generation || !dialog.open) return;
      current = data; locations = data.shortcuts; path.value = data.path; loading = false;
      renderList(); renderStatus();
      if (focusList) (list.querySelector("button") ?? choose).focus();
    } catch (cause) {
      if (version !== generation || !dialog.open) return;
      loading = false; failure = errorMessage(cause); renderStatus();
      path.focus();
    }
  }

  form.addEventListener("submit", (event) => {
    event.preventDefault();
    void load(path.value, true);
  });
  path.addEventListener("input", renderStatus);
  path.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && isImeKey(event)) event.preventDefault();
  });
  parent.addEventListener("click", () => { if (current?.parent) void load(current.parent, true); });
  choose.addEventListener("click", () => {
    if (loading || !current || path.value !== current.path) return;
    const selected = current.path, callback = select;
    dialog.close();
    callback?.(selected);
  });
  for (const id of ["close", "cancel"]) find(id).addEventListener("click", () => dialog.close());
  dialog.addEventListener("close", () => {
    // Native close events can be queued behind a fresh open().
    if (dialog.open) return;
    ++generation; controller?.abort(); current = null; select = null;
  });
  const unsubscribe = subscribeLocale(() => {
    if (dialog.open) { renderList(); renderStatus(); }
  });

  return Object.freeze({
    open(initialPath, onSelect) {
      select = onSelect; path.value = initialPath ?? "";
      if (!dialog.open) dialog.showModal();
      path.focus();
      return load(path.value);
    },
    close() {
      ++generation; controller?.abort(); select = null;
      if (dialog.open) dialog.close();
    },
    destroy() { this.close(); unsubscribe(); },
  });
}
