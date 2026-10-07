import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { renderSourceMarkdown } from "../app/web/js/utils/source-markdown.js";

class Node {
  constructor(tag,text="") {this.tag=tag;this.text=text;this.children=[];}
  set textContent(text) {this.text=text;this.children=[];}
  get textContent() {return this.text+this.children.map(n=>n.textContent).join("");}
  append(...nodes) {this.children.push(...nodes);}
}
globalThis.document={createElement:tag=>new Node(tag),createTextNode:text=>new Node("text",text),createDocumentFragment:()=>new Node("fragment")};
const flatten=root=>[root,...root.children.flatMap(flatten)];

test("source documentation renders headings, lists and literal code without executing HTML",()=>{
  const source='# Guide\n\n**Review** `file.c`\n\n- Read\n- Inspect\n\n```html\n<img src=x onerror=alert(1)>\n```\n\n<script>alert(1)</script>';
  const output=renderSourceMarkdown(source),nodes=flatten(output);
  for(const tag of ["h1","strong","code","ul","li","pre"])assert(nodes.some(n=>n.tag===tag));
  assert(!nodes.some(n=>["img","script"].includes(n.tag)));
  assert(output.textContent.includes('<script>alert(1)</script>'));
  assert.equal(nodes.find(n=>n.tag==="pre").textContent,'<img src=x onerror=alert(1)>');
});
test("links allow only HTTP(S), images do not load and both clients share the renderer",()=>{
  const output=renderSourceMarkdown('[safe](https://example.com/a) [bad](javascript:alert) ![image](https://example.com/x.png)');
  const nodes=flatten(output),links=nodes.filter(n=>n.tag==="a");
  assert.equal(links[0].href,'https://example.com/a');assert.equal(links[0].rel,'noopener noreferrer');
  assert(!links.some(n=>n.href?.startsWith('javascript:')));assert(!nodes.some(n=>n.tag==="img"));
  assert.deepEqual(readFileSync(new URL('../app/web/js/utils/source-markdown.js',import.meta.url)),readFileSync('D:/GIT/home/host/xywhsoft_ai/plugin/mdo/page/ecosystem/markdown.js'));
});
test("ecosystem translations follow the application's flat language-pack contract",()=>{
  for(const locale of ['zh-CN','en-US','ru-RU']){
    const pack=JSON.parse(readFileSync(new URL(`../app/web/lang/${locale}.json`,import.meta.url)));
    assert(Object.values(pack).every(value=>typeof value==='string'));
    for(const key of ['store.drafts','store.state.withdrawn','ecosystem.returnPackage'])assert.equal(typeof pack[key],'string');
  }
});
