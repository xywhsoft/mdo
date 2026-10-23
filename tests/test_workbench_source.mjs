import test from "node:test";
import assert from "node:assert/strict";
import { sourceTokens } from "../app/web/js/features/workbench/source-view.js";
import { fileEncoding, encodeFileText, fileBase64 } from "../app/web/js/features/workbench/file-codec.js";

test("source highlighting preserves exact text and never interprets markup", () => {
  const source='const html = "<script>alert(1)</script>"; // comment';
  const result=sourceTokens(source,'file.js');assert.equal(result.map(token=>token.text).join(''),source);
  assert.equal(result.find(token=>token.kind==='string').text,'"<script>alert(1)</script>"');
  assert.ok(result.some(token=>token.kind==='keyword'&&token.text==='const'));
  assert.deepEqual(sourceTokens(source,'file.txt'),[{text:source}]);
});
test("very long lines use bounded highlighting without losing content", () => {
  const text='"'.repeat(100000);assert.deepEqual(sourceTokens(text,'file.js'),[{text}]);
});
test("text saves retain UTF BOM, UTF-16 endianness and original line endings", () => {
  for (const encoding of ["utf-8", "utf-16le", "utf-16be"]) {
    const format={encoding,bom:true,lineEnding:"\r\n"}, text="小说 🌙\n第二行\n", encoded=encodeFileText(text,format);
    assert.deepEqual(fileEncoding(encoded),{encoding,bom:true});
    const decoder=new TextDecoder(encoding,{fatal:true});
    assert.equal(decoder.decode(encoded.subarray(0,5),{stream:true})+decoder.decode(encoded.subarray(5)),text.replaceAll("\n","\r\n"));
    assert.deepEqual(Uint8Array.from(atob(fileBase64(encoded)),c=>c.charCodeAt(0)),encoded);
  }
  assert.equal(new TextDecoder().decode(encodeFileText("a\r\nb\n",{encoding:"utf-8",bom:false,lineEnding:"\r\n"})),"a\r\nb\r\n");
});
