import { t as uiErrorText } from "../i18n.js";
// Source documentation uses a small DOM renderer. HTML remains literal text,
// links allow HTTP(S) only, and image syntax never starts a network request.
export function renderSourceMarkdown(source) {
  const make = (tag,text) => { const e=document.createElement(tag);if(text!==undefined)e.textContent=text;return e; };
  const inline = (parent,text) => {
    const pattern=/(`[^`\n]+`|\*\*[^*\n]+\*\*|\*[^*\n]+\*|\[[^\]\n]+\]\([^\s)]+\))/g;let start=0;
    for(const match of text.matchAll(pattern)) {
      parent.append(document.createTextNode(text.slice(start,match.index)));const token=match[0];let e;
      if(token.startsWith("`"))e=make("code",token.slice(1,-1));
      else if(token.startsWith("**"))e=make("strong",token.slice(2,-2));
      else if(token.startsWith("*"))e=make("em",token.slice(1,-1));
      else {const split=token.indexOf("](");try {const url=new URL(token.slice(split+2,-1));if(["https:","http:"].includes(url.protocol)){e=make("a",token.slice(1,split));e.href=url.href;e.target="_blank";e.rel="noopener noreferrer";}}catch {}}
      parent.append(e || document.createTextNode(token));start=match.index+token.length;
    }
    parent.append(document.createTextNode(text.slice(start)));
  };
  const lines=String(source || "").replace(/\r\n?/g,"\n").split("\n"),out=document.createDocumentFragment();let i=0;
  const boundary=line=>/^\s*$|^```|^#{1,4}\s|^\s{0,3}(?:[-*]|\d+[.)])\s|^>/.test(line);
  while(i<lines.length) {
    const line=lines[i++];if(!line.trim())continue;
    if(/^```/.test(line)){const code=[];while(i<lines.length&&!/^```/.test(lines[i]))code.push(lines[i++]);if(i<lines.length)i++;const pre=make("pre");pre.append(make("code",code.join("\n")));out.append(pre);continue;}
    const heading=/^(#{1,4})\s+(.*)$/.exec(line);
    if(heading){const e=make(`h${heading[1].length}`);inline(e,heading[2]);out.append(e);continue;}
    const list=/^\s{0,3}([-*]|\d+[.)])\s+(.*)$/.exec(line);
    if(list){const ordered=/\d/.test(list[1]),e=make(ordered?"ol":"ul");let item=list;
      for(;;){const li=make("li");inline(li,item[2]);e.append(li);item=/^\s{0,3}([-*]|\d+[.)])\s+(.*)$/.exec(lines[i] || "");if(!item||/\d/.test(item[1])!==ordered)break;i++;}out.append(e);continue;}
    if(/^>/.test(line)){const e=make("blockquote");inline(e,line.replace(/^>\s?/,""));out.append(e);continue;}
    const paragraph=[line];while(i<lines.length&&!boundary(lines[i]))paragraph.push(lines[i++]);const e=make("p");inline(e,paragraph.join("\n"));out.append(e);
  }
  return out;
}

// Attached scripts are part of the reviewed package, not just filenames.
// Never execute or interpret their markup. Downloads retain the exact bytes.
export function renderPackageAttachment(file, labels = {}) {
  const make=(tag,text)=>{const e=document.createElement(tag);if(text!==undefined)e.textContent=text;return e;};
  const out=make("details");out.append(make("summary",file.path));
  try {
    if(typeof file.base64!=="string" || file.base64.length>1400000)throw Error(uiErrorText("error.detail.invalidAttachment", {}, "Invalid attachment"));
    const raw=atob(file.base64),bytes=Uint8Array.from(raw,c=>c.charCodeAt(0));
    const meta=make("p",`${bytes.length} B`);out.append(meta);
    let source=null;try{source=new TextDecoder("utf-8",{fatal:true}).decode(bytes);if(/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/.test(source))source=null;}catch{}
    if(source!==null){out.append(make("pre",source.slice(0,65536)));if(source.length>65536)out.append(make("p",labels.truncated||"Preview: 64 KiB. Download to inspect the complete file."));}
    else out.append(make("p",labels.binary||"Binary file. Download to inspect."));
    if(globalThis.crypto?.subtle)void crypto.subtle.digest("SHA-256",bytes).then(hash=>{meta.textContent+=` · SHA-256 ${Array.from(new Uint8Array(hash),b=>b.toString(16).padStart(2,"0")).join("")}`;}).catch(()=>{});
    const download=make("button",labels.download||"Download file");download.type="button";
    download.onclick=()=>{const url=URL.createObjectURL(new Blob([bytes])),a=make("a");a.href=url;a.download=file.path.split("/").at(-1)||"file.bin";a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};out.append(download);
  }catch{out.append(make("p",labels.invalid||"Invalid attachment"));}
  return out;
}
