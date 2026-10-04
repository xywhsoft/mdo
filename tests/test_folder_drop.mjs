import assert from "node:assert/strict";
import test from "node:test";
import { absoluteFolderPath, droppedFolderPath } from "../app/web/js/features/sessions/folder-drop.js";

const transfer = (text, uri = "") => ({ getData: (type) => type === "text/uri-list" ? uri : text });

test("directory drops use absolute host paths, never a virtual entry fullPath", async () => {
  assert.equal(await droppedFolderPath(transfer('"D:\\工作 目录\\mdo"')), "D:\\工作 目录\\mdo");
  assert.equal(await droppedFolderPath(transfer("", "file:///D:/work/hello%20world")), "D:\\work\\hello world");
  assert.equal(await droppedFolderPath(transfer("", "file://server/share/project")), "\\\\server\\share\\project");
  assert.equal(await droppedFolderPath(transfer("/home/user/project")), "/home/user/project");
  for (const path of ["mdo", "C:relative", "https://example.com", "D:\\bad\npath", "D:\\" + "字".repeat(683)])
    assert.equal(absoluteFolderPath(path), "");
  await assert.rejects(droppedFolderPath({ items: [{kind:"file", webkitGetAsEntry: () =>
    ({isDirectory:true, fullPath:"/virtual-project"})}] }), {code:"folder_drop_unavailable"});
  await assert.rejects(droppedFolderPath(transfer("", "https://example.com/project")), {code:"folder_drop_unavailable"});
});

test("multi-folder and file drops are rejected before registering a workspace", async () => {
  await assert.rejects(droppedFolderPath({files:[{},{}]}), {code:"folder_drop_one"});
  await assert.rejects(droppedFolderPath({items:[{kind:"file",webkitGetAsEntry:()=>({isFile:true})}]}), {code:"folder_drop_directory"});
  await assert.rejects(droppedFolderPath(transfer("", "file:///one\nfile:///two")), {code:"folder_drop_one"});
});

test("native file paths require the matching request, and closing cancels the listener", async () => {
  const listeners = new Set();
  const sent = [];
  const bridge = {
    addEventListener(type, listener) { listeners.add(listener); },
    removeEventListener(type, listener) { listeners.delete(listener); },
    postMessageWithAdditionalObjects(message, files) { sent.push({message,files}); },
  };
  const receive = (data) => { for (const listener of listeners) listener({data}); };
  const file = {};
  const controller = new AbortController();
  const pending = droppedFolderPath({files:[file]}, {bridge, signal:controller.signal});
  const id = sent[0].message.split(":")[1];
  receive({type:"xs-directory-drop",request_id:"old",path:"D:\\stale"});
  assert.equal(listeners.size, 1);
  receive({type:"xs-directory-drop",request_id:id,path:"D:\\selected"});
  assert.equal(await pending, "D:\\selected");
  assert.equal(listeners.size, 0);
  const aborting = droppedFolderPath({files:[file]}, {bridge,signal:controller.signal});
  controller.abort();
  await assert.rejects(aborting, {name:"AbortError"});
  assert.equal(listeners.size, 0);
  receive({type:"xs-directory-drop",request_id:sent[1].message.split(":")[1],path:"D:\\late"});
  assert.equal(sent[0].files[0], file);
});

test("directory handles are captured during the drop event and passed to the native host", async () => {
  const handle = {kind:"directory"};
  let captured = false, receive;
  const bridge = {
    addEventListener(type, listener) { receive = listener; }, removeEventListener() {},
    postMessageWithAdditionalObjects(message, objects) {
      assert.equal(objects[0], handle);
      queueMicrotask(() => receive({data:{type:"xs-directory-drop",request_id:message.split(":")[1],path:"D:\\folder"}}));
    },
  };
  const promise = droppedFolderPath({items:[{kind:"file",getAsFileSystemHandle() {
    captured=true; return Promise.resolve(handle);
  }}]}, {bridge});
  assert.equal(captured, true);
  assert.equal(await promise, "D:\\folder");
});
