import assert from "node:assert/strict";
import test from "node:test";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";
import { createSubmissionTimeline } from "../app/web/js/features/chat/submission-timeline.js";

function fixture() {
  globalThis.document = testDocument();
  const host = new TestElement("section"), container = new TestElement("ol");
  document.body.append(host); host.append(container);
  let scope = "local", state = { projectId: "default", sessionId: "", entries: [], events: [] };
  const view = createSubmissionTimeline({ host, container, scope: () => scope, snapshot: () => state });
  return { view, host, container, state, switchScope(value) { scope = value; },
    value: (id = "a") => ({ id, text: "A new message", projectId: "default", sessionId: "", attachments: [] }) };
}

test("edited resend is visible while the old run is finishing and hands off by submission ID",()=>{
  const f=fixture();f.state.sessionId='editing';f.state.active=true;let cancelled=0;
  try{
    const value={...f.value('edited'),sessionId:'editing',edit:true,phase:'waiting',cancelEdit:()=>cancelled++};
    f.view.begin(value);assert.equal(f.container.children.length,2);
    assert.equal(f.container.children[1].children.at(-1).hidden,false);
    f.container.children[1].children.at(-1).onclick();assert.equal(cancelled,1);
    f.view.progress('edited','updating');assert.equal(f.container.children[1].children.at(-1).hidden,true);
    f.state.entries=[{id:'edited',text:value.text,state:'prepared',attachments:[]}];f.view.accepted('edited');
    assert.equal(f.container.children.length,2);
    f.state.events=[{kind:'agent_start',queue_item_id:'edited'}];f.view.render();assert.equal(f.container.children.length,0);
  }finally{f.view.destroy();}
});

test("a click is visible before storage and the same ID is replaced by authoritative history", () => {
  const f = fixture();
  try {
    assert.equal(f.view.begin(f.value()), "a");
    assert.equal(f.container.hidden, false);
    assert.equal(f.container.children.length, 2);
    assert.equal(f.container.children[0].dataset.kind, "user");
    assert.match(f.container.children[0].textContent, /A new message/);
    assert.equal(f.view.begin(f.value("duplicate")), false, "a double click cannot create a second unsaved intent");
    f.state.sessionId = "created";
    f.state.entries = [{ id: "a", text: "A new message", state: "prepared", attachments: [] }];
    f.view.bindProject("default", "created"); f.view.accepted("a");
    assert.equal(f.container.children.length, 2, "draft migration retains one bubble");
    f.state.entries = [{ ...f.state.entries[0], state: "sending", start_claimed: true }];
    f.view.render(); assert.equal(f.container.children.length, 2);
    f.state.events = [{ kind: "agent_start", queue_item_id: "a" }];
    f.view.render(); assert.equal(f.container.children.length, 0);
    assert.equal(f.host.classList.contains("has-submissions"), false);
  } finally { f.view.destroy(); }
});

test("a failed provisional message can be retried with its original admission identity", () => {
  const f = fixture();
  try {
    assert.equal(f.view.begin(f.value()), "a");
    f.view.fail("a", new Error("Draft save failed before admission"));
    assert.equal(f.state.entries.length, 0);
    assert.equal(f.container.children[1].dataset.state, "error");
    assert.equal(f.view.begin(f.value("new-click-id")), "a");
    assert.equal(f.container.children.length, 2, "retry reuses the visible message");
    assert.equal(f.container.children[1].dataset.state, "saving");
    assert.equal(f.view.begin(f.value("double-click")), false, "a running retry still suppresses double clicks");
  } finally { f.view.destroy(); }
});

test("an edit retry retains its identity without adopting an identical ordinary send or another source", () => {
  const f = fixture();
  try {
    f.view.begin(f.value("ordinary"));
    f.view.fail("ordinary", new Error("ordinary draft save failed"));
    const edit = { ...f.value("edit"), edit: true, editSourceEventId: 17, phase: "waiting" };
    assert.equal(f.view.begin(edit), "edit");
    f.view.fail("edit", new Error("edit submission failed"));
    assert.equal(f.view.begin({ ...edit, id: "retry" }), "edit");
    assert.equal(f.view.begin({ ...edit, id: "different-source", editSourceEventId: 18 }), "different-source");
    assert.equal(f.container.children.length, 6);
  } finally { f.view.destroy(); }
});

test("errors and existing guarded retry controls live in the matching reply without losing focus", () => {
  const f = fixture(), original = new TestElement("section"), error = new TestElement("div"), retry = new TestElement("button");
  document.body.append(original); original.append(error); error.append(retry);
  try {
    f.view.begin(f.value()); f.view.fail("a", new Error("Disk unavailable"));
    retry.focus(); f.view.attachError(error, "a");
    assert.equal(error.parentElement, f.container.children[1]);
    f.view.render(); assert.equal(document.activeElement, retry);
    assert.equal(f.container.children[1].dataset.state, "error");
    assert.equal(f.container.children[1].children[0].hidden, true, "inline actions replace the plain status");
    f.view.detachError(); assert.equal(error.parentElement, original);
  } finally { f.view.destroy(); }
});

test("reviewed creation can rebind remaining intents to a new session ID", () => {
  const f = fixture();
  try {
    f.view.begin(f.value("followup"));
    f.view.bindProject("default", "rejected-session");
    f.state.sessionId = "reviewed-session";
    f.state.entries = [{ id: "followup", text: "A new message", state: "prepared" }];
    f.view.render();
    assert.equal(f.container.children.length, 2);
    assert.equal(f.container.children[0].dataset.submissionId, "followup");
  } finally { f.view.destroy(); }
});

test("removed entries and device switches cannot leak provisional messages or error controls", () => {
  const f = fixture();
  try {
    f.view.begin(f.value()); f.view.accepted("a");
    f.view.forget("a"); assert.equal(f.container.hidden, true);
    f.view.begin(f.value("b")); f.switchScope("another-device"); f.view.render();
    assert.equal(f.container.hidden, true);
    f.state.projectId = "other"; f.view.render(); assert.equal(f.container.children.length, 0);
  } finally { f.view.destroy(); }
});

test("a restored pending queue shows its message and a genuine preflight error in the reply", () => {
  const f = fixture();
  try {
    f.state.entries = [{ id: "a", text: "Recovered message", state: "pending", attachments: [] }];
    f.state.paused = true; f.state.error = "profile_model_missing";
    f.view.render(); assert.equal(f.container.children.length, 2);
    assert.equal(f.container.children[1].dataset.state, "error");
  } finally { f.view.destroy(); }
});
