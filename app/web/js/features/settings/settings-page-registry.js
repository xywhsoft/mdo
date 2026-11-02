// Settings contributed by installed extensions use the same desktop/mobile shell.
const pages = new Map(), listeners = new Set();
const emit = () => { for (const fn of listeners) fn([...pages]); };
export function registerSettingsPage(id, descriptor) {
  if (!/^[a-z][a-z0-9-]{0,95}$/.test(id) || pages.has(id) || typeof descriptor.mount !== "function")
    throw Error(`Invalid or duplicate extension settings page: ${id}`);
  pages.set(id, descriptor);
  try { emit(); } catch (error) { pages.delete(id); emit(); throw error; }
  let active = true;
  return () => { if (active && pages.get(id) === descriptor) { active = false; pages.delete(id); emit(); } };
}
export function subscribeSettingsPages(fn) {
  listeners.add(fn);
  try { fn([...pages]); } catch (error) { listeners.delete(fn); throw error; }
  return () => listeners.delete(fn);
}
