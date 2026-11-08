// Validate bounded task-host metadata before deriving any per-file source path.
const utf8=new TextEncoder(),text=new TextDecoder('utf-8',{ignoreBOM:true});
export function safeUploadName(name){
  if(typeof name!=='string'||!name)return false;
  const bytes=utf8.encode(name),stem=name.split('.')[0].toUpperCase();
  return bytes.length<=240&&text.decode(bytes)===name&&!/[<>:"/\\|?*\u0000-\u001f\u007f]/.test(name)&&!/[. ]$/.test(name)&&!/ $/.test(stem)&&
    !/^(CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|CLOCK\$|COM[0-9¹²³]|LPT[0-9¹²³])$/.test(stem);
}
const problem=()=>Object.assign(Error('The task host did not return a complete bounded relative directory manifest'),{code:'upload_snapshot_invalid'});
function path(value,depth){
  if(typeof value!=='string'||utf8.encode(value).length>1024)return false;
  const parts=value.split('/');return parts.length<=depth&&parts.every(safeUploadName);
}
export function directoryManifest(snapshot,name){
  if(!safeUploadName(name)||snapshot?.format!=='mdo.file-tree.v1'||!/^t1-[a-f0-9]{64}$/.test(snapshot.revision)||
    snapshot.root?.path!=='.'||typeof snapshot.root.revision!=='string'||!snapshot.root.revision||
    !Array.isArray(snapshot.files)||snapshot.files.length>128||
    !Array.isArray(snapshot.directories)||snapshot.directories.length>128||snapshot.file_count!==snapshot.files.length||snapshot.directory_count!==snapshot.directories.length)throw problem();
  const directories=snapshot.directories.map(row=>row?.path),seen=new Set(directories);
  if(seen.size!==directories.length||directories.some(value=>!path(value,16)))throw problem();
  const files=[];let total=0;
  for(const file of snapshot.files){
    if(!path(file?.path,17)||seen.has(file.path)||!Number.isSafeInteger(file.size)||file.size<0||file.size>33554432||
      typeof file.revision!=='string'||!file.revision||!/^[a-f0-9]{64}$/.test(file.sha256))throw problem();
    seen.add(file.path);total+=file.size;if(total>67108864)throw problem();
    files.push({path:file.path,size:file.size,sha256:file.sha256});
  }
  const declared=new Set(directories);
  for(const value of seen){const slash=value.lastIndexOf('/');if(slash>=0&&!declared.has(value.slice(0,slash)))throw problem();}
  const manifest={name,directories,files};
  if(snapshot.total_bytes!==total||utf8.encode(JSON.stringify(manifest)).length>245760)throw problem();
  return manifest;
}
