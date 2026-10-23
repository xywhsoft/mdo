(() => {
  const errors = [], toasts = [];
  window.addEventListener("error", event => errors.push(event.message));
  window.addEventListener("unhandledrejection", event => errors.push(String(event.reason?.stack || event.reason)));
  const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
  async function wait(test, label, timeout = 7000) {
    const end = Date.now() + timeout;
    while (Date.now() < end) { const value = await test(); if (value) return value; await pause(30); }
    throw new Error(`Timed out: ${label}; ${JSON.stringify(errors)}; ${JSON.stringify(toasts)}`);
  }
  const check = (value, label) => { if (!value) throw new Error(label); };
  void (async () => {
    await wait(() => document.body, "body"); const output = document.createElement("pre"); output.id = "result"; output.hidden = true; document.body.append(output);
    new MutationObserver(records => { for (const record of records) for (const node of record.addedNodes)
      if (node.nodeType === 1 && node.matches(".toast")) toasts.push(node.textContent); }).observe(document.body, { childList: true, subtree: true });
    const reports = {};
    try {
      await wait(() => globalThis.mdo && document.querySelector(".sidebar-drag-handle:not(:disabled)"), "ready");
      const { t } = await import("/js/i18n.js");
      const query = new URLSearchParams(location.search), one = query.get("one"), two = query.get("two"), free = query.get("free");
      const api = async path => (await (await fetch("/api/v1" + path)).json()).data;
      const layout = () => api("/sidebar-layout");
      const row = key => [...document.querySelectorAll("[data-sidebar-key]")].find(node => node.dataset.sidebarKey === key);
      const handle = key => [...document.querySelectorAll("[data-sidebar-drag]")].find(node => node.dataset.sidebarDrag === key);
      const focus = key => [...document.querySelectorAll("[data-sidebar-focus]")].find(node => node.dataset.sidebarFocus === key);
      if (innerWidth < 760) document.querySelector("#open-sidebar").click();
      const menuItem = async label => (await wait(() => [...document.querySelectorAll("#sidebar-session-menu button")].find(node => node.textContent.trim() === label), label));
      const options = async action => { focus("list-options").click(); (await menuItem(t(action))).click(); };
      const submitDialog = () => document.querySelector(".sidebar-organizer-dialog form").requestSubmit();
      async function create(name) {
        await options("sidebar.newGroup");
        const input = await wait(() => document.querySelector(".sidebar-organizer-dialog input"), "group dialog"); input.value = name;
        submitDialog(); await wait(() => !document.querySelector(".sidebar-organizer-dialog"), "group created");
        return (await layout()).groups.find(group => group.name === name).id;
      }
      const work = await create("Work"), later = await create("Later"); reports.createGroups = true;
      async function move(key, group) {
        if (key.startsWith("p:")) focus(`menu:project:${key.slice(2)}`).click();
        else [...document.querySelectorAll(".session-more")].find(node => node.dataset.sessionKey === key.slice(2)).click();
        (await menuItem(t("sidebar.moveToGroup"))).click();
        const input = await wait(() => document.querySelector(".sidebar-organizer-dialog select"), "destination"); input.value = group;
        submitDialog(); await wait(() => !document.querySelector(".sidebar-organizer-dialog"), "move saved");
      }
      await move("p:alpha", work); await move(`s:default/${free}`, work);
      check(row("p:alpha").dataset.sidebarParent === `custom:${work}`, "project moved as a tree");
      check(row(`s:default/${free}`).dataset.sidebarParent === `custom:${work}`, "session moved visually"); reports.moveAndOwnership = true;
      function drag(source, target, after = false) {
        const start = handle(source), destination = row(target); start.scrollIntoView({ block: "nearest" }); destination.scrollIntoView({ block: "nearest" });
        const sb = start.getBoundingClientRect(), tb = destination.querySelector(":scope > .session-group-heading")?.getBoundingClientRect() || destination.getBoundingClientRect();
        const list = document.querySelector("#session-list");
        start.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true, pointerId: 71, button: 0, clientX: sb.left + 5, clientY: sb.top + sb.height / 2 }));
        list.dispatchEvent(new PointerEvent("pointermove", { bubbles: true, cancelable: true, pointerId: 71,
          clientX: tb.left + 40, clientY: tb.top + (after ? tb.height - 2 : 2) }));
        list.dispatchEvent(new PointerEvent("pointerup", { bubbles: true, pointerId: 71 }));
      }
      drag(`s:alpha/${two}`, `s:alpha/${one}`);
      await wait(async () => (await layout()).placements.filter(item => item.key.startsWith("s:alpha/")).at(0)?.key === `s:alpha/${two}`, "session drag order");
      check([...document.querySelectorAll('[data-sidebar-parent="project:alpha"]')].map(node => node.dataset.sidebarKey).join() === `s:alpha/${two},s:alpha/${one}`, "manual order rendered");
      drag(`s:default/${free}`, `g:${later}`);
      await wait(async () => (await layout()).placements.find(item => item.key === `s:default/${free}`)?.group === later, "cross-group drag");
      drag(`g:${later}`, `g:${work}`);
      await wait(async () => (await layout()).groups[0].id === later, "group drag order"); reports.dragging = true;
      handle(`g:${work}`).dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true, cancelable: true }));
      await wait(async () => (await layout()).groups[0].id === work, "keyboard group order"); reports.keyboardOrder = true;
      focus(`section:custom-${work}`).click();
      await wait(async () => (await layout()).collapsed.includes(`section:custom-${work}`), "collapse saved");
      focus(`section:custom-${work}`).click();
      await wait(async () => !(await layout()).collapsed.includes(`section:custom-${work}`), "expand saved"); reports.collapse = true;
      await options("sidebar.view.timeline");
      await wait(() => document.querySelector(".sidebar-time-heading") && !row(`g:${work}`), "timeline view");
      check(document.querySelectorAll(".session-item").length === 4, "timeline contains every conversation once");
      await options("sidebar.view.workspace"); await wait(() => row(`g:${work}`), "workspace restored");
      reports.views = true;
      focus(`custom-menu:${later}`).click(); (await menuItem(t("sidebar.renameGroup"))).click();
      document.querySelector(".sidebar-organizer-dialog input").value = "Later renamed"; submitDialog();
      await wait(async () => (await layout()).groups.some(group => group.name === "Later renamed"), "renamed group");
      await wait(() => !document.querySelector(".sidebar-organizer-dialog"), "rename closed");
      focus(`custom-menu:${later}`).click(); (await menuItem(t("sidebar.deleteGroup"))).click(); submitDialog();
      await wait(async () => !(await layout()).groups.some(group => group.id === later), "group removed");
      check(document.querySelector(`.session-item[data-session-key="default/${free}"]`), "deleting group retains session");
      reports.renameDelete = true;
      const retainedCount = document.querySelectorAll(".session-item").length;
      await options("sidebar.reset"); submitDialog();
      await wait(async () => !(await layout()).groups.length && !(await layout()).placements.length, "reset organization");
      check(document.querySelectorAll(".session-item").length === retainedCount, "reset keeps all conversations");
      reports.reset = true;
      await create("Saved after restart");
      check(document.documentElement.scrollWidth <= innerWidth, "no horizontal overflow"); check(!errors.length, JSON.stringify(errors));
      output.textContent = JSON.stringify({ passed: true, reports });
    } catch (error) { output.textContent = JSON.stringify({ passed: false, reports, error: String(error.stack), errors, toasts }); }
  })();
})();
