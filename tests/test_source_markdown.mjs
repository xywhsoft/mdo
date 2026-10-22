import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { renderSourceMarkdown, renderPackageAttachment } from "../app/web/js/utils/source-markdown.js";

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
test("links allow only HTTP(S) and images do not load",()=>{
  const output=renderSourceMarkdown('[safe](https://example.com/a) [bad](javascript:alert) ![image](https://example.com/x.png)');
  const nodes=flatten(output),links=nodes.filter(n=>n.tag==="a");
  assert.equal(links[0].href,'https://example.com/a');assert.equal(links[0].rel,'noopener noreferrer');
  assert(!links.some(n=>n.href?.startsWith('javascript:')));assert(!nodes.some(n=>n.tag==="img"));
});
test("reviewed attachments expose literal scripts, bound previews and downloadable binary bytes",()=>{
  const view=renderPackageAttachment({path:"bin/tool.py",base64:Buffer.from('<script>bad()</script>\nprint("ok")').toString('base64')});
  assert(view.textContent.includes('<script>bad()</script>'));assert(!flatten(view).some(n=>n.tag==='script'));
  assert(flatten(view).some(n=>n.tag==='button'));
  const large=renderPackageAttachment({path:"large.txt",base64:Buffer.alloc(100000,120).toString('base64')});
  assert.equal(flatten(large).find(n=>n.tag==='pre').textContent.length,65536);
  const binary=renderPackageAttachment({path:"tool.bin",base64:'AP/+'});assert(binary.textContent.includes('Binary file'));
  const bad=renderPackageAttachment({path:"bad",base64:'!'});assert(bad.textContent.includes('Invalid attachment'));
});
const ecosystemSite = process.env.MDO_ECOSYSTEM_SITE_ROOT;
test("the configured ecosystem website shares the source renderer", {
  skip: ecosystemSite ? false : "Set MDO_ECOSYSTEM_SITE_ROOT for the website integration check",
}, () => {
  // Git may check out CRLF on Windows. Compare source text, retaining all
  // other differences; a missing configured website is still a test failure.
  const source = path => readFileSync(path, "utf8").replaceAll("\r\n", "\n");
  assert.equal(source(new URL('../app/web/js/utils/source-markdown.js',import.meta.url)),
    source(resolve(ecosystemSite, 'plugin/mdo/page/ecosystem/markdown.js')));
});
test("ecosystem translations follow the application's flat language-pack contract",()=>{
  for(const locale of ['zh-CN','en-US','ru-RU']){
    const pack=JSON.parse(readFileSync(new URL(`../app/web/lang/${locale}.json`,import.meta.url)));
    assert(Object.values(pack).every(value=>typeof value==='string'));
    for(const key of ['store.drafts','store.state.withdrawn','ecosystem.returnPackage'])assert.equal(typeof pack[key],'string');
  }
});
