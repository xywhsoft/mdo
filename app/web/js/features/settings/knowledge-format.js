export const KNOWLEDGE_FORMAT = "mdo.knowledge.v1";
export const KNOWLEDGE_LIMIT = 64 * 1024 * 1024;
export function knowledgePath(path) {
  return typeof path === "string" && path.length <= 220 && path.split("/").every(part => part && part.length <= 64 && !part.startsWith(".") && !/[. ]$/.test(part) && !/[\\:\x00-\x1f\x7f<>"|?*]/.test(part) && !/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part));
}
export function knowledgeMetadata(value = {}) {
  return { format: KNOWLEDGE_FORMAT, name: "", description: "", index: "index.md", prompt: "涉及本知识库的内容时，可以参考 {$path} 目录，index.md 为索引文件。", inject_prompt: true, ...value };
}
export function parseKnowledge(text) {
  const doc = JSON.parse(text);
  if (doc?.format !== KNOWLEDGE_FORMAT || typeof doc.name !== "string" || !doc.name.trim() || typeof doc.description !== "string" || !doc.description.trim() || !knowledgePath(doc.index) || doc.index.toLowerCase() === "knowledge.json" || typeof doc.prompt !== "string" || typeof doc.inject_prompt !== "boolean") throw Error("请填写知识库名称、描述、相对索引文件和可选提示词。");
  for (const [key, limit] of [["name",100],["description",600],["prompt",8192],["author",100],["copyright",4096],["license",80],["version",32]]) if (doc[key] !== undefined && (typeof doc[key] !== "string" || doc[key].includes("\0") || new TextEncoder().encode(doc[key]).length > limit)) throw Error(`${key} 内容超长或无效`);
  const allowed = new Set(["format","name","description","index","prompt","inject_prompt","author","copyright","license","version"]);
  if (Object.keys(doc).some(k => !allowed.has(k))) throw Error("知识库仅包含文档与提示词，不支持安装脚本或机器路径配置。");
  return doc;
}
export function knowledgeBase64(bytes) {
  let text = ""; for (let i=0;i<bytes.length;i+=32768) text += String.fromCharCode(...bytes.subarray(i,i+32768)); return btoa(text);
}
export function knowledgeBytes(encoded) { return Uint8Array.from(atob(encoded), c=>c.charCodeAt(0)); }
export function knowledgeText(file) {
  try { const bytes=knowledgeBytes(file.base64);if(bytes.includes(0))return null;return new TextDecoder("utf-8",{fatal:true}).decode(bytes); } catch { return null; }
}
export function validateKnowledgeFiles(doc, files) {
  if (!Array.isArray(files) || !files.length || files.length > 128) throw Error("每个知识库需要 1–128 个文档文件。");
  let total=0;const paths=[];
  for (const f of files) {
    if (!knowledgePath(f.path) || f.path.toLowerCase()==="knowledge.json"||f.path.toLowerCase().startsWith("knowledge.json/")) throw Error(`文档路径无效：${f.path}`);
    const path=f.path.toLowerCase();if(paths.some(p=>p===path||p.startsWith(path+"/")||path.startsWith(p+"/")))throw Error(`文件路径冲突：${f.path}`);paths.push(path);
    total+=knowledgeBytes(f.base64).length;if(total>KNOWLEDGE_LIMIT)throw Error("文档总量超过 64 MiB。");
  }
  const index=files.find(f=>f.path===doc.index);if(!index || !knowledgeText(index)?.length || knowledgeBytes(index.base64).length>4*1024*1024)throw Error("索引必须是已包含的 UTF-8 文本文档，且不超过 4 MiB。");
  return files;
}
export async function knowledgeFolder(input) {
  const files=Array.from(input);if(!files.length)throw Error("请选择文档目录。");
  const root=files[0].webkitRelativePath?.split("/")[0]||"my-knowledge";let metadata=null,total=0;const docs=[];
  for(const file of files){const path=file.webkitRelativePath?file.webkitRelativePath.slice(root.length+1):file.name;
    if(!knowledgePath(path))throw Error(`目录包含不支持的路径：${path}`);total+=file.size;if(total>KNOWLEDGE_LIMIT)throw Error("文档目录超过 64 MiB。");
    if(path==="knowledge.json"){metadata=parseKnowledge(await file.text());continue;}
    if(docs.length>=128)throw Error("最多导入 128 个文档文件。");docs.push({path,base64:knowledgeBase64(new Uint8Array(await file.arrayBuffer()))});
  }
  const doc=metadata||knowledgeMetadata({name:root,description:"请补充知识库适用的内容和用途。"});
  return {id:root.toLowerCase().replace(/[^a-z0-9._-]/g,"-").slice(0,64).replace(/^[.-]+|[.-]+$/g,"")||"my-knowledge",content:JSON.stringify(doc),files:docs};
}
