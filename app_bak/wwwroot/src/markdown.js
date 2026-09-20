// markdown.js — 安全 Markdown 渲染器（无依赖、无框架）
//
// XSS 纪律：所有文本先整体转义，渲染器产出的标签是唯一 HTML 来源；
// 链接/图片仅允许 http(s) 与 data:image/，代码块内容不参与行内解析。

function esc(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

const SAFE_URL = /^(https?:\/\/|data:image\/(png|jpe?g|gif|webp|svg\+xml);base64,)/i;

/** 行内格式：在已转义文本上做 `code`、**粗**、*斜*、~~删除线~~、[链接]、![图片]。 */
function inline(text) {
  const codes = [];
  let t = text.replace(/`([^`\n]+)`/g, (_, c) => {
    codes.push(c);
    return '\u0000' + (codes.length - 1) + '\u0000';
  });
  t = t.replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>');
  t = t.replace(/(^|[^*])\*([^*\n]+)\*(?!\*)/g, '$1<em>$2</em>');
  t = t.replace(/~~([^~\n]+)~~/g, '<del>$1</del>');
  // 图片 ![alt](url) —— 白名单校验后输出；点击放大走事件委托
  t = t.replace(/!\[([^\]\n]*)\]\(([^)\s]+)\)/g, (m, alt, url) =>
    SAFE_URL.test(url) ? `<img class="md-img" src="${url}" alt="${alt}" loading="lazy" data-zoom>` : m);
  t = t.replace(/\[([^\]\n]+)\]\((https?:\/\/[^)\s]+)\)/g,
    '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>');
  t = t.replace(/\u0000(\d+)\u0000/g, (_, i) => `<code>${codes[+i]}</code>`);
  return t;
}

/** 块级解析：逐行状态机（fence / 标题 / 引用 / 列表(含任务项) / 表格 / 分割线 / 段落）。 */
export function renderMarkdown(src) {
  const lines = String(src ?? '').replace(/\r\n?/g, '\n').split('\n');
  const out = [];
  let i = 0;
  let codeSeq = 0;

  const flushList = (items, ordered) => {
    if (!items.length) return;
    // 任务列表项：以 [ ] / [x] 开头 → 勾选样式（dsh markdown checkbox 语义）
    const tasks = items.filter((x) => /^\[[ xX]\]\s/.test(x));
    if (tasks.length === items.length) {
      out.push('<ul class="task-list">' + items.map((x) => {
        const done = /^\[[xX]\]/.test(x);
        const body = x.replace(/^\[[ xX]\]\s/, '');
        return `<li class="task${done ? ' done' : ''}"><span class="task-box">${done ? '☑' : '☐'}</span><span>${inline(body)}</span></li>`;
      }).join('') + '</ul>');
      return;
    }
    const tag = ordered ? 'ol' : 'ul';
    out.push(`<${tag}>` + items.map((x) => `<li>${inline(x)}</li>`).join('') + `</${tag}>`);
  };

  while (i < lines.length) {
    const line = lines[i];

    // 围栏代码块
    const fence = line.match(/^```([A-Za-z0-9_+-]*)\s*$/);
    if (fence) {
      const lang = fence[1] || 'text';
      const buf = [];
      i++;
      while (i < lines.length && !/^```\s*$/.test(lines[i])) buf.push(lines[i++]);
      i++; // 吃掉收尾 ```
      const id = 'cb' + (++codeSeq);
      out.push(
        `<div class="codeblock"><div class="codeblock-head"><span class="codeblock-lang">${esc(lang)}</span>` +
        `<button class="copy-btn" data-copy-id="${id}" type="button">复制</button></div>` +
        `<pre id="${id}">${esc(buf.join('\n'))}</pre></div>`
      );
      continue;
    }

    if (/^\s*$/.test(line)) { i++; continue; }

    // 标题（h1-h4）
    const h = line.match(/^(#{1,4})\s+(.*)$/);
    if (h) { out.push(`<h${h[1].length}>${inline(esc(h[2]))}</h${h[1].length}>`); i++; continue; }

    // 分割线
    if (/^\s*(-{3,}|\*{3,})\s*$/.test(line)) { out.push('<hr>'); i++; continue; }

    // 引用块
    if (/^>\s?/.test(line)) {
      const buf = [];
      while (i < lines.length && /^>\s?/.test(lines[i])) buf.push(lines[i++].replace(/^>\s?/, ''));
      out.push(`<blockquote>${inline(esc(buf.join(' ')))}</blockquote>`);
      continue;
    }

    // 表格（| a | b | 隔行 |---|---|）
    if (/^\s*\|.*\|\s*$/.test(line) && i + 1 < lines.length && /^\s*\|[\s:|-]+\|\s*$/.test(lines[i + 1])) {
      const cells = (l) => l.trim().replace(/^\||\|$/g, '').split('|').map((c) => c.trim());
      const head = cells(lines[i]); i += 2;
      const rows = [];
      while (i < lines.length && /^\s*\|.*\|\s*$/.test(lines[i])) rows.push(cells(lines[i++]));
      out.push('<div class="table-scroll"><table><thead><tr>' + head.map((c) => `<th>${inline(esc(c))}</th>`).join('') + '</tr></thead><tbody>'
        + rows.map((r) => '<tr>' + r.map((c) => `<td>${inline(esc(c))}</td>`).join('') + '</tr>').join('') + '</tbody></table></div>');
      continue;
    }

    // 无序 / 有序列表（项可带 [ ] 任务前缀）
    const ul = line.match(/^\s*[-*]\s+(.*)$/);
    const ol = line.match(/^\s*\d+[.)]\s+(.*)$/);
    if (ul || ol) {
      const ordered = !!ol;
      const items = [];
      while (i < lines.length) {
        const m = ordered ? lines[i].match(/^\s*\d+[.)]\s+(.*)$/) : lines[i].match(/^\s*[-*]\s+(.*)$/);
        if (!m) break;
        items.push(esc(m[1])); i++;
      }
      flushList(items, ordered);
      continue;
    }

    // 段落：连续非空非块行合并
    const buf = [line];
    i++;
    while (i < lines.length && !/^\s*$/.test(lines[i]) && !/^(#{1,4}\s|>|\s*[-*]\s|\s*\d+[.)]\s|```|\s*\|)/.test(lines[i])) {
      buf.push(lines[i++]);
    }
    out.push(`<p>${inline(esc(buf.join(' ')))}</p>`);
  }
  return out.join('\n');
}
