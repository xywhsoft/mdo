# Android 0.8.5 local build — 2026-11-03

Generated the current mainline application as the standard ARM64 lite and full APKs. This is a local delivery only: no website, platform catalogue or automatic update channel was changed, and no device installation was performed.

The display version remains `0.8.5` and the semantic version code remains `05000805`. The Android installation serials advance to `70000166` (lite) and `70000167` (full), preserving same-version upgrade ordering. Other platform installation serials are unchanged.

| Root output | Installation serial | Bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `mdo-arm64-v8a.apk` | 70000166 | 6,473,108 | `f5649b7584210319bad64eeeb158861e984f13a7099d62c5c5337bc781eaacd8` |
| `mdo-full-arm64-v8a.apk` | 70000167 | 85,134,985 | `e531b6556319423d4457b1c21019a9502c937e75c6ab365464669b6df2c5deaf` |

Both APKs use package `org.xleaves.mdo`, minimum API 26, target API 36, and the existing formal signing identity. Certificate SHA-256: `94b1947b60f3ce0852b19d9c37a47cc4a084fe9d601692c6a1f6b204c2174e84`. V2/V3 signatures verify and `debuggable` is false. Signing secrets were read only into the build process environment from the existing project-private signing directory; they were not placed in commands, logs, APKs or Git.

The existing Windows pack host passed its dependency receipt checks. The Android native cache had an older SDK overlay/profile receipt, so it was rejected and the ARM64 native host was rebuilt in the existing `.build/android/native` directory against the current locked SDK overlay. Existing SDK, JDK and native build directories were reused; no separate runtime, Home or worktree was created.

Validation passed:

- Nine Android runtime mapping tests and two command packaging tests.
- Actual pinned Android package-installer Java integration and SDK drift guard during the build.
- Both APKs: CRC, manifest version/installation serial, formal certificate, native dependency closure, complete native/data mapping, and 16 KiB ELF/ZIP alignment.
- Lite contains three host/helper ELF files and no external tools. Full contains 77 ELF files and exactly the original eight tool groups: BusyBox, curl, jq, OpenSSH, aria2, ripgrep, 7-Zip and Python. Git, SQLite CLI, Poppler, FFmpeg and ImageMagick are excluded.
- Application and dependency-lock fingerprints were unchanged throughout the successful build and validation. Existing unrelated working-tree changes were retained and included as current source; they were not reverted or committed by this build step.

Private source freeze, build log, product hashes, signature/manifest output and validation receipts are retained in `.build/android-generation-20261103`. This build does not claim Android device, real vendor model or full release-gate acceptance.
