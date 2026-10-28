import assert from "node:assert/strict";
import test from "node:test";
import {directoryManifest,directoryEntries,directoryId,directoryPath} from "../app/web/js/features/settings/directory-documents.js";
test("all nine kinds define a root manifest and an independent entry, with no embedded package or compatibility layer",()=>{
 assert.equal(Object.keys(directoryEntries).length,9);
 for(const [kind,entry] of Object.entries(directoryEntries)){
  const doc=directoryManifest(kind,"CALL-ALIAS",{name:"Human title",author:"Upstream",copyright:"Original copyright",license:"MIT"});
  assert.equal(doc.kind,kind);assert.equal(doc.entry,entry);assert.equal(doc.author,"Upstream");assert.equal(doc.copyright,"Original copyright");
  for(const key of ["format","files","resources","package","payload","min_build_id"])assert(!Object.hasOwn(doc,key));
  assert.equal(directoryPath(kind,"STORE-XID"),`/extensions/${kind}/STORE-XID`);
 }
});
test("physical directory identities are separate from callable aliases",()=>{
 assert(directoryId("x".repeat(96)));assert(!directoryId("x".repeat(97)));assert(!directoryId("x".repeat(65),64));
 for(const id of ["../a","a/b","con","x..y","x."])assert(!directoryId(id));
});
