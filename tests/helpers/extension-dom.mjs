// Minimal DOM surface for extension UI behavior tests; no product code is mocked.
export const descendants = node => [node, ...node.children.flatMap(child => typeof child === "object" ? descendants(child) : [])];
export class TestElement extends EventTarget {
  constructor(tag) { super(); this.tag = tag; this.tagName = tag.toUpperCase(); this.children = []; this.dataset = {}; this.attributes = {}; this.style = { setProperty(key,value,priority="") { this[key]=String(value); this[`${key}:priority`]=priority; }, getPropertyValue(key) { return this[key] || ""; }, getPropertyPriority(key) { return this[`${key}:priority`] || ""; }, removeProperty(key) { delete this[key]; delete this[`${key}:priority`]; } }; this.value = ""; this.scrollTop = 0; this.isConnected = true; }
  get isConnected() { return this._connected!==false&&(!this.parent||this.parent.isConnected); } set isConnected(value) { this._connected=Boolean(value); }
  set textContent(value) { this.replaceChildren(); this.text = String(value); }
  get textContent() { return this.text || this.children.map(c => typeof c === "object" ? c.textContent : c).join(""); }
  setAttribute(key, value) { this.attributes[key] = String(value); if (["name", "value", "type", "id"].includes(key)) this[key] = value; if (key.startsWith("data-")) this.dataset[key.slice(5).replace(/-([a-z])/g, (_, c) => c.toUpperCase())] = String(value); }
  getAttribute(key) { return this.attributes[key]; }
  removeAttribute(key) { delete this.attributes[key]; }
  get classList() { const self = this; return { contains(c) { return (self.className || "").split(" ").includes(c); }, toggle(c, force) { const values = new Set((self.className || "").split(" ").filter(Boolean)); const add = force === undefined ? !values.has(c) : force; if (add) values.add(c); else values.delete(c); self.className = [...values].join(" "); }, remove(c) { this.toggle(c, false); }, add(c) { this.toggle(c, true); } }; }
  append(...nodes) { for (const node of nodes) { if (typeof node === "object") { node.remove(); node.parent = this; node.isConnected = true; } this.children.push(node); } }
  prepend(...nodes) { const old = [...this.children]; this.replaceChildren(...nodes, ...old); }
  after(node) { const parent=this.parent;if(!parent)return;node.remove();node.parent=parent;node.isConnected=true;parent.children.splice(parent.children.indexOf(this)+1,0,node); }
  replaceWith(node) { const parent=this.parent;if(!parent)return;const index=parent.children.indexOf(this);node.remove();node.parent=parent;node.isConnected=true;parent.children[index]=node;this.parent=null;this.isConnected=false; }
  insertBefore(node, before) { node.remove(); node.parent = this; this.children.splice(this.children.indexOf(before), 0, node); }
  replaceChildren(...nodes) { for (const child of this.children) if (typeof child === "object") child.parent = null; this.children = []; this.text = ""; this.append(...nodes); }
  remove() { this.removed = true; this.isConnected = false; if (this.parent) this.parent.children = this.parent.children.filter(n => n !== this); this.parent = null; }
  matches(selector) { if (selector.startsWith(".")) return this.classList.contains(selector.slice(1)); if (selector.startsWith("[")) { const [, key, value] = selector.match(/^\[([^=\]]+)(?:=["']?([^"'\]]+)["']?)?\]$/) || []; return key && (value === undefined ? key === "open" ? this.open : this.getAttribute(key) !== undefined : this.getAttribute(key) === value); } return this.tagName === selector.toUpperCase(); }
  querySelectorAll(selector) { return descendants(this).slice(1).filter(node => selector.split(",").some(s => node.matches(s.trim()))); }
  querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
  contains(node) { return descendants(this).includes(node); }
  closest(selector) { return selector.split(",").some(s => this.matches(s)) ? this : this.parent?.closest(selector); }
  focus() { document.activeElement = this; }
  click() { if (!this.disabled) { this.onclick?.(new Event("click")); this.dispatchEvent(new Event("click")); } }
  get elements() { return descendants(this).filter(n => ["INPUT", "TEXTAREA", "BUTTON", "SELECT"].includes(n.tagName)); }
  get firstChild() { return this.children[0]; } get lastChild() { return this.children.at(-1); }
  get parentElement() { return this.parent; }
  get hidden() { return this._hidden ?? false; } set hidden(value) { this._hidden = Boolean(value); }
  showModal() { this.open = true; } close() { if (this.open) { this.open = false; this.dispatchEvent(new Event("close")); } }
  getBoundingClientRect() { return { left: 10, right: 70, top: 20, bottom: 45, width: 60, height: 25 }; }
  getClientRects() { return this.hidden ? [] : [this.getBoundingClientRect()]; }
}
export function testDocument() {
  const body = new TestElement("body");
  return { body, documentElement: new TestElement("html"), activeElement: body, createElement: tag => new TestElement(tag), createTextNode: text => String(text), createDocumentFragment: () => new TestElement("fragment"), querySelector: () => null };
}
