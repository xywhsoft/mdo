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
    const { loadLocale, t } = await import("/js/i18n.js");
    const params = new URL(location.href).searchParams;
    await until(() => document.documentElement.dataset.mdoConfiguredLocale, "application locale settled");
    // The application's first locale initialization may still be committing.
    // Re-read its guard rather than making an unconditional test-only write.
    for (let attempt = 0; ; attempt++) {
      const settings = await api.get("/settings");
      try { await api.patch("/settings/settings", { schema_version: 1, patch: { locale: params.get("locale") } }, { ifMatch: settings.etag }); break; }
      catch (error) { if (![409, 412].includes(error.status) || attempt >= 4) throw error; }
    }
    await loadLocale(params.get("locale") || "en-US");
    check(document.documentElement.lang === params.get("locale"), "Requested interface language applied");
    const tr = key => t(`localModels.${key}`);
    const { createLocalModelPanel } = await import("/js/features/settings/local-models.js");
    const host = document.createElement("section");
    host.style.cssText = "position:fixed;inset:0;z-index:9000;background:var(--bg);padding:20px;overflow:auto;box-sizing:border-box";
    document.body.append(host); const panel = createLocalModelPanel(host); panel.selectSection("local-models");
    const button = (key, root = host) => [...root.querySelectorAll("button")].find(node => node.textContent === tr(key));
    const dialog = () => [...document.querySelectorAll("dialog[open]")].at(-1);
    await until(() => host.querySelector(".local-model-card"), "installed model list");
    check(host.textContent.includes(tr("state.installed")), "Installed receipt shown");
    if (params.get("qa_services") === "1") {
      button("createDeployment").click(); await until(() => dialog()?.querySelector("[name=context_tokens]"), "native deployment options loaded");
      const ctx = dialog().querySelector("[name=context_tokens]"); ctx.value = "512";
      button("confirm", dialog()).click(); await until(() => dialog()?.querySelector("[role=status]")?.textContent && !button("confirm", dialog()).disabled, "invalid deployment rejected");
      check(ctx.value === "512", "Failed deployment save retains parameter edits"); ctx.value = "8192";
      button("confirm", dialog()).click(); await until(() => !dialog(), "deployment created");
      await until(() => button("start"), "stopped deployment rendered"); check(!button("chat"), "Stopped deployment does not offer an unusable chat action");
      button("start").click(); await until(() => button("verify"), "deployment reached ready");
      button("verify").click(); await until(() => dialog()?.textContent.includes(tr("verifiedTools")), "native streaming and tools verification complete");
      await dialog().querySelector('[aria-label]').closest("dialog").querySelector(".mdo-layer-controls button:last-child").click(); await until(() => !dialog(), "verification dialog closed");
      check(!!button("chat"), "Ready model offers mdo conversation entry");
      button("web").click(); await until(() => dialog()?.querySelector("input[type=password]"), "runtime credentials loaded only on demand");
      const key = dialog().querySelector("input[type=password]").value;
      check(key.length >= 32 && !JSON.stringify((await api.get("/local-models")).data).includes(key), "Temporary API key stays out of registry snapshots");
      dialog().querySelector(".mdo-layer-controls button:last-child").click(); await until(() => !dialog(), "web dialog closed");
      button("stop").click(); await until(() => button("configure"), "deployment stopped without refresh");
      button("configure").click(); await until(() => dialog()?.querySelector("[name=output_tokens]"), "deployment editor");
      dialog().querySelector("[name=output_tokens]").value = "1024"; button("confirm", dialog()).click(); await until(() => !dialog(), "edited deployment saved");
      await until(() => button("rollback"), "previous configuration available"); button("rollback").click(); button("confirm", dialog()).click(); await until(() => !dialog(), "configuration rollback finished");
      button("deleteDeployment").click(); button("confirm", dialog()).click(); await until(() => !dialog() && !button("start"), "deployment deleted while keeping installation");
      check(!!button("createDeployment"), "Model resources remain reusable after deleting deployment");
    }
    button("import").click(); await until(() => dialog()?.querySelector("[name=path]"), "import form");
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
    await until(() => [...host.querySelectorAll("article")].some(row => row.textContent.includes("Browser reference") && button("uninstall", row)), "completed reference rendered");
    const reference = [...host.querySelectorAll("article")].find(row => row.textContent.includes("Browser reference"));
    button("uninstall", reference).click(); await until(() => dialog()?.querySelector("input[role=switch]"), "uninstall options");
    check(dialog().textContent.includes(tr("removeNotice")), "Ownership explained before removal");
    button("confirm", dialog()).click(); await until(() => !dialog(), "reference uninstalled");
    button("store").click(); button("local").click(); button("store").click();
    await until(() => button("details"), "store survives fast tab switching");
    check(host.textContent.includes("1.0 MiB"), "Catalog weight size rendered");
    check(button("previous").disabled && button("next").disabled, "Single-page navigation disabled");
    button("details").click(); await until(() => dialog()?.querySelector("select"), "deployment profiles");
    check(dialog().textContent.includes(tr("ram")) && dialog().textContent.includes("llama.cpp"), "Profile resources and fixed runtime shown");
    button("inspect", dialog()).click(); await until(() => dialog()?.querySelector("input[role=switch]")?.closest("div")?.hidden === false, "inspected deployment plan");
    const trust = dialog().querySelector("input[role=switch]");
    check(button("install", dialog()).disabled && !trust.checked, "Runtime installation requires explicit approval");
    check(dialog().textContent.includes("SHA-256:") && dialog().textContent.includes("https://weights.example.test/model.gguf"), "Review displays immutable hashes and sources");
    trust.checked = true; trust.dispatchEvent(new Event("change", { bubbles: true }));
    check(!button("install", dialog()).disabled, "Valid approved plan enables install");
    const rect = dialog().getBoundingClientRect(), body = dialog().querySelector(".mdo-layer-body");
    check(rect.left >= -1 && rect.right <= innerWidth + 1 && body.scrollWidth <= body.clientWidth + 1, "Dialog fits desktop and narrow screens");
    check(host.scrollWidth <= host.clientWidth + 1, "Page has no horizontal overflow");
    check(!dialog().textContent.includes("localModels."), "Localized interface resolves all keys");
    check(document.documentElement.lang === params.get("locale"), "Configured language survives model mutations");
    result.textContent = JSON.stringify({ passed: true, checks, locale: params.get("locale"), width: innerWidth });
  } catch (error) {
    result.textContent = JSON.stringify({ passed: false, checks, error: String(error.stack || error), body: document.body.innerText.slice(-1500) });
  }
})();
