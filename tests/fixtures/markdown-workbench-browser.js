(async () => {
  const checks = {}, errors = []; let out;
  window.addEventListener("error", event => errors.push(event.message));
  const wait = async check => { for (let i = 0; i < 300; i++) { if (check()) return; await new Promise(resolve => setTimeout(resolve, 30)); } throw Error("Workbench condition timed out"); };
  try {
    await wait(() => globalThis.mdo?.ui?.openFile);
    out = document.createElement("pre"); out.id = "result"; out.hidden = true; document.body.append(out);
    const owner = await (await fetch("/__qa/info")).json();
    const { openMarkdownFile, markdownAddress } = await import("/js/features/chat/markdown-assets.js");
    await openMarkdownFile(markdownAddress(owner.file + ":3"), owner);
    await wait(() => [...document.querySelectorAll('.wb-source-line[data-line="3"]')].some(node => node.classList.contains("wb-source-selected")));
    checks.selectedThirdLine = [...document.querySelectorAll('.wb-source-line[data-line="3"]')].some(node => node.textContent.includes("third"));
    const count = document.querySelectorAll('.wb-tab').length;
    await openMarkdownFile(markdownAddress(owner.file + "#L2"), owner);
    await wait(() => [...document.querySelectorAll('.wb-source-line[data-line="2"]')].some(node => node.classList.contains("wb-source-selected")));
    checks.reusesFileTab = document.querySelectorAll('.wb-tab').length === count;
    checks.noErrors = errors.length === 0;
    out.textContent = JSON.stringify({ passed: Object.values(checks).every(Boolean), checks, errors });
  } catch (error) {
    if (!out) { out = document.createElement("pre"); out.id = "result"; document.body.append(out); }
    out.textContent = JSON.stringify({ passed: false, checks, errors, error: String(error.stack) });
  }
})();
