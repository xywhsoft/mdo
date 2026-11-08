import { wireI18n } from './fixtures/i18n.mjs';
import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {test} from "node:test";

test("an initial requested navigation shows loading until its real page starts",async()=>{
  const source=await readFile(new URL("../../app/web/js/features/workbench/model.js",import.meta.url),"utf8");
  const {browserPhase}=await import(`data:text/javascript;base64,${Buffer.from(wireI18n(source)).toString("base64")}`);
  assert.equal(browserPhase({url:"about:blank",loading:false,initial_navigation_pending:true}),"loading");
  assert.equal(browserPhase({url:"about:blank",loading:false,initial_navigation_pending:false}),"empty");
  assert.equal(browserPhase({url:"https://example.com/",loading:false,initial_navigation_pending:false}),"ready");
});
