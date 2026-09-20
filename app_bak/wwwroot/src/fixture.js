// fixture.js — 本地演示驱动：脚本化事件流（对应 dsh 内捆的 fixture.js 角色）
// 所有内容经 Store 走真实事件管线（日志 → 折叠器 → 帧），不绕过任何一层。
//
// 本版新增演示能力：
//   · 多模态回合（用户贴图 → 分析 → 出图）
//   · todo 工具 + tool/update 实时勾选推进
//   · task 子代理（嵌套步骤进度）
//   · fetch / write / glob 工具
//   · 错误 → 自我修正回合
//   · retry / editUserMessage（截断重放分支）
//   · effort（思考力度）影响 reasoning 流：off 不流、low 截短

const rnd = (min, max) => min + Math.random() * (max - min);

// —— mock 图表（内联 SVG data URI，离线可跑）——
function chartDataUrl({ title, bars, unit = 's', w = 340, h = 190 }) {
  const max = Math.max(...bars.map((b) => b.value)) * 1.15;
  const padL = 34, padB = 26, padT = 30;
  const bw = (w - padL - 14) / bars.length * 0.55;
  const step = (w - padL - 14) / bars.length;
  const barEls = bars.map((b, i) => {
    const bh = (h - padT - padB) * (b.value / max);
    const x = padL + i * step + (step - bw) / 2;
    const y = h - padB - bh;
    const lbl = `<text x="${x + bw / 2}" y="${h - 10}" font-size="10" fill="#8b929c" text-anchor="middle">${b.label}</text>
      <text x="${x + bw / 2}" y="${y - 5}" font-size="10" fill="#d9dde2" text-anchor="middle">${b.value}${unit}</text>`;
    return `<rect x="${x}" y="${y}" width="${bw}" height="${bh}" rx="2" fill="${b.color ?? '#6cb0ec'}" opacity="0.9"/>${lbl}`;
  }).join('');
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">
    <rect width="100%" height="100%" fill="#1b1e22" rx="8"/>
    <text x="${padL}" y="18" font-size="11" fill="#d9dde2" font-family="sans-serif">${title}</text>
    <line x1="${padL}" y1="${h - padB}" x2="${w - 8}" y2="${h - padB}" stroke="#3a4048"/>
    ${barEls}</svg>`;
  return 'data:image/svg+xml;base64,' + btoa(unescape(encodeURIComponent(svg)));
}

const CHART_TREND = () => chartDataUrl({
  title: '近 4 次构建耗时', bars: [
    { label: '#41', value: 12.1, color: '#57ab5a' },
    { label: '#42', value: 18.4, color: '#d29922' },
    { label: '#43', value: 27.0, color: '#e5534b' },
    { label: '#44', value: 42.3, color: '#e5534b' },
  ],
});
const CHART_AFTER = () => chartDataUrl({
  title: '优化前后对比', bars: [
    { label: '优化前', value: 42.3, color: '#e5534b' },
    { label: '优化后', value: 9.1, color: '#57ab5a' },
  ],
});

export function installScripts(host, store) {
  const S = () => host.scale;
  const d = (ms) => new Promise((r) => setTimeout(r, ms * S()));
  const cancelled = (sid) => host.isCancelled(sid);

  let callSeq = 0;
  const nextCallId = () => 'call-' + (++callSeq);

  // —— 思考力度：off 不流 reasoning；low 取首句 ——
  function reasonText(t) {
    if (host.effort === 'off') return '';
    if (host.effort === 'low') {
      const m = t.match(/^[^。！？]*[。！？]?/);
      return (m ? m[0] : t.slice(0, 40)) + '（低思考模式）';
    }
    return t;
  }

  // —— 流式原语：reasoning（非表面，无需落账）——
  async function streamReasoning(sid, text) {
    const t = reasonText(text);
    if (!t) return true;
    return streamInto(sid, 'assistant/reasoning', t);
  }

  // —— 流式正文：chunk 预览 + assistant/message 权威落账（修复：原版缺落账，表面投影不全）——
  async function streamText(sid, text, { chunk = [2, 5], gap = [14, 30], extraBlocks = [] } = {}) {
    const model = store.model?.id ?? 'fixture/fx-1';
    if (host.instant) {
      store.append(sid, 'assistant/message', {
        message: { model, content: [{ type: 'text', text }, ...extraBlocks] },
      });
      await d(40);
      return true;
    }
    let i = 0;
    while (i < text.length) {
      if (cancelled(sid)) return false;
      const n = Math.floor(rnd(chunk[0], chunk[1]));
      store.append(sid, 'assistant/chunk', { delta: text.slice(i, i + n) });
      i += n;
      await d(rnd(gap[0], gap[1]));
    }
    store.append(sid, 'assistant/message', {
      message: { model, content: [{ type: 'text', text }, ...extraBlocks] },
    });
    return true;
  }

  async function streamInto(sid, type, text, { chunk = [2, 5], gap = [14, 30] } = {}) {
    if (host.instant) { // 整块落账：跳过逐字动画
      store.append(sid, type, { delta: text });
      await d(40);
      return true;
    }
    let i = 0;
    while (i < text.length) {
      if (cancelled(sid)) return false;
      const n = Math.floor(rnd(chunk[0], chunk[1]));
      store.append(sid, type, { delta: text.slice(i, i + n) });
      i += n;
      await d(rnd(gap[0], gap[1]));
    }
    return true;
  }

  // —— 工具调用原语：assistant/message(含 tool-call 块) → tool/result ——
  async function tool(sid, spec) {
    const callId = spec.callId ?? nextCallId();
    store.append(sid, 'assistant/message', {
      message: {
        model: store.model?.id ?? 'fixture/fx-1',
        content: [{ type: 'tool-call', callId, name: spec.name, input: spec.input }],
      },
    });
    await d(host.instant ? 60 : rnd(350, 800));
    if (cancelled(sid)) {
      store.append(sid, 'tool/result', { callId, output: 'cancelled', ms: 1, isError: true });
      return { callId, aborted: true };
    }
    store.append(sid, 'tool/result', {
      callId, output: spec.output, ms: spec.ms ?? Math.round(rnd(200, 3000)), isError: !!spec.isError, plan: spec.plan,
    });
    await d(rnd(120, 300));
    return { callId, aborted: false };
  }

  // —— todo 工具：建立 + tool/update 实时勾选推进 ——
  async function todoCreate(sid, items) {
    const r = await tool(sid, {
      name: 'todo', input: { items: items.length },
      output: `已建立 ${items.length} 项计划`, ms: 30,
      plan: items.map((text) => ({ text, done: false })),
    });
    return r;
  }
  async function todoProgress(sid, callId, items, doneCount) {
    store.append(sid, 'tool/update', {
      callId, patch: { plan: items.map((text, i) => ({ text, done: i < doneCount })), doneCount },
    });
    await d(200);
  }

  // —— task 子代理：嵌套步骤进度 + 汇总 ——
  async function taskTool(sid, spec) {
    const callId = nextCallId();
    store.append(sid, 'assistant/message', {
      message: {
        model: store.model?.id ?? 'fixture/fx-1',
        content: [{ type: 'tool-call', callId, name: 'task', input: { goal: spec.goal } }],
      },
    });
    const sub = spec.steps.map((s) => ({ name: s, status: 'pending' }));
    for (let i = 0; i < sub.length; i++) {
      sub[i].status = 'running';
      store.append(sid, 'tool/update', { callId, patch: { sub: sub.map((x) => ({ ...x })) } });
      await d(rnd(500, 1100));
      sub[i].status = 'ok';
      store.append(sid, 'tool/update', { callId, patch: { sub: sub.map((x) => ({ ...x })) } });
      if (cancelled(sid)) {
        store.append(sid, 'tool/result', { callId, output: '子代理已中断', ms: 1, isError: true });
        return { callId, aborted: true };
      }
    }
    store.append(sid, 'tool/result', { callId, output: spec.output, ms: spec.ms ?? 2400 });
    await d(150);
    return { callId, aborted: false };
  }

  function begin(sid, title) {
    host.clearCancel(sid);
    store.append(sid, 'session/running', { value: true });
    if (title) store.append(sid, 'session/title', { title });
  }

  function end(sid, stats) {
    if (stats) store.append(sid, 'session/stats', stats);
    store.append(sid, 'session/running', { value: false });
  }

  async function abortPath(sid) {
    store.append(sid, 'session/interrupted', {});
    end(sid, { promptTokens: 8210, completionTokens: 742, ms: 6400, tps: 116, ctxTokens: (store.sessions.get(sid)?.ctxTokens ?? 0) + 8952 });
    return false;
  }

  // ============================================================
  // 完整演示：构建脚本优化（多工具 + 审批闸门 + diff + 汇总）
  // ============================================================
  host.runShowcase = async function (sid) {
    const s = store.sessions.get(sid);
    if (!s || s.running) return;
    let ctx = 0;
    begin(sid, '构建脚本优化');

    store.append(sid, 'user/message', {
      content: [{ type: 'text', text: '帮我看看这个项目的构建为什么慢，找到瓶颈后给出方案，并直接实施改动。' }],
    });
    ctx += 96;
    await d(500);

    if (!(await streamReasoning(sid, '用户要的是定位瓶颈并直接改。先看构建入口和依赖，再实测一次耗时拿到基线，避免凭感觉优化。'))) return abortPath(sid);
    await d(300);

    let r = await tool(sid, {
      name: 'read', input: { path: 'package.json' },
      output: `{\n  "name": "widget-ui",\n  "scripts": {\n    "build": "node scripts/build.mjs"\n  },\n  "devDependencies": {\n    "esbuild": "^0.24.0",\n    "@babel/core": "^7.25.0",\n    "typescript": "^5.6.0"\n  }\n}`,
      ms: 42,
    });
    if (r.aborted) return abortPath(sid);
    ctx += 210;

    r = await tool(sid, {
      name: 'bash', input: { command: 'time pnpm build' },
      output: `$ pnpm build\n\n> widget-ui@0.3.1 build\n> node scripts/build.mjs\n\n[babel] transform src/** (412 files) … 38.1s\n[esbuild] bundle dist/app.js … 2.2s\n[typecheck] tsc --noEmit … 1.9s\n\nreal\t42.3s\nuser\t39.8s\nsys\t 2.1s`,
      ms: 42300,
    });
    if (r.aborted) return abortPath(sid);
    ctx += 860;

    if (!(await streamReasoning(sid, '42 秒里 38 秒花在 Babel 全量转译，而项目已经在用 esbuild 打包——Babel 那一趟是历史遗留，目标浏览器早就不用管了。确认一下 scripts/build.mjs 里是否真的还在串行跑 Babel。'))) return abortPath(sid);

    r = await tool(sid, {
      name: 'search', input: { pattern: 'babel|transformAst|sourceMap', path: 'scripts/' },
      output: `scripts/build.mjs:18:  await babelTransformDir('src', '.tmp/babel')\nscripts/build.mjs:23:  const keep = await glob('.tmp/babel/**/*.js')\nscripts/build.mjs:31:  sourcemap: true,\n1 个文件，3 处匹配`,
      ms: 260,
    });
    if (r.aborted) return abortPath(sid);
    ctx += 190;

    if (!(await streamInto(sid, 'assistant/chunk', '瓶颈很明确：`build.mjs` 在 esbuild 之前先对 `src/` 做了一次全量 Babel 转译（412 个文件、38 秒），esbuild 自己 2 秒就打完了。这趟 Babel 是给旧浏览器准备的兼容层，按 package.json 的 browserslist 早已不需要。我的方案是删掉这一趟，直接让 esbuild 接管转译。改动如下。'))) return abortPath(sid);
    store.append(sid, 'assistant/message', {
      message: { model: store.model?.id ?? 'fixture/fx-1', content: [{ type: 'text', text: '瓶颈很明确：`build.mjs` 在 esbuild 之前先对 `src/` 做了一次全量 Babel 转译（412 个文件、38 秒），esbuild 自己 2 秒就打完了。这趟 Babel 是给旧浏览器准备的兼容层，按 package.json 的 browserslist 早已不需要。我的方案是删掉这一趟，直接让 esbuild 接管转译。改动如下。' }] },
    });
    await d(400);

    // —— 审批闸门 ——
    const decision = await host.awaitApproval(sid, {
      id: 'ap-' + Date.now(),
      kind: 'file-write',
      title: '修改文件 scripts/build.mjs',
      detail: '移除 Babel 全量转译 · +3 −5 行',
    });
    if (decision === 'rejected') {
      if (!(await streamText(sid, '好，改动先不动。上面的分析仍然成立：只要删掉 `build.mjs` 第 18 行起的 Babel 段，构建时间就能从 42s 降到 10s 以内。需要我继续时说一声。'))) return abortPath(sid);
      end(sid, { promptTokens: 9640, completionTokens: 1180, ms: 21800, tps: 198, ctxTokens: ctx + 10820 });
      return true;
    }

    r = await tool(sid, {
      name: 'edit', input: { path: 'scripts/build.mjs' },
      output: '已写入 scripts/build.mjs（1 个文件，+3 −5）',
      ms: 38,
      diff: [
        ' import { build } from \'esbuild\'',
        '-import { transformDir } from \'./babel-pass.mjs\'',
        '-',
        '-// 旧浏览器兼容层：esbuild 之前先全量转译',
        '-await transformDir(\'src\', \'.tmp/babel\')',
        '-',
        ' const keep = await glob(\'src/**/*.ts\')',
        '+// esbuild 直接接管转译（target 见下）',
        '+const target = \'es2020\'',
        ' await build({',
        '   entryPoints: [...keep],',
        '+  target,',
        '+  sourcemap: true,',
        '   outdir: \'dist\',',
      ],
    });
    if (r.aborted) return abortPath(sid);
    ctx += 240;

    r = await tool(sid, {
      name: 'bash', input: { command: 'time pnpm build' },
      output: `$ pnpm build\n\n> widget-ui@0.3.1 build\n> node scripts/build.mjs\n\n[esbuild] bundle dist/app.js (target es2020) … 6.8s\n[typecheck] tsc --noEmit … 1.9s\n\nreal\t9.1s\nuser\t 8.3s\nsys\t 0.9s`,
      ms: 9100,
    });
    if (r.aborted) return abortPath(sid);
    ctx += 520;

    r = await tool(sid, {
      name: 'plan', input: { items: 3 },
      output: '计划完成',
      ms: 90,
      plan: [
        { text: '定位构建耗时瓶颈（Babel 全量转译 38s）', done: true },
        { text: '移除 Babel 段，esbuild 接管转译', done: true },
        { text: '复测构建时间并回归产物体积', done: true },
      ],
    });
    if (r.aborted) return abortPath(sid);

    await d(300);
    const summary = [
      '## 结论',
      '',
      '构建时间 **42.3s → 9.1s**（约 4.6 倍）。瓶颈是 `build.mjs` 里在 esbuild 之前串行跑的一趟 Babel 全量转译——412 个文件耗掉 38 秒，而它的产出只是给早已退场的旧浏览器做语法降级。',
      '',
      '| 阶段 | 改动前 | 改动后 |',
      '| --- | --- | --- |',
      '| Babel 全量转译 | 38.1s | 移除 |',
      '| esbuild 打包 | 2.2s | 6.8s（接管转译） |',
      '| 类型检查 | 1.9s | 1.9s |',
      '| **合计** | **42.3s** | **9.1s** |',
      '',
      '改动内容：',
      '',
      '- 删除 `build.mjs` 第 14–24 行的 Babel 转译与 `.tmp/babel` 中转目录',
      '- esbuild 增加 `target: \'es2020\'` 与 `sourcemap: true`，行为对齐原产物',
      '- `package.json` 的 `@babel/core` 等依赖可顺手移除（未动，留给你确认）',
      '',
      '产物体积基本持平（±2KB），sourcemap 从无到有。回归命令：',
      '',
      '```bash',
      'pnpm build && pnpm test --filter widget-ui',
      '```',
      '',
      '如果 CI 也要提速，下一步可以把 `tsc --noEmit` 挪到独立 job 与打包并行，还能再省约 2 秒。',
    ].join('\n');
    if (!(await streamText(sid, summary, { chunk: [3, 7], gap: [10, 22] }))) return abortPath(sid);

    end(sid, { promptTokens: 12840, completionTokens: 2360, ms: 41200, tps: 336, ctxTokens: ctx + 15200 });
    return true;
  };

  // ============================================================
  // 多模态演示：贴图分析 + todo 实时推进 + fetch/write + 子代理 + 出图
  // ============================================================
  host.runMultimodal = async function (sid) {
    const s = store.sessions.get(sid);
    if (!s || s.running) return;
    let ctx = 0;
    begin(sid, '构建耗时趋势分析');

    store.append(sid, 'user/message', {
      content: [
        { type: 'text', text: '这是最近四次构建的耗时记录（截图），帮我分析趋势、定位原因，能自动改的直接改掉。' },
        { type: 'image', dataUrl: CHART_TREND(), name: 'build-times.svg' },
      ],
    });
    ctx += 420;
    await d(500);

    if (!(await streamReasoning(sid, '图上趋势是单调恶化：12.1 → 18.4 → 27.0 → 42.3 秒，四周翻了 3.5 倍。这种"线性变慢"不像缓存失效，更像源文件规模增长叠加了冗余转译层。先立计划，再逐项取证据。'))) return abortPath(sid);

    const TODO = ['读取构建配置与依赖清单', '定位耗时增长的根因', '实施修改并复测构建', '回归产物体积与 sourcemap'];
    let r = await todoCreate(sid, TODO);
    if (r.aborted) return abortPath(sid);
    const todoCall = r.callId;
    ctx += 160;

    r = await tool(sid, {
      name: 'read', input: { path: 'scripts/build.mjs' },
      output: ` 1  import { build } from 'esbuild'\n 2  import { transformDir } from './babel-pass.mjs'\n…\n17  // 兼容层：esbuild 之前先全量转译\n18  await transformDir('src', '.tmp/babel')\n…\n31  sourcemap: true,`,
      ms: 35,
    });
    if (r.aborted) return abortPath(sid);
    ctx += 240;
    await todoProgress(sid, todoCall, TODO, 1);

    r = await tool(sid, {
      name: 'bash', input: { command: 'pnpm build --dry-run 2>&1 | tail -5 && ls .tmp/babel 2>/dev/null | wc -l' },
      output: `inputs: 412 files (src/**)\ntransform: babel-pass → .tmp/babel\ngit log --oneline src/ | head -3:\n  a91f2c add dashboard module (+86 files)\n  77b0de split utils (+54 files)\n  3e19ac vendor icons (+120 files)\n.tmp/babel: 412 entries`,
      ms: 1200,
    });
    if (r.aborted) return abortPath(sid);
    ctx += 680;
    await todoProgress(sid, todoCall, TODO, 2);

    if (!(await streamReasoning(sid, '证据齐了：三周里 src/ 增加了 260 个文件，而每个文件都要过一遍 Babel 再进 esbuild——双重转译让耗时随规模线性放大。查一下官方对 target 的建议再动手。'))) return abortPath(sid);

    r = await tool(sid, {
      name: 'fetch', input: { url: 'https://esbuild.github.io/api/#target' },
      output: `HTTP 200 · esbuild API 文档\n» target: 传 'es2020' 等环境版本，esbuild 将直接输出对应语法，\n  不再需要外部转译器。默认 esnext。\n结论：项目 browserslist 全绿，可直接接管 Babel 的降级职责。`,
      ms: 860,
    });
    if (r.aborted) return abortPath(sid);
    ctx += 310;

    const decision = await host.awaitApproval(sid, {
      id: 'ap-' + Date.now() + '-m',
      kind: 'file-write',
      title: '修改文件 scripts/build.mjs',
      detail: '移除 Babel 双重转译 · +4 −6 行',
    });
    if (decision === 'rejected') {
      if (!(await streamText(sid, '好，文件先不动。结论供参考：删掉 `build.mjs` 的 Babel 段并给 esbuild 传 `target: \'es2020\'`，耗时可回到 10 秒内。随时可以继续。'))) return abortPath(sid);
      end(sid, { promptTokens: 9080, completionTokens: 990, ms: 15600, tps: 287, ctxTokens: ctx + 10070 });
      return true;
    }

    r = await tool(sid, {
      name: 'write', input: { path: 'scripts/build.mjs' },
      output: '已重写 scripts/build.mjs（+4 −6）',
      ms: 42,
      diff: [
        ' import { build } from \'esbuild\'',
        "-import { transformDir } from './babel-pass.mjs'",
        '-',
        '-// 兼容层：esbuild 之前先全量转译',
        "-await transformDir('src', '.tmp/babel')",
        '-',
        " const keep = await glob('src/**/*.ts')",
        "+// esbuild 直接输出目标语法（文档：#target）",
        "+const target = 'es2020'",
        ' await build({',
        '   entryPoints: [...keep],',
        '+  target,',
        '+  sourcemap: true,',
        "   outdir: 'dist',",
      ],
    });
    if (r.aborted) return abortPath(sid);
    ctx += 260;

    // —— 复测失败 → 自我修正 ——
    r = await tool(sid, {
      name: 'bash', input: { command: 'time pnpm build' },
      output: `[esbuild] bundle dist/app.js (target es2020) … 6.9s\n[typecheck] tsc --noEmit …\nsrc/dashboard/chart.ts(48,11): error TS2551: Property 'flatMap' does not exist on type 'NodeList'.\n  Target es2020 不含 DOM lib 的 flatMap 声明（tsconfig lib 未对齐）\n\nreal\t7.8s（失败）`,
      ms: 7800, isError: true,
    });
    if (r.aborted) return abortPath(sid);
    ctx += 540;

    if (!(await streamReasoning(sid, '转译提速生效了，但 tsconfig 的 lib 还停在 es2018，声明跟不上。补 lib 即可，不动业务代码。'))) return abortPath(sid);

    r = await tool(sid, {
      name: 'edit', input: { path: 'tsconfig.json' },
      output: '已更新 tsconfig.json（+1 −1）',
      ms: 26,
      diff: [
        ' {',
        '   "compilerOptions": {',
        "-    \"lib\": [\"ES2018\", \"DOM\"]",
        "+    \"lib\": [\"ES2020\", \"DOM\"]",
        '   }',
      ],
    });
    if (r.aborted) return abortPath(sid);
    ctx += 130;

    r = await tool(sid, {
      name: 'bash', input: { command: 'time pnpm build' },
      output: `[esbuild] bundle dist/app.js (target es2020) … 6.9s\n[typecheck] tsc --noEmit … 1.8s\n\nreal\t9.0s\nuser\t 8.2s\nsys\t 0.9s`,
      ms: 9000,
    });
    if (r.aborted) return abortPath(sid);
    ctx += 520;
    await todoProgress(sid, todoCall, TODO, 3);

    // —— 子代理回归 ——
    r = await taskTool(sid, {
      goal: '回归检查产物体积与 sourcemap 完整性',
      steps: ['检索 sourcemap 引用', '对比 dist 体积', '抽查 map 文件头'],
      output: `sourcemap: dist/app.js.map 存在且 sourceMappingURL 已注入 ✓\n体积: 218KB → 221KB（+3KB，新增 map 不计入）\n抽查: 3 个 map 文件头均含 sources 字段 ✓\n结论：回归通过`,
    });
    if (r.aborted) return abortPath(sid);
    ctx += 410;
    await todoProgress(sid, todoCall, TODO, 4);

    await d(300);
    const summary = [
      '## 分析与结果',
      '',
      '**趋势**：四次构建 12.1 → 18.4 → 27.0 → **42.3s**，单调恶化，与三周内 `src/` 新增 260 个文件强相关——每个文件都先过 Babel 再进 esbuild，双重转译让耗时随规模线性放大。',
      '',
      '**处置**：移除 Babel 层，esbuild 以 `target: \'es2020\'` 直接接管转译；期间发现并修复了 `tsconfig.json` 的 lib 版本滞后（ES2018 → ES2020）。',
      '',
      '| 指标 | 改动前 | 改动后 |',
      '| --- | --- | --- |',
      '| 构建耗时 | 42.3s | **9.0s** |',
      '| 产物体积 | 218KB | 221KB |',
      '| sourcemap | 无 | 有 |',
      '',
      '对比图：',
      '',
      `![优化前后对比](${CHART_AFTER()})`,
      '',
      '回归由子代理完成：体积、sourcemap、map 文件头三项全部通过。`@babel/core` 依赖建议顺手移除（未动，等你确认）。',
    ].join('\n');
    if (!(await streamText(sid, summary, { chunk: [3, 7], gap: [10, 22] }))) return abortPath(sid);

    end(sid, { promptTokens: 11240, completionTokens: 1980, ms: 33600, tps: 302, ctxTokens: ctx + 14300 });
    return true;
  };

  // ============================================================
  // 普通输入的脚本化回合（按关键词分支）
  // ============================================================
  async function runTurn(sid, text, images = []) {
    const s = store.sessions.get(sid);
    const kw = (re) => re.test(text ?? '');

    // —— 多模态：带了图片 ——
    if (images.length) {
      if (!(await streamReasoning(sid, '用户附了图。先从图里读出可量化的信息（坐标轴、趋势、异常点），再结合工作区上下文给结论，不凭空猜。'))) return abortPath(sid);
      const answer = [
        `图我看了（${images.length} 张）。以第一张为例能读出这些信息：`,
        '',
        '- **类型**：柱状图，4 组数据，纵向数值带单位',
        '- **趋势**：单调上升，末值约为首值 3.5 倍——不是抖动，是系统性恶化',
        '- **可疑点**：第 3→4 组增量最大，值得对齐那段时间的变更记录',
        '',
        '这类"耗时随时间线性变慢"的曲线，最常见的三个根因：冗余转译层、依赖图膨胀、缓存失效。要定位的话，把构建脚本给我或直接说「跑一次完整演示」，我按取证流程走一遍。',
      ].join('\n');
      if (!(await streamText(sid, answer))) return abortPath(sid);
      end(sid, { promptTokens: 3120, completionTokens: 460, ms: 6200, tps: 265, ctxTokens: (s.ctxTokens ?? 0) + 3580 });
      return true;
    }

    if (kw(/正则|regex|函数|写一个/)) {
      if (!(await streamReasoning(sid, '写一个小的正则工具函数。先确认需求边界：用户要的是构造与转义，不是完整引擎。'))) return abortPath(sid);
      let r = await tool(sid, {
        name: 'write', input: { path: 'src/regex-utils.ts' },
        output: '已创建 src/regex-utils.ts（1 个文件，+24）',
        ms: 31,
        diff: [
          '+export const escapeRe = (s: string) =>',
          "+  s.replace(/[.*+?^${}()|[\\]\\\\]/g, '\\\\$&');",
          '+',
          '+/** 整词匹配（不依赖 \\b 的 Unicode 边界语义） */',
          '+export const buildExact = (word: string) =>',
          "+  new RegExp(`(?<![\\w])${escapeRe(word)}(?![\\w])`);",
        ],
      });
      if (r.aborted) return abortPath(sid);
      const answer = [
        '已写入 `src/regex-utils.ts`，导出两个函数：',
        '',
        '- `escapeRe(s)` —— 转义正则元字符，处理用户输入必用',
        '- `buildExact(word)` —— 生成整词匹配（等价 `\\b` 语义但不依赖 Unicode 边界）',
        '',
        '```ts',
        "export const escapeRe = (s: string) => s.replace(/[.*+?^${}()|[\\]\\\\]/g, '\\\\$&');",
        "export const buildExact = (word: string) => new RegExp(`(?<![\\w])${escapeRe(word)}(?![\\w])`);",
        '```',
        '',
        '注意 `(?<!…)` 查找断言需要 ES2018+；若要兼容更老目标改成 `\\b` 即可。',
      ].join('\n');
      if (!(await streamText(sid, answer))) return abortPath(sid);
      end(sid, { promptTokens: 4820, completionTokens: 690, ms: 9400, tps: 241, ctxTokens: (s.ctxTokens ?? 0) + 5510 });
      return true;
    }

    if (kw(/报错|错误|error|失败|exception/i)) {
      if (!(await streamReasoning(sid, '用户给了报错但没贴日志。按最常见三类排：端口占用、依赖缺失、Node 版本不匹配。先实测拿证据。'))) return abortPath(sid);
      let r = await tool(sid, {
        name: 'bash', input: { command: 'node -v && npm ls --depth=0 2>&1 | head -20' },
        output: `v22.11.0\nwidget-ui@0.3.1\n├── esbuild@0.24.0\n├── typescript@5.6.3\n└── UNMET DEPENDENCY @babel/core ✗`,
        ms: 480,
      });
      if (r.aborted) return abortPath(sid);
      r = await tool(sid, {
        name: 'bash', input: { command: 'pnpm install --frozen-lockfile' },
        output: `ERR_PNPM_OUTDATED_LOCKFILE  Cannot install with "frozen-lockfile" because pnpm-lock.yaml is not up to date with package.json`,
        ms: 2100, isError: true,
      });
      if (r.aborted) return abortPath(sid);
      if (!(await streamReasoning(sid, '锁文件和 package.json 不一致，冻结安装被拒。修复顺序：重装（允许更新锁文件）→ 复跑构建验证。'))) return abortPath(sid);
      r = await tool(sid, {
        name: 'bash', input: { command: 'pnpm install && pnpm build 2>&1 | tail -3' },
        output: `Done in 9.4s\n+ esbuild@0.24.0\nbuild: ok (9.1s)`,
        ms: 12800,
      });
      if (r.aborted) return abortPath(sid);
      const answer = '问题定位并修复了：`@babel/core` 处于 **UNMET DEPENDENCY** 状态——锁文件和 `package.json` 不一致（大概率手动改过依赖没重装），冻结安装也被拒。\n\n已执行 `pnpm install` 重新对齐锁文件并复跑构建：**9.1s 通过** ✓\n\n以后改完 `package.json` 记得同步跑一次安装，CI 里的 `--frozen-lockfile` 才不会拦。';
      if (!(await streamText(sid, answer))) return abortPath(sid);
      end(sid, { promptTokens: 5240, completionTokens: 705, ms: 16200, tps: 232, ctxTokens: (s.ctxTokens ?? 0) + 5945 });
      return true;
    }

    if (kw(/目录|结构|文件列表|ls/)) {
      let r = await tool(sid, {
        name: 'glob', input: { pattern: 'src/**/*.ts' },
        output: `src/app.ts\nsrc/core/engine.ts\nsrc/cli/index.ts\nsrc/dashboard/chart.ts (+83 more)\n86 个文件`,
        ms: 140,
      });
      if (r.aborted) return abortPath(sid);
      const answer = '当前工作区 86 个 TS 文件，入口 `src/app.ts`，按 core / cli / dashboard 三域分层。要看哪个域的完整清单直接说。';
      if (!(await streamText(sid, answer))) return abortPath(sid);
      end(sid, { promptTokens: 2870, completionTokens: 210, ms: 3400, tps: 259, ctxTokens: (s.ctxTokens ?? 0) + 3080 });
      return true;
    }

    // 默认回合：检索 + 简短回答
    const key = (text ?? '').split(/\s+/).slice(0, 3).join(' ') || (text ?? '').slice(0, 8);
    if (!(await streamReasoning(sid, `围绕「${key}」在当前工作区找依据，先检索再回答，避免凭空发挥。`))) return abortPath(sid);
    let r = await tool(sid, {
      name: 'search', input: { pattern: key, path: '.' },
      output: `src/app.ts:12:  // ${key} 相关入口\nsrc/core/engine.ts:148:  if (${key.split(/\s+/)[0]} != null) {\ntests/engine.test.ts:34:  it('handles ${key.split(/\s+/)[0]}', …)\n3 个文件命中`,
      ms: 210,
    });
    if (r.aborted) return abortPath(sid);
    const answer = `在仓库里找到 3 处与「${key}」相关的代码：入口在 \`src/app.ts:12\`，核心逻辑在 \`src/core/engine.ts:148\`，测试在 \`tests/engine.test.ts:34\`。\n\n从上下文看这是一条已覆盖测试的常规路径。如果要改动，建议从 \`engine.ts\` 的判空分支入手，测试可以直接复用。需要我读出具体代码再展开吗？`;
    if (!(await streamText(sid, answer))) return abortPath(sid);
    end(sid, { promptTokens: 3240, completionTokens: 445, ms: 5900, tps: 289, ctxTokens: (s.ctxTokens ?? 0) + 3685 });
    return true;
  }

  host.prompt = async function (sid, parts) {
    const s = store.sessions.get(sid);
    if (!s || s.running) return;
    const text = (parts ?? []).filter((p) => p.type === 'text').map((p) => p.text).join(' ').trim();
    const images = (parts ?? []).filter((p) => p.type === 'image');
    if (!text && !images.length) return;
    begin(sid, (text || '图片分析').length > 14 ? (text || '图片分析').slice(0, 14) + '…' : (text || '图片分析'));
    store.append(sid, 'user/message', {
      content: [
        ...(text ? [{ type: 'text', text }] : []),
        ...images.map((p) => ({ type: 'image', dataUrl: p.dataUrl, name: p.name })),
      ],
    });
    return runTurn(sid, text, images);
  };

  // —— 重试：截断到最后一轮 user 消息（含），换一路重新跑 ——
  host.retry = async function (sid) {
    const s = store.sessions.get(sid);
    if (!s || s.running) return;
    const un = [...s.nodes].reverse().find((n) => n.kind === 'user');
    if (!un) return;
    const seq = +un.id.slice(1);
    const idx = s.log.indexOfSeq(seq);
    if (idx < 0) return;
    const text = un.text;
    const images = un.images ?? [];
    store.truncate(sid, idx + 1);
    begin(sid);
    return runTurn(sid, text, images);
  };

  // —— 编辑重发：截断到该 user 消息（不含），新文本入账后重跑 ——
  host.editUserMessage = async function (sid, seq, newText) {
    const s = store.sessions.get(sid);
    if (!s || s.running) return;
    const idx = s.log.indexOfSeq(seq);
    if (idx < 0) return;
    store.truncate(sid, idx);
    const images = [];
    begin(sid, newText.length > 14 ? newText.slice(0, 14) + '…' : newText);
    store.append(sid, 'user/message', { content: [{ type: 'text', text: newText }, ...images] });
    return runTurn(sid, newText, images);
  };

  // ============================================================
  // 种子会话：一条静态历史 + 一个空会话（不流式，直接落终态事件）
  // ============================================================
  host.seed = function () {
    const hist = store.createSession({ id: 'seed-1', title: 'CLI 重构复盘' });
    store.append(hist.id, 'session/title', { title: 'CLI 重构复盘' });
    store.append(hist.id, 'user/message', { content: [{ type: 'text', text: '把上次 CLI 重构的结论整理成一页备忘。' }] });
    store.append(hist.id, 'assistant/message', {
      message: {
        model: 'fixture/fx-1',
        content: [{ type: 'tool-call', callId: 'seed-c1', name: 'search', input: { pattern: 'argv|commander', path: 'src/cli' } }],
      },
    });
    store.append(hist.id, 'tool/result', { callId: 'seed-c1', ms: 180, output: 'src/cli/index.ts:8: const argv = parseArgv(process.argv.slice(2))\nsrc/cli/index.ts:21: switch (argv.command) {\n2 处匹配' });
    store.append(hist.id, 'assistant/message', {
      message: {
        model: 'fixture/fx-1',
        content: [{ type: 'text', text: '## CLI 重构备忘\n\n- 手写 `parseArgv` 替换掉了 commander，**少 1 个依赖、少 14KB**\n- 子命令分发表集中在 `src/cli/index.ts:21`，新增命令只改一处\n- 参数校验下沉到各命令模块，入口不再做类型判断\n\n```bash\nnpm run cli -- engine inspect ./dist/app.js\n```\n\n遗留：`--json` 输出还没对齐新结构，下个迭代补。' }],
        usage: { promptTokens: 2140, completionTokens: 305 },
      },
    });
    store.append(hist.id, 'session/stats', { promptTokens: 2140, completionTokens: 305, ms: 3800, tps: 243, ctxTokens: 2445 });

    store.createSession({ id: 'seed-2', title: '新的会话' });
    store.select('seed-2');
    return hist;
  };
}
