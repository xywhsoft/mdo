import { api } from "../../api/client.js";
import { subscribeTarget, targetState } from "../../api/target.js";
import { BUILTIN_COMMAND_NAMES, expandCommand } from "../settings/extension-formats.js";

const listeners = new Set();
let items = [], pending = null, loaded = false, generation = 0;
let device = targetState().selected?.id || "local";
function invalidate() {
  ++generation; loaded = false; pending = null; items = [];
  for (const listener of listeners) listener(items);
}
window.addEventListener("mdo-extensions-changed", event => { if (event.detail?.kind === "commands") invalidate(); });
subscribeTarget(() => {
  const current = targetState().selected?.id || "local";
  if (current !== device) { device = current; invalidate(); }
});
export function subscribeCommands(listener) { listeners.add(listener); return () => listeners.delete(listener); }
export async function loadCommands() {
  if (loaded) return items;
  if (pending) return pending;
  const serial = generation;
  const read = api.get("/extensions/commands").then(response => {
    if (serial !== generation) return [];
    items = (response.data.items ?? []).filter(item => item.enabled && item.valid && !BUILTIN_COMMAND_NAMES.includes(item.id))
      .map(item => ({ ...item, name: `/${item.id}`, custom: true }));
    loaded = true; for (const listener of listeners) listener(items); return items;
  }).finally(() => { if (pending === read) pending = null; });
  pending = read; return read;
}
export async function commandExpansion(id, args) {
  const serial = generation;
  const item = (await api.get(`/extensions/commands/${id}`)).data;
  if (serial !== generation) throw new Error("Command target changed; select it again");
  if (!item.enabled || !item.valid) throw new Error("Command is missing, disabled or invalid");
  return { text: expandCommand(item.prompt, args), takesArguments: item.prompt.includes("$ARGUMENTS"), argumentHint: item.argument_hint || "" };
}
