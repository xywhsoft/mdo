import test from "node:test";
import assert from "node:assert/strict";
import { connectorTicket } from "../app/web/js/api/connector.js";

test("controller refresh preserves account binding before obtaining a device ticket", async () => {
  const original = globalThis.fetch;
  let owner = 2, expired = true, refreshed = false, refreshes = 0, connects = 0;
  globalThis.fetch = async path => {
    let data = {};
    if (path === "/api/v1/account") data = { state:expired&&!refreshed?"refreshing":"signed_in",
      refresh_available:true,busy:false,profile:{id:owner},origin:"https://ai.xywhsoft.com" };
    if (path === "/api/v1/account/refresh") { refreshed = true; refreshes++; }
    if (path === "/api/v1/connector/devices") { connects++; data={job:{id:"b".repeat(32)}}; }
    if (path === "/api/v1/connector/state") data={job:{id:"b".repeat(32),state:"done"}};
    if (path === "/api/v1/connector/ticket") data={ticket:"c".repeat(64)};
    return Response.json({ok:true,data},{headers:{"X-Mdo-Write-Token":"a".repeat(32)+"-0"}});
  };
  try {
    const ticket = await connectorTicket("d".repeat(32),"control",undefined,"2");
    assert.equal(ticket.owner,"2"); assert.equal(refreshes,1); assert.equal(connects,1);
    owner = 3; expired = true; refreshed = false;
    await assert.rejects(connectorTicket("d".repeat(32),"control",undefined,"2"),e=>e.code==="connector_account_changed");
    assert.equal(refreshes,1); assert.equal(connects,1);
    owner=2;
    const boundary=globalThis.fetch;
    globalThis.fetch=async path=> {
      const result=await boundary(path);
      if(path==="/api/v1/account/refresh")owner=3;
      return result;
    };
    await assert.rejects(connectorTicket("d".repeat(32),"control",undefined,"2"),e=>e.code==="connector_account_changed");
    assert.equal(connects,1);
  } finally {globalThis.fetch=original;}
});
