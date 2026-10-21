# mdo — Native C-Stack Agent Workbench

> Full agent capabilities in a ~4 MB desktop app. Build, extend, and run it anywhere.
> See [README_zh.md](README_zh.md) for the Chinese version.

mdo (墨斗) is a portable Agent workbench that layers a **native C stack** —
`xrt`, `xs`, `xllm`, `xllm-session`, and `xwork` — under a native HTML/CSS/JavaScript
frontend. Business logic is written in C, compiled into a single TCC unity entry, and
hosted by an `xs` runtime that renders the UI in a WebView. The result is a fast,
self-contained desktop / mobile / server app with a full local-agent feature set.

---

## ✨ Highlights

### 🪶 Complete Agent in ~4 MB
The full agent feature set ships in a remarkably small footprint:

| Product | Size | Notes |
| --- | --- | --- |
| `mdo-arm64-v8a.apk` (Android, slim) | **~4 MB** | Full agent on Android |
| `mdo-linux-x86_64-glibc` | ~4.8 MB | Desktop, dynamic core |
| `mdo-server.exe` | ~5 MB | Headless server edition |
| `mdo.exe` (Windows) | ~5.1 MB | Desktop with native window |
| `mdo-full-arm64-v8a.apk` (Android, full) | ~59 MB | Bundles a bundled browser |

The slim APK reaches the ~4 MB target while still delivering the complete agent:
model chat, tools, projects, sessions, memory, skills, and remote control.

### ⚡ Full Feature Set
- **Local Agent core** — model chat with streaming, tool calls, and guided builders.
- **Projects & Sessions** — create, restore, search, fork, truncate, export, pin, archive, trash; snapshot/journal layered persistence.
- **Skills** — author, scope, and invoke skills; built-in creator and native validation.
- **Memory** — per-project memory plus transfer between devices.
- **Web tools** — bounded `web_search`, `web_open`, `web_find` with DNS-rebinding protection.
- **Configuration** — layered defaults + per-user settings, atomic replace, `.bak` backups, `secret_ref` for sensitive values.
- **MCP** — Model Context Protocol support.
- **Schedules, runs, approvals, asks** — background automation and permission gating.
- **Auto-update** — Windows and Linux update pipelines.

### 🛠 Secondary Development (Extensibility)
mdo is built to be extended and rebuilt:

- **C source is the app** — all logic lives in `app/src/`, organized by product boundary
  (`api/`, `sessions/`, `remote/`, `skills/`, `projects/`, ...). ~200 C files, ~73k lines.
- **Deterministic build** — `deps.lock` pins every dependency to a full commit + source hash;
  `tools/build.py` generates the TCC unity entry from `app/sources.json`, builds the matching
  `xs` host, and packages the app. It rejects unreferenced C sources and private headers.
- **Native module ABI** — `app/include/mdo/` defines versioned modules (MDO-3) for pluggable features.
- **Frontend contract** — `app/web/` holds the HTML/CSS/JS; a publish check rejects missing imports
  and modules unreachable from page entry points.
- **Add your own** — skills, models, MCP servers, and custom modules slot into the same pipeline.

### 🌐 Cross-Platform
One code base, four targets, portable Home:

| Platform | Artifact | GUI | Notes |
| --- | --- | --- | --- |
| Windows | `mdo.exe` / `mdo-server.exe` | Native window | Portable Home, zero-write read-only startup |
| Android | `mdo-arm64-v8a.apk` / `mdo-full-arm64-v8a.apk` | WebView2 | Slim (~4 MB) and full variants |
| Linux | `mdo-linux-x86_64-{glibc,musl}` | GTK / WebKitGTK | GUI + headless server editions |

- **Portable Home** — all runtime data (`mdo-home/`) lives beside the executable; never pollutes the user profile.
  Override with `--home` / `MDO_HOME`.
- **Consistent sessions** — same session format, config schema, and Home layout across platforms.
- **Linux build** — glibc/musl, GUI/server editions, Debian-baseline bootstrap, `--wsl` support.

### 📱 Multi-Device Remote Control
Control one device from another, or move work between them:

- **Bidirectional control** — PC ↔ Android remote control (real-device verified). Same account,
  enable remote control on both ends.
- **Send to device** — send a single session or a complete project from the menu; the receiver
  keeps an independent copy (source data is never moved).
- **What transfers** — single session: model ledger, UI history, usage, attachments, artifacts,
  schedules, drafts, staging queue. Full project: all sessions (incl. archive & trash), the entire
  workspace directory, and project memory.
- **Resilient transfer** — native background tasks with chunked writes, checksums, resume-after-break,
  and lease-based exclusivity so running processes don't corrupt the send.
- **Secure transport** — reuses remote-control auth (same-account tickets, write tokens, sequence numbers);
  LAN TLS when available, otherwise relayed through the existing server.

---

## 🚀 Quick Start

Build the app (Windows PowerShell):

```powershell
python tools/build.py
```

Prepare the entry, then launch the built host:

```powershell
python tools/build_mdo.py --prepare-only
.build\host\xs.exe dev.json
```

Release gate (requires Node.js for frontend checks):

```powershell
python tools/qa_release.py --xserver-root D:\GIT\xserver-mdo-refactor
```

---

## 📂 Source Layout

```text
app/
  include/mdo/     versioned module ABI (MDO-3)
  src/             C modules by product boundary
  default-home/    read-only default resource tree for mdo.exe
  web/             native HTML/CSS/JavaScript frontend
  sources.json     unity source manifest
  xs.json            single-file app config
deps.lock          cross-repo dependency + format-version lock
tools/build_mdo.py verify → generate → host build → package
tools/build.py     desktop + Android slim/full unified build
```

---

## 📚 Docs
Full contracts and guides live in [`docs/`](docs):
`configuration.md`, `session-storage.md`, `web-tools.md`, `device-transfers.md`,
`device-remote-control.md`, `linux.md`, `android.md`, `release-gate.md`,
`mdo-refactor-implementation-plan.md`.

---

## 📄 License
See [LICENSE](LICENSE). Current source version: **0.2.1**.
