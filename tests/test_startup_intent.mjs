import assert from "node:assert/strict";
import test from "node:test";
import { createStartupIntent } from "../app/web/js/features/shell/startup-intent.js";

test("startup fallback yields to input, composition, pointer and meaningful keyboard intent", () => {
  for (const type of ["input","compositionstart","pointerdown","keydown"]) {
    const root=new EventTarget(),route={view:"workspace"};
    const intent=createStartupIntent({root,navigation:{get:()=>route}});
    assert.equal(intent.allowsRestore(),true);
    const event=new Event(type); Object.assign(event,{key:"Tab"}); root.dispatchEvent(event);
    assert.equal(intent.allowsRestore(),false); intent.destroy();
  }
});

test("modifiers and data refresh do not claim startup; a route change does and teardown removes listeners", () => {
  const root=new EventTarget(); let route={view:"workspace"};
  const intent=createStartupIntent({root,navigation:{get:()=>route}});
  const modifier=new Event("keydown"); Object.assign(modifier,{key:"Control"}); root.dispatchEvent(modifier);
  assert.equal(intent.allowsRestore(),true);
  route={view:"settings"}; assert.equal(intent.allowsRestore(),false); intent.destroy();
  const clean=createStartupIntent({root,navigation:{get:()=>route}});
  clean.destroy(); root.dispatchEvent(new Event("input")); assert.equal(clean.allowsRestore(),true);
});
