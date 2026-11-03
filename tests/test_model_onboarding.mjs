import assert from "node:assert/strict";
import test from "node:test";
import { needsModelSetup, MODEL_REGISTRATION_URL } from "../app/web/js/features/chat/composer-controls.js";
import { modelLoginBlock } from "../app/web/js/utils/models.js";

test("registration is offered only after a ready empty model catalog", () => {
  assert.equal(needsModelSetup({status:"loading",data:null}),false);
  assert.equal(needsModelSetup({status:"error",data:null}),false);
  assert.equal(needsModelSetup({status:"ready",data:{models:[],login_state:"pending"}}),false);
  assert.equal(needsModelSetup({status:"ready",data:{models:[],login_state:"unavailable"}}),false);
  assert.equal(needsModelSetup({status:"ready",data:{models:[],login_state:"signed_out"}}),true);
  assert.equal(needsModelSetup({status:"ready",data:{models:[],login_state:"ready"}}),true);
  assert.equal(needsModelSetup({status:"ready",data:{models:[{id:"personal"}]}}),false);
});
test("an empty default waits for account catalog loading without blocking personal connections", () => {
  for (const [login_state, expected] of [["pending","profile_login_pending"],["unavailable","profile_login_unavailable"]]) {
    assert.equal(modelLoginBlock({models:[],login_state},""),expected);
    const catalog={models:[{id:"personal",requires_login:false}],login_state};
    assert.equal(modelLoginBlock(catalog,"personal"),"");
    assert.equal(modelLoginBlock(catalog,""),"");
  }
  assert.equal(modelLoginBlock({models:[],login_state:"signed_out"},""),"");
  assert.equal(modelLoginBlock({models:[],login_state:"ready"},""),"");
});
test("registration opens the official account entry with only a public return path", () => {
  const url=new URL(MODEL_REGISTRATION_URL);
  assert.equal(url.origin,"https://ai.xywhsoft.com");assert.equal(url.pathname,"/account/index.html");
  assert.equal(url.searchParams.get("return_to"),"/mdo/account");assert.equal([...url.searchParams].length,1);
});
test("retired model sessions require the account; exact personal names keep their own auth", () => {
  for(const id of ["ornith-1.5-35b","ling-3.0-tiny","ling-gpu","mdo-online.ornith-1.5-35b"]){
    assert.equal(modelLoginBlock({models:[],login_state:"signed_out"},id),"profile_login_required");
    assert.equal(modelLoginBlock({models:[{id,requires_login:false}],login_state:"signed_out"},id),"");
  }
});
