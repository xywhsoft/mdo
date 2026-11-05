# XRT agent libraries consolidated into mainline

The historical mdo XRT branches can be removed after their missing behavior is
ported to XRT's modular source tree. This dependency update uses the consolidated
libraries, retains the existing native core and host compatibility, and provides
a source bundle so no historical branch name is needed to rebuild mdo.

## Exact source pins

| Source | Revision / SHA-256 |
| --- | --- |
| Canonical XRT agent libraries | `88bfa0de2230a1656a1b52e4b3968f3dfa995259` |
| XS export base | `94eb06ab94d7ea276922d8d09461d4ae032f1e37` |
| Complete native compatibility snapshot | `81f7fe13d3992bea6ab4133b865f793407e040a5` |
| `xs-agent-mainline.bundle` | `dda7239c16eca0c31a66d7e5122a5b63732d93ab62550156b9accac816276be4` |
| `xwork-mcp.patch` | `f387b1b5a03bbee7e3b50fbec7b0281015fe53de1257e9e92f83dc4cdad65918` |
| Effective native XRT single header | `99bead7f87b4e6601cbb555b0d1ba30bb0030bf42c731c7c21f8bd29c1f04453` |

The bundle's pinned ref is `refs/heads/codex/mdo-sdk-snapshot`; this immutable
ref inside the bundle does not require a live local or remote branch. Its
historical prerequisite is `695988f7b8eec4b52bcc829a2d52210443c0bc69`.
The bundle also retains the complete native snapshot as an archive tag.

The original native XRT pin `3d55ec41` and regex fix `a539c579` stay in place.
Native process cleanup and the other existing compatibility changes remain in
the genuine SDK overlay. The three canonical library implementations no longer
live in that overlay. Their mechanically adjusted flat XS/TCC exports are
checked against XRT with `tools/export_agent_libraries.py` and the XS verifier.
Host-owned bridge files are additionally covered by the production-tree hashes.

| Library | Version | Production-tree SHA-256 |
| --- | --- | --- |
| xllm | 3.2.0 | `c6432b4cf9cb001bf4ce23553ba45244c5b0fd5852e66bb37fbdd393371cfc32` |
| xllm-session | 3.0.0-dev | `b15f24962301eeb5be1cb77fb2d12c223ab566bd6932a60ad19652135f4dba23` |
| xwork | 3.11.0 | `2066157b406a32cb0c4f9d4bab0e5999bee57c8fd96e1e0b68bc91f224b285df` |

## Compatibility and fixes

xllm preserves both the mainline relative-millisecond timeout setter and the
product's absolute monotonic-microsecond deadline setter. It captures a relative
budget when a request is submitted, including retries and backoff. The public
request layout changes, so all native/SDK consumers must be rebuilt together.
Session persistence remains schema 4; xwork remains ABI 9 / event schema 3.
Application module ABI and release version/build IDs are unchanged.

The consolidated sources retain current mdo behavior: ordered text/image/native
parts, model-context/usage anchors, explicit runtime/agent/run lifetimes, dynamic
catalog and callback owners, permission effects, structured process tasks,
MCP transports, subagents, scheduling/run-now, sequenced events, recovery
receipts, request tokens and bounded immutable result files. XRT's consolidation
record details the three historical branch tips and their archive tags.

The obsolete rooted-rename branch is not restored as an old XRT public API.
`xsRootRenameNoReplace` handles files/directories/final links and rejects
intermediate-link traversal; rooted hard-link publication protects immutable
result files against replacement. The XS runtime upgrade probe covers these
operations, with link checks conditional on platform link availability.

The search fixtures now accept canonical `search` and legacy `web_search` names
while keeping exact execution counts. The real process-restart fixture seeds
an explicit local personal model profile. The preceding host also failed this
fixture with the current empty product model catalog; product defaults and
account access checks have not been changed.

Fresh SDK preparation exposed a separate reproducibility bug: `git apply`
discovered the outer mdo repository while running below its `.build` directory
and silently skipped the SDK patch paths. Preparation now sets a Git discovery
ceiling around the temporary export. A nested-repository regression test checks
that the inner SDK is patched and the outer file stays intact.

## Validation

- Windows GCC: 41 independent migrated extension translation units; all three
  standard library suites, including original modular and migrated single-header
  tests, provider/TLS, OOM, lifecycle, persistence and recovery checks.
- Linux GCC: the same three standard library suites. The task-ID fixture's LP64
  pointer assumption was fixed using a `uint64_t` value and `memcpy`.
- Android NDK 30, API 26, ARM64: strict modular compilation and linking of all
  three library test programs. This is compile validation, not a device run.
- Windows static/shared packages and a real linked consumer; all three feature
  dependency rejection probes, maturity checks, generated API references and
  single-header consistency checks.
- MCP HTTP error/recovery/replay fixtures and modern/legacy SSE routing, resume
  and DELETE tests, using bounded local servers.
- XS extension contracts: 36 tests, one platform skip; the actual native host's
  clock/wait/process/rooted-rename upgrade probe.
- mdo build contracts: 18 tests; model SDK build contracts and native overlay
  receipt contracts: two tests each.
- Source replay from a repository containing only the bundle's historical base:
  fetch bundled source, apply the pinned patch, verify every dependency hash.
- Actual mdo native host: `test_agent_media_runtime.py`,
  `test_artifact_publication_runtime.py`, `test_home_artifact_runtime.py`,
  `test_web_result_files_runtime.py`, `test_schedule_executor_runtime.py`,
  `test_mcp_reliability_runtime.py` and `test_search_client_restart.py`.
  The last test exercises two real client processes for seven guarded recovery
  scenarios, including single search dispatch/debit and exactly one user prompt.
- A candidate Windows GUI package was built with verified final native receipts;
  extraction confirmed the exact embedded dependency lock, generated unity source
  and Windows CLI forwarder.

The native host and candidate package are built into isolated output paths.
Nothing is deployed, installed, published or pushed by this consolidation.
