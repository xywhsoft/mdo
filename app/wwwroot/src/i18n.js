// i18n.js — 语言包帮手：pack 由服务端 xrtTemplate 渲染的 /i18n-data.js?lang=xx 提供
// t(key, params, fallback)：缺失键回退中文包 → fallback → key 本身

let gLang = 'zh';
let gPack = {};
let gZh = {};   // 中文基准（切换后兜底）

export function setLangPack(lang, pack) {
  gLang = lang;
  gPack = pack ?? {};
  if (lang === 'zh') gZh = gPack;
}

export function curLang() { return gLang; }

export function t(key, params, fallback) {
  let s = gPack[key] ?? gZh[key] ?? fallback ?? key;
  if (params) {
    for (const [k, v] of Object.entries(params)) {
      s = s.split('{' + k + '}').join(String(v));
    }
  }
  return s;
}

/** 动态拉取语言包（服务端模板渲染产物）并装载 */
export async function loadLang(lang) {
  const m = await import('../i18n-data.js?lang=' + encodeURIComponent(lang) + '&v=' + Date.now());
  setLangPack(m.lang, m.pack);
  document.documentElement.lang = m.lang;
  document.title = t('app.title');
  return m.lang;
}
