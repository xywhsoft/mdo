import assert from "node:assert/strict";
import test from "node:test";
import { makeStorePackage, containsCode, STORE_KINDS,packageFields } from "../app/web/js/features/settings/store-formats.js";
const fields={slug:"review",name:"Review",version:"1.0.0",description:"Review code",readme:"Check changed files",license:"MIT",platforms:["windows-x86_64"]};
const resources=[{kind:"commands",id:"review",content:'/* mdo-command {"id":"review","description":"Review"} */\nC source'}];
test("source export remains portable and does not mutate edited resources",()=>{
  const packageValue=makeStorePackage(fields,resources);assert.equal(packageValue.format,"mdo.extension.v1");assert.deepEqual(packageValue.resources,resources);
  assert.equal(containsCode(resources),true);assert.equal(containsCode([{kind:"skills"}]),false);assert.equal(containsCode([...resources,{kind:"c-agents"}]),true);
});
test("publishing requires a real resource selection and platform declaration",()=>{
  assert.throws(()=>makeStorePackage(fields,[]));assert.throws(()=>makeStorePackage({...fields,platforms:[]},resources));
  assert.throws(()=>makeStorePackage({...fields,platforms:["unknown"]},resources));assert.throws(()=>makeStorePackage({...fields,slug:"../x"},resources));
  assert.throws(()=>makeStorePackage({...fields,version:"1\n2"},resources));
});
test("author and copyright round-trip independently of account identity and retain byte limits",()=>{
  const p=makeStorePackage({...fields,author:"Original Authors",copyright:"Copyright Original Authors"},resources);
  const restored=packageFields(p.manifest);assert.equal(restored.author,"Original Authors");assert.equal(restored.copyright,"Copyright Original Authors");
  assert(!Object.hasOwn(p.manifest,"submitter"));
  for(const invalid of [null,[],"a\0b","字".repeat(1366)])assert.throws(()=>makeStorePackage({...fields,copyright:invalid},resources));
});
test("ten extension systems include knowledge and keep portable v2 numeric versions",()=>{
  assert.equal(STORE_KINDS.length,10);
  assert(STORE_KINDS.includes("knowledge"));
  for(const kind of ["modules","toolpacks","plugins"]){
    const resource={kind,id:"fixture",content:"fixture"};
    const p=makeStorePackage({...fields,version:"1.10.0"},[resource]);
    assert.equal(p.format,"mdo.extension.v2");assert.equal(p.manifest.min_build_id,30000126);assert.equal(containsCode([resource]),true);
    assert.throws(()=>makeStorePackage({...fields,version:"1.01.0"},[resource]));
    assert.throws(()=>makeStorePackage({...fields,version:"4294967296.0.0"},[resource]));
  }
});
