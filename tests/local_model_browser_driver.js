(async () => {
  const checks = [];
  const until = async (read, label) => {
    for (let i = 0; i < 260; i++) { if (await read()) return; await new Promise(r => setTimeout(r, 40)); }
    throw Error(label);
  };
  const check = (value, label) => { if (!value) throw Error(label); checks.push(label); };
  await until(() => document.body, "body");
  const result = document.createElement("pre"); result.id = "result"; result.hidden = true; document.body.append(result);
  try {
    const { api, currentPageWriteToken } = await import("/js/api/client.js");
    await until(() => currentPageWriteToken(), "writable bootstrap");
    const { t } = await import("/js/i18n.js");
    const { loadSettings } = await import("/js/state/settings.js");
    const params = new URL(location.href).searchParams;
    await until(() => document.documentElement.dataset.mdoConfiguredLocale, "application locale settled");
    // The application's first locale initialization may still be committing.
    // Re-read its guard rather than making an unconditional test-only write.
    for (let attempt = 0; ; attempt++) {
      const settings = await api.get("/settings");
      try { await api.patch("/settings/settings", { schema_version: 1, patch: { locale: params.get("locale") } }, { ifMatch: settings.etag }); break; }
      catch (error) { if (![409, 412].includes(error.status) || attempt >= 4) throw error; }
    }
    await loadSettings();
    await until(() => document.documentElement.lang === params.get("locale"), "settings store applied saved locale");
    check(document.documentElement.lang === params.get("locale"), "Requested interface language applied");
    const tr = key => t(`localModels.${key}`);
    const { navigation } = await import("/js/state/navigation.js");
    navigation.openSettings("local-models");
    const host = document.querySelector('[data-settings-panel="local-models"]');
    const button = (key, root = host) => [...root.querySelectorAll("button")].find(node => node.textContent === tr(key) && !node.closest("[hidden]"));
    const clickAction = (key, root = host) => {
      const control = button(key, root);
      const menu = control.closest(".eb-menu");
      if (menu && !menu.open) menu.querySelector("summary").click();
      control.click();
    };
    const dialog = () => [...document.querySelectorAll("dialog[open]")].at(-1);
    await until(() => !host.hidden && host.querySelector(".eb-row"), "installed model list in settings");
    check(host.querySelector('.store-tabs [aria-selected="true"]').textContent === tr("local") && !!host.querySelector(".eb-toolbar .eb-menu"), "Local model page uses shared extension tabs, toolbar and rows");
    check(host.textContent.includes(tr("state.installed")), "Installed receipt shown");
    host.querySelector(".eb-open").click(); await until(() => dialog()?.querySelector(".eb-facts"), "shared installed detail window");
    dialog().querySelector(".mdo-layer-controls button:last-child").click(); await until(() => !dialog(), "installed detail closed");
    if (params.get("qa_services") === "1") {
      button("createDeployment").click(); await until(() => dialog()?.querySelector("[name=context_tokens]"), "native deployment options loaded");
      const ctx = dialog().querySelector("[name=context_tokens]"); ctx.value = "512";
      button("confirm", dialog()).click(); await until(() => dialog()?.querySelector("[role=status]")?.textContent && !button("confirm", dialog()).disabled, "invalid deployment rejected");
      check(ctx.value === "512", "Failed deployment save retains parameter edits"); ctx.value = "8192";
      button("confirm", dialog()).click(); await until(() => !dialog(), "deployment created");
      await until(() => button("start"), "stopped deployment rendered"); check(!button("chat"), "Stopped deployment does not offer an unusable chat action");
      button("start").click(); await until(() => button("verify"), "deployment reached ready");
      clickAction("verify"); await until(() => dialog()?.textContent.includes(tr("verifiedTools")), "native streaming and tools verification complete");
      await dialog().querySelector('[aria-label]').closest("dialog").querySelector(".mdo-layer-controls button:last-child").click(); await until(() => !dialog(), "verification dialog closed");
      check(!!button("chat"), "Ready model offers mdo conversation entry");
      clickAction("web"); await until(() => dialog()?.querySelector("input[type=password]"), "runtime credentials loaded only on demand");
      const key = dialog().querySelector("input[type=password]").value;
      check(key.length >= 32 && !JSON.stringify((await api.get("/local-models")).data).includes(key), "Temporary API key stays out of registry snapshots");
      dialog().querySelector(".mdo-layer-controls button:last-child").click(); await until(() => !dialog(), "web dialog closed");
      button("stop").click(); await until(() => button("configure"), "deployment stopped without refresh");
      check(button("configure").nextElementSibling?.matches(".eb-menu"), "Configure is beside the shared More menu");
      button("configure").click(); await until(() => dialog()?.querySelector("[name=output_tokens]"), "deployment editor");
      dialog().querySelector("[name=output_tokens]").value = "1024"; dialog().querySelector("[name=reasoning_mode]").value = "on"; button("confirm", dialog()).click(); await until(() => !dialog(), "edited deployment saved");
      const managed = (await api.get("/models")).data.models.find(m => m.managed_local);
      const { fillReasoningOptions } = await import("/js/features/chat/composer-profile.js"); const thinking = document.createElement("select"); fillReasoningOptions(thinking, managed);
      check(thinking.disabled && thinking.selectedOptions[0].textContent === tr("reasoning_mode.on"), "Composer shows the effective deployment thinking mode");
      await until(() => button("rollback"), "previous configuration available"); clickAction("rollback"); button("confirm", dialog()).click(); await until(() => !dialog(), "configuration rollback finished");
      clickAction("deleteDeployment"); button("confirm", dialog()).click(); await until(() => !dialog() && !button("start"), "deployment deleted while keeping installation");
      check(!!button("createDeployment"), "Model resources remain reusable after deleting deployment");
    }
    const menu = host.querySelector(".eb-toolbar .eb-menu"), anchor = menu.querySelector("summary");
    anchor.click();
    await until(() => menu.querySelector(".eb-menu-items").style.left, "shared menu positioned");
    const menuBox = menu.querySelector(".eb-menu-items").getBoundingClientRect(), anchorBox = anchor.getBoundingClientRect();
    check(menuBox.left >= 0 && menuBox.right <= innerWidth && Math.min(Math.abs(menuBox.top - anchorBox.bottom), Math.abs(menuBox.bottom - anchorBox.top)) <= 6, "More menu opens next to its trigger within the viewport");
    clickAction("import"); await until(() => dialog()?.querySelector("[name=path]"), "import form");
    const path = dialog().querySelector("[name=path]"); path.value = "relative.gguf";
    button("confirm", dialog()).click(); await until(() => dialog()?.querySelector(".extension-error")?.textContent, "invalid import rejected");
    check(path.value === "relative.gguf", "Failed import retains user input");
    path.value = params.get("qa_model_path"); dialog().querySelector("[name=name]").value = "Browser reference";
    const copy = dialog().querySelector("input[type=checkbox]");
    check(copy.getAttribute("role") === "switch" && copy.closest("label").getBoundingClientRect().height >= 44, "Touch-friendly capsule switch");
    copy.checked = false; copy.dispatchEvent(new Event("change", { bubbles: true }));
    await until(() => !button("confirm", dialog()).disabled, "import retry enabled");
    button("confirm", dialog()).click(); await until(() => !dialog(), "import accepted");
    await until(async () => Object.values((await api.get("/local-models")).data.installations).some(row => row.name === "Browser reference" && row.state === "installed"), "reference installation");
    button("refresh").click(); await until(() => [...host.querySelectorAll("article")].some(row => row.textContent.includes("Browser reference")), "reference rendered");
    const reference = () => [...host.querySelectorAll(".eb-row")].find(row => row.querySelector(".eb-name").textContent === "Browser reference");
    await until(() => reference() && button("uninstall", reference()), "completed reference rendered");
    const filter = host.querySelector(".eb-toolbar input[type=search]");
    filter.value = "Browser reference"; filter.dispatchEvent(new Event("input", { bubbles: true }));
    check(host.querySelectorAll('.eb-row').length === 1 && host.querySelector('.eb-name').textContent === "Browser reference", "Installed list can be filtered by name");
    filter.value = ""; filter.dispatchEvent(new Event("input", { bubbles: true }));
    clickAction("uninstall", reference());
    await until(() => dialog()?.querySelector("input[role=switch]"), "uninstall options");
    check(dialog().textContent.includes(tr("removeNotice")), "Ownership explained before removal");
    button("confirm", dialog()).click(); await until(() => !dialog(), "reference uninstalled");
    button("store").click(); button("local").click(); button("store").click();
    await until(() => button("details"), "store survives fast tab switching");
    check(host.textContent.includes("1.0 MiB"), "Catalog weight size rendered");
    if (innerWidth <= 600) {
      const row = button("details").closest(".eb-row");
      const info = row.querySelector(".eb-open").getBoundingClientRect(), actions = row.querySelector(".eb-actions").getBoundingClientRect();
      check(info.bottom <= actions.top, "Narrow layout wraps actions below the full-width model name");
    }
    check(host.querySelector(".single-resource-store .eb-pagination").hidden, "Shared pagination hides for a single page");
    button("details").click(); await until(() => dialog()?.querySelector("select"), "deployment profiles");
    check(!!dialog().querySelector(".eb-detail .eb-facts") && dialog().textContent.includes(tr("ram")) && dialog().textContent.includes("llama.cpp"), "Store uses shared detail window for requirements and fixed runtime");
    check(!dialog().querySelector("input[role=switch]").getClientRects().length, "Approval stays hidden until the plan is reviewed");
    button("inspect", dialog()).click(); await until(() => dialog()?.querySelector("input[role=switch]")?.closest("div")?.hidden === false, "inspected deployment plan");
    const trust = dialog().querySelector("input[role=switch]");
    check(button("install", dialog()).disabled && !trust.checked, "Runtime installation requires explicit approval");
    check(dialog().textContent.includes("SHA-256:") && dialog().textContent.includes("https://weights.example.test/model.gguf"), "Review displays immutable hashes and sources");
    trust.checked = true; trust.dispatchEvent(new Event("change", { bubbles: true }));
    check(!button("install", dialog()).disabled, "Valid approved plan enables install");
    const rect = dialog().getBoundingClientRect(), body = dialog().querySelector(".mdo-layer-body");
    check(rect.left >= -1 && rect.right <= innerWidth + 1 && body.scrollWidth <= body.clientWidth + 1, "Dialog fits desktop and narrow screens");
    const content = host.closest(".settings-content");
    check(host.scrollWidth <= host.clientWidth + 1 && content.scrollWidth <= content.clientWidth + 1, "Settings page has no horizontal overflow");
    check(!dialog().textContent.includes("localModels."), "Localized interface resolves all keys");
    check(document.documentElement.lang === params.get("locale"), "Configured language survives model mutations");
    dialog().querySelector(".mdo-layer-controls button:last-child").click(); await until(() => !dialog(), "review window closed");
    result.textContent = JSON.stringify({ passed: true, checks, locale: params.get("locale"), width: innerWidth });
  } catch (error) {
    result.textContent = JSON.stringify({ passed: false, checks, error: String(error.stack || error), body: document.body.innerText.slice(-1500) });
  }
})();
