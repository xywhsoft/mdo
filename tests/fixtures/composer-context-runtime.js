// Injected into a disposable real mdo/xs site by the bounded CDP driver.
(() => {
  const errors = [];
  const toasts = [];
  window.addEventListener("error", event => errors.push(event.message));
  window.addEventListener("unhandledrejection", event => errors.push(String(event.reason?.stack || event.reason)));
  const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
  async function wait(test, name, timeout = 9000) {
    const until = Date.now() + timeout;
    while (Date.now() < until) { const value = await test(); if (value) return value; await pause(40); }
    throw new Error(`Timed out: ${name}; ${JSON.stringify(errors)}`);
  }
  const check = (value, name) => { if (!value) throw new Error(name); };
  void (async () => {
    await wait(() => document.body, "body");
    const output = document.createElement("pre"); output.id = "result"; output.hidden = true; document.body.append(output);
    new MutationObserver(records => { for (const record of records) for (const node of record.addedNodes)
      if (node.nodeType === 1 && node.matches(".toast")) toasts.push(node.textContent); }).observe(document.body, { childList: true, subtree: true });
    const reports = {};
    try {
      const query = new URLSearchParams(location.search), source = query.get("source"), session = query.get("session");
      const route = id => `#/projects/context-qa/sessions/${id}`;
      const api = async suffix => (await (await fetch(`/api/v1${suffix}`)).json()).data;
      const sessionPath = `/projects/context-qa/sessions/${session}`;
      const input = await wait(() => {
        const node = document.querySelector("#prompt"); return globalThis.mdo && node && !node.disabled && !document.querySelector("#send").disabled ? node : null;
      }, "app ready", 15000);
      await pause(450);
      const enter = (node, value) => { node.value = value; node.setSelectionRange(value.length, value.length); node.dispatchEvent(new Event("input", { bubbles: true })); };
      const chips = () => [...document.querySelectorAll("#composer-contexts .prompt-context-chip")];
      const openAdd = () => document.querySelector("#composer-attach").click();
      const chooseAdd = index => document.querySelectorAll(".composer-add-menu:not([hidden]) button")[index].click();
      if (!sessionStorage.getItem("mdo-context-qa-reloaded")) {
        // Real streamed transcript selection becomes a pinned, source-owned quote.
        const answer = await wait(() => document.querySelector('.timeline-item[data-kind="assistant"] .timeline-body'), "seed reply");
        const range = document.createRange(); range.selectNodeContents(answer);
        getSelection().removeAllRanges(); getSelection().addRange(range);
        answer.dispatchEvent(new PointerEvent("pointerup", { bubbles: true }));
        await wait(() => !document.querySelector(".selection-quote-button").hidden, "selection toolbar");
        document.querySelector(".selection-quote-button").click();
        check(chips().length === 1, "selection quote attached"); getSelection().removeAllRanges();
        openAdd(); check(document.querySelectorAll(".composer-add-menu:not([hidden]) button").length === 6, "unified add menu");
        chooseAdd(2);
        const directory = await wait(() => [...document.querySelectorAll("#file-mention-menu .file-mention-option")]
          .find(node => node.textContent === "src/"), "directory reference");
        directory.click(); check(chips().length === 2 && input.value === "", "directory becomes a chip");
        openAdd(); chooseAdd(3);
        const candidate = await wait(() => [...document.querySelectorAll("#session-mentions-prompt .file-mention-option")]
          .find(node => node.textContent.includes("Source conversation")), "session reference");
        candidate.click(); await wait(() => chips().length === 3, "session snapshot");
        check(input.value === "", "session token consumed");
        const clipboard = new DataTransfer(); clipboard.setData("text/plain", "pasted evidence\n".repeat(160));
        input.dispatchEvent(new ClipboardEvent("paste", { clipboardData: clipboard, bubbles: true, cancelable: true }));
        check(chips().length === 4 && input.value === "", "long paste folded without filling textarea");
        const picker = document.querySelector("#composer-text-file");
        picker.dispatchEvent(new Event("click"));
        const files = new DataTransfer(); files.items.add(new File(["Attached UTF-8 文本"], "notes.txt", { type: "text/plain" }));
        picker.files = files.files; picker.dispatchEvent(new Event("change"));
        await wait(() => chips().length === 5, "text file attachment");
        check(document.querySelector("#composer-input-estimate").textContent.trim(), "context-only tokens are visible");
        enter(input, "Review all attached context");
        await wait(async () => (await api(sessionPath + "/draft")).text.includes("notes.txt"), "durable canonical draft");
        location.hash = route(source);
        await wait(() => document.querySelector("#session-title").textContent.includes("Source conversation") && !chips().length, "other session isolation");
        location.hash = route(session);
        await wait(() => chips().length === 5 && input.value === "Review all attached context", "draft restore after navigation");
        sessionStorage.setItem("mdo-context-qa-reloaded", "1"); location.reload(); return;
      }
      check(chips().length === 5 && input.value === "Review all attached context", "contexts restored after reload");
      reports.reloadAndOwnerIsolation = true;
      document.querySelector("#composer-contexts .prompt-context-open").click();
      await wait(() => document.querySelector(".prompt-context-preview[open]"), "context preview");
      document.querySelector(".prompt-context-preview button").click();
      reports.contextPreview = true;
      const beforeRunIds = new Set((await api("/runs")).items.map(run => run.id));
      document.querySelector("#composer").requestSubmit();
      await wait(() => input.value === "" && chips().length === 0, "submission clears matching contexts");
      await wait(async () => { const runs = (await api("/runs")).items; return runs.some(run => !beforeRunIds.has(run.id)) && runs.every(run => run.terminal); }, "model completion");
      await wait(() => document.querySelector("#stop").hidden, "idle presentation");
      await wait(async () => { const draft = await api(sessionPath + "/draft"); return !draft.text && !draft.submissions.length &&
        document.querySelector("#prompt-queue").hidden; }, "submission queue drained");
      const user = () => [...document.querySelectorAll('.timeline-item[data-kind="user"]')].at(-1);
      const edit = () => user()?.querySelector('[data-timeline-action$="/edit"]');
      await wait(() => edit() && user().querySelectorAll(".prompt-context-chip").length === 5, "transcript context chips");
      check(!user().textContent.includes("<!--mdo-context"), "wire metadata hidden from transcript");
      edit().click();
      await wait(() => document.querySelector(".message-inline-editor"), "inline editor");
      check(!document.querySelector("#message-edit-dialog").open, "editor is in message, not modal");
      const editor = document.querySelector("#message-edit-input");
      check(document.querySelectorAll("#message-edit-contexts .prompt-context-chip").length === 5, "edit preserves contexts");
      enter(editor, "Cancelled edit");
      await wait(async () => (await api(sessionPath + "/draft")).text.startsWith("Cancelled edit"), "inline edit autosave");
      document.querySelector("#cancel-message-edit").click();
      await wait(() => !document.querySelector(".message-inline-editor") && input.value === "", "cancel clears only owned edit draft");
      check(user().textContent.includes("Review all attached context"), "cancel preserves original history");
      reports.inlineCancelAndAutosave = true;
      edit().click(); await wait(() => document.querySelector(".message-inline-editor"), "reopen editor");
      enter(editor, "/context-qa");
      document.querySelector('#message-edit-form button[type="submit"]').click();
      await wait(() => editor.value === "Expanded inline command" && !editor.readOnly, "inline command expansion");
      check(document.querySelector(".message-inline-editor") && user().textContent.includes("Review all attached context"), "expanded command waits for review");
      check(document.querySelectorAll("#message-edit-contexts .prompt-context-chip").length === 5, "command expansion retains references");
      reports.inlineCommand = true;
      const beforeEditedRunIds = new Set((await api("/runs")).items.map(run => run.id));
      enter(editor, "Revised review with original context");
      document.querySelector('#message-edit-form button[type="submit"]').click();
      await wait(() => !document.querySelector(".message-inline-editor"), "edit accepted");
      await wait(async () => { const runs = (await api("/runs")).items; return runs.some(run => !beforeEditedRunIds.has(run.id)) && runs.every(run => run.terminal); }, "edited completion");
      await wait(() => user()?.textContent.includes("Revised review") && document.querySelector("#stop").hidden, "edited transcript");
      check(user().querySelectorAll(".prompt-context-chip").length === 5, "resend retains all context");
      reports.inlineResend = true;
      edit().click(); await wait(() => document.querySelector(".message-inline-editor"), "edit before navigating");
      enter(editor, "Retained edit after navigation");
      location.hash = route(source);
      await wait(() => document.querySelector("#session-title").textContent.includes("Source conversation") && !input.value && !chips().length, "detached editor isolation");
      location.hash = route(session);
      await wait(() => input.value === "Retained edit after navigation" && chips().length === 5, "detached edit restored as original draft");
      enter(input, "");
      while (chips().length) chips()[0].querySelector("button:last-child").click();
      reports.editNavigation = true;
      input.focus(); await pause(200);
      const up = new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true, cancelable: true }); input.dispatchEvent(up);
      await wait(() => input.value.includes("Review all attached context"), "history recall");
      check(chips().length === 5, "history restores contexts");
      enter(input, input.value + " changed");
      const down = new KeyboardEvent("keydown", { key: "ArrowDown", bubbles: true, cancelable: true }); input.dispatchEvent(down);
      check(!down.defaultPrevented && input.value.endsWith(" changed"), "history does not steal editing arrow keys");
      reports.inputHistory = true;
      check(!errors.length, "runtime errors: " + JSON.stringify(errors));
      output.textContent = JSON.stringify({ passed: true, reports });
    } catch (error) { output.textContent = JSON.stringify({ passed: false, reports, error: String(error.stack), errors, toasts }); }
  })();
})();
