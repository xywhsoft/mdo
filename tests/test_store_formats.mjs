import assert from "node:assert/strict";
import test from "node:test";
import { makeStorePackage, containsCode } from "../app/web/js/features/settings/store-formats.js";
const fields={slug:"review",name:"Review",version:"1.0.0",description:"Review code",readme:"Check changed files",license:"MIT",platforms:["windows-x86_64"]};
const resources=[{kind:"commands",id:"review",content:"Review $ARGUMENTS"}];
test("source export remains portable and does not mutate edited resources",()=>{
  const packageValue=makeStorePackage(fields,resources);assert.equal(packageValue.format,"mdo.extension.v1");assert.deepEqual(packageValue.resources,resources);
  assert.equal(containsCode(resources),false);assert.equal(containsCode([...resources,{kind:"c-agents"}]),true);
});
test("publishing requires a real resource selection and platform declaration",()=>{
  assert.throws(()=>makeStorePackage(fields,[]));assert.throws(()=>makeStorePackage({...fields,platforms:[]},resources));
  assert.throws(()=>makeStorePackage({...fields,platforms:["unknown"]},resources));assert.throws(()=>makeStorePackage({...fields,slug:"../x"},resources));
  assert.throws(()=>makeStorePackage({...fields,version:"1\n2"},resources));
});
