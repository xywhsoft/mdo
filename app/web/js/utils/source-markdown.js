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
