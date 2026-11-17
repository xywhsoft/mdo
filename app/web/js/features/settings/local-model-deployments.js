import { element, errorMessage } from "../../utils/dom.js";
import { copyText } from "../../utils/clipboard.js";
import { createApplicationClient } from "../../api/application.js";
import { isRemoteTarget } from "../../api/target.js";
import { createSession } from "../../state/sessions.js";
import { loadModels } from "../../state/catalogs.js";
import { navigation } from "../../state/navigation.js";

// The native manager owns paths, credentials and process identity. This view
// submits deployment parameters, never command lines or saved loopback URLs.
export function createLocalModelDeployments({ tr, bytes, button, layer, field, toggle, call, load, snapshot, current }) {
  const numberFields = ["context_tokens", "max_output_tokens", "output_tokens", "parallel", "gpu_layers", "threads", "idle_seconds"];
  const switches = ["auto_start", "auto_start_on_use", "allow_uncertain_resources"];
  let pending = 0;
  function wrapped(run) { return async () => { const valid = current(); pending++; try { await run(valid); } finally { pending--; } }; }
  async function command(body, valid) { const data = await call(body); if (!valid()) return null; await loadModels(); if (valid()) await load(); return data; }
  function actions(card, row) {
    const bar = element("div", { className: "extension-toolbar" });
    const running = ["starting", "loading", "ready", "stopping"].includes(row.service_state);
    const proof = row.verification?.fingerprint === row.fingerprint ? row.verification : null;
    card.append(element("h4", { text: row.name }), element("p", { text: `${tr(`service.${row.service_state}`)} · ${row.runtime.engine} ${row.runtime.build} · ${row.runtime.backend}` }),
      element("p", { text: `${tr("context")}: ${row.config.context_tokens.toLocaleString()} · ${tr("parallel")}: ${row.config.parallel} · ${tr("activeRequests")}: ${row.active_requests || 0}` }),
      element("p", { text: proof?.reply ? tr(proof.tools ? "verifiedTools" : "verifiedReply") : tr("unverified") }));
    if (row.service_error) card.append(element("p", { className: "extension-error", text: row.service_error }));
    const act = (key, body) => button(key, wrapped(async valid => { try { await command(body, valid); } catch (e) { if (valid()) showError(e); } }));
    if (running) {
      bar.append(act("stop", { action: "stop", id: row.id }));
      bar.append(button("forceStop", () => confirm("forceStopNotice", { action: "stop", id: row.id, force: true })));
    } else {
      bar.append(row.runtime.backend === "cpu" || row.config.allow_uncertain_resources ? act("start", { action: "start", id: row.id }) : button("start", () => confirm("resourceAttemptNotice", {action:"start",id:row.id,allow_unknown:true})));
      bar.append(button("configure", () => edit(row.installation, row)), button("duplicate", () => edit(row.installation, row, true)));
      if (snapshot().installations[row.installation]?.recipe_ref) bar.append(button("switchVersion", () => switchVersion(row)));
      if (row.can_rollback) bar.append(button("rollback", () => confirm("rollbackNotice", { action: "deployment_rollback", id: row.id, deployment_revision: row.revision })));
      bar.append(button("deleteDeployment", () => confirm("deleteDeploymentNotice", { action: "deployment_delete", id: row.id, deployment_revision: row.revision })));
    }
    if (row.service_state === "ready") {
      bar.append(button("verify", wrapped(async valid => { const view = layer(tr("verify")), text = element("p", { text: tr("verifying"), attrs: { role: "status" } }); view.body.append(text); view.update({ beforeClose: () => !text.dataset.busy }); text.dataset.busy = "1";
        try { const result = await command({ action: "verify", id: row.id }, valid); if (valid() && result) text.textContent = result.verified ? tr(result.tools ? "verifiedTools" : "verifiedReply") : result.error || tr("unverified"); }
        catch (e) { if (valid()) text.textContent = errorMessage(e); } finally { delete text.dataset.busy; } })), button("chat", wrapped(async valid => {
        try { await loadModels(); if (!valid()) return; const session = await createSession({ project_id: navigation.workspace().projectId || "default", model_id: "local-model." + row.id, title: row.name }); if (valid()) navigation.select(session.project_id, session.id); }
        catch (e) { if (valid()) showError(e); }
      })), button("web", () => web(row)));
    }
    bar.append(button("logs", () => logs(row))); card.append(bar);
  }
  function switchVersion(row) {
    const installed = snapshot().installations, ref = installed[row.installation]?.recipe_ref;
    const choices = Object.values(installed).filter(j => j.state === "installed" && j.id !== row.installation && j.recipe_ref === ref);
    const view = layer(tr("switchVersion")), text = element("p", { text:tr(choices.length ? "switchNotice" : "noAlternative"), attrs:{role:"status"} }); view.body.append(text); if (!choices.length) return;
    const select = element("select", { attrs:{"aria-label":tr("switchVersion")} }); for (const j of choices) select.append(element("option", { text:`${j.name} · ${j.profile_id} · ${j.recipe_version_id}`, attrs:{value:j.id} })); view.body.append(select); let busy = false; view.update({ beforeClose: () => !busy });
    const save = button("confirm", wrapped(async valid => { if (busy) return; busy = true; save.disabled = true; try { await command({ action:"deployment_switch", id:row.id, deployment_revision:row.revision, installation:select.value, name:row.name }, valid); if (valid()) view.dispose(); } catch(e) { if (valid()) text.textContent = errorMessage(e); } finally { busy = false; save.disabled = false; } })); view.footer.append(button("cancel", () => view.close()), save);
  }
  function showError(e) { const view = layer(tr("operationFailed")); view.body.append(element("p", { className: "extension-error", text: errorMessage(e) })); }
  function confirm(key, body) {
    const view = layer(tr(key)), text = element("p", { text: tr(key), attrs: { role: "status" } }); view.body.append(text); let busy = false; view.update({ beforeClose: () => !busy });
    const save = button("confirm", wrapped(async valid => { if (busy) return; busy = true; save.disabled = true; try { await command(body, valid); if (valid()) view.dispose(); } catch (e) { if (valid()) text.textContent = errorMessage(e); } finally { busy = false; save.disabled = false; } })); view.footer.append(button("cancel", () => view.close()), save);
  }
  async function edit(installation, previous, duplicate = false) {
    const valid = current(), view = layer(tr(previous && !duplicate ? "configure" : "createDeployment")), status = element("p", { attrs: { role: "status" }, text: tr("loading") }); view.body.append(status); let busy = true; view.update({ beforeClose: () => !busy });
    try {
      const options = await call({ action: "deployment_options", installation }); if (!valid()) return;
      const config = previous?.config || options.config, form = element("form", { className: "extension-settings-form local-model-config" }), name = field(form, "name", previous?.name || snapshot().installations[installation]?.name || ""); name.required = true;
      let runtime, backend;
      if (options.imported) {
        const label = element("label", { className: "extension-field" }); runtime = element("select", { attrs: { name: "runtime_ref" } });
        for (const row of options.runtimes || []) if (row.installation?.version_id && row.id?.startsWith("external-tool:")) runtime.append(element("option", { text: `${row.name || row.id} · ${row.version || ""}`, attrs: { value: row.id } }));
        if (previous) runtime.value = previous.runtime.ref;
        label.append(element("span", { text: tr("runtime") }), runtime); form.append(label);
        backend = choice(form, "backend", ["cpu", "vulkan", "cuda", "metal"], previous?.runtime.backend || "cpu");
        form.append(element("p", { text: tr("importRuntimeNotice") }));
      }
      const controls = {};
      for (const key of numberFields) { const input = controls[key] = field(form, key, String(config[key])); input.type = "number"; input.required = true; input.step = "1"; input.min = key === "gpu_layers" ? "-1" : key === "context_tokens" ? "512" : ["threads", "idle_seconds"].includes(key) ? "0" : "1"; input.max = String(({ context_tokens:1048576, max_output_tokens:1048575, output_tokens:1048575, parallel:64, gpu_layers:100000, threads:256, idle_seconds:604800 })[key]); }
      controls.reasoning_mode = choice(form, "reasoning_mode", ["default", "off", "on"], config.reasoning_mode);
      for (const key of switches) controls[key] = toggle(form, key, !!config[key]);
      const tools = toggle(form, "toolsCapability", (previous?.capabilities || options.capabilities || []).includes("tools"));
      form.append(element("p", { text: tr("configNotice") })); view.body.append(form);
      if (options.requirements) view.body.append(element("p", { text: `${tr("ram")}: ${bytes(options.requirements.ram_bytes)} · ${tr("vram")}: ${bytes(options.requirements.vram_bytes)} · ${tr("profileEstimate")}` }));
      const save = button("confirm", wrapped(async stillValid => {
        if (busy || !form.reportValidity()) return; busy = true; save.disabled = true; form.inert = true; status.textContent = tr("saving");
        try { const effective = { ...config }; for (const key of numberFields) effective[key] = Number(controls[key].value); for (const key of switches) effective[key] = controls[key].checked; effective.reasoning_mode = controls.reasoning_mode.value;
          const capabilities = (previous?.capabilities || options.capabilities || ["text"]).filter(c => c !== "tools"); if (tools.checked) capabilities.push("tools");
          const body = { action: previous && !duplicate ? "deployment_update" : "deployment_create", name: name.value.trim(), config: effective, capabilities };
          if (previous && !duplicate) Object.assign(body, { id: previous.id, deployment_revision: previous.revision }); else Object.assign(body, { installation, ...(runtime ? { runtime_ref:runtime.value, backend:backend.value } : {}) });
          await command(body, stillValid); if (stillValid()) view.dispose();
        } catch (e) { if (stillValid()) status.textContent = errorMessage(e); } finally { busy = false; save.disabled = false; form.inert = false; }
      })); form.addEventListener("submit", e => { e.preventDefault(); save.click(); }); view.footer.append(button("cancel", () => view.close()), save); status.textContent = "";
    } catch (e) { if (valid()) status.textContent = errorMessage(e); } finally { busy = false; }
  }
  function choice(form, key, values, value) { const label = element("label", { className: "extension-field" }), select = element("select", { attrs: { name:key } }); for (const v of values) select.append(element("option", { text: tr(`${key}.${v}`), attrs: { value:v } })); select.value = value; label.append(element("span", { text: tr(key) }), select); form.append(label); return select; }
  async function logs(row) { const valid = current(), view = layer(tr("logs")), text = element("pre", { className: "local-model-requirements" }); view.body.append(text); const refresh = button("refresh", async () => { try { const data = await call({ action:"logs", id:row.id }); if (valid()) text.textContent = data.text || tr("noLogs"); } catch(e) { if (valid()) text.textContent = errorMessage(e); } }); view.footer.append(refresh); refresh.click(); }
  async function web(row) {
    let access, key; const valid = current(), view = layer(tr("web"), () => { if (key) key.value = ""; access = null; }), status = element("p", { text: tr("webNotice"), attrs: { role:"status" } }); view.body.append(status);
    try { access = await call({ action:"web_access", id:row.id }); if (!valid()) return;
      if (!/^http:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}$/.test(access.url)) throw Error(tr("operationFailed"));
      key = element("input", { attrs: { type:"password", readonly:"", autocomplete:"off", "aria-label":tr("temporaryKey") } }); key.value = access.key; view.body.append(key);
      view.footer.append(button("copyKey", async () => { try { await copyText(key.value); status.textContent = tr("keyCopied"); } catch(e) { status.textContent = errorMessage(e); } }), button(isRemoteTarget() ? "openTargetWeb" : "openWeb", async () => { try { if (valid()) await createApplicationClient().call("system.open", { target:access.url }); } catch(e) { if (valid()) status.textContent = errorMessage(e); } }));
    } catch(e) { if (valid()) status.textContent = errorMessage(e); }
  }
  return { actions, edit, hasPendingChanges: () => pending > 0 };
}
