// dom.js — 极小 DOM 帮手（ui.js / chrome.js 共用）
//
// XSS 纪律：字符串子节点一律转 textNode，文本永不进 innerHTML；
// 唯一例外是 props.html —— 仅用于 markdown 渲染器的受控产物。

export function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(props ?? {})) {
    if (v == null) continue;
    if (k === 'class') node.className = v;
    else if (k === 'html') node.innerHTML = v;           // 仅 markdown 渲染器产物
    else if (k.startsWith('on') && typeof v === 'function') node.addEventListener(k.slice(2), v);
    else if (k === 'dataset') Object.assign(node.dataset, v);
    else node.setAttribute(k, v);
  }
  for (const c of children.flat()) {
    if (c == null || c === false) continue;
    node.append(typeof c === 'string' ? document.createTextNode(c) : c);
  }
  return node;
}
