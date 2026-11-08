// UI scaffolding for source-extracted component tests. Production translations
// are exercised by the applied main application; these peers retain fallbacks.
export const uiText = (key, args = {}, fallback = key) =>
  fallback.replace(/\{([^}]+)\}/g, (_, name) => String(args[name] ?? `{${name}}`));
export const uiMessage = uiText;
export const writeUiText = (node, value) => { node.textContent = value; };
export const writeUiAttribute = (node, name, value) => {
  if (name === 'title') node.title = value;
  node.setAttribute(name, value);
};
export { uiText as t, writeUiText as setUiText, writeUiAttribute as setUiAttribute };
Object.assign(globalThis, {uiText, uiMessage, writeUiText, writeUiAttribute});

export const wireI18n = source => source.replace(
  /from (["'])(?:\.\.\/)+i18n\.js\1/g,
  `from ${JSON.stringify(new URL('./i18n.mjs', import.meta.url).href)}`);
