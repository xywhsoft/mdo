# Plugin prompt files rejected by group activation

The unified extension enable/install plan built a plugin file inventory without
`bytes`. `MdoPluginValidate` correctly requires a positive size for file-backed
prompt contributions, so valid files were reported as empty or larger than
2 KiB. Browser Use 0.1.1's public prompt files are 1003, 1008 and 722 bytes.

`MdoExtGroupPluginRow` now forwards each collected file's actual `Info.Size`;
directories carry zero bytes. Limits, path ownership and recipe validation stay
unchanged. This fixes both current-directory and staged-install group checks.

Evidence and validation:

- Reproduced HTTP 422 using the actual 0.9.6 client's
  `dependency_plan(operation=enable)` followed by `apply_plan`. The diagnostic
  plan was discarded; the user's existing failed plan and disabled plugin were
  preserved.
- Native group validation reproduced failures for valid 1-byte, 2048-byte and
  total-4096-byte prompt files before the change. All 10 cases pass after it,
  including empty/missing/directory files, 2049-byte fragments and aggregate
  overflow rejection. Existing prompt registry contracts pass all 28 checks.
- Tests compile current product source in the existing client compiler runtime,
  using only an owned data fixture under the existing Browser Use Home. No new
  app/Home checkout or service restart. A separate test-service launch was
  rejected by automatic approval review; existing-runtime verification was used
  instead.

Previous Browser Use acceptance used `plugin.plan/commit`, whose inventory
already included sizes, rather than the UI's unified dependency plan. Extend
release qualification to cover the actual UI path. The companion plugin package
will render its already bounded prompt fragments inline for compatibility with
installed 0.9.6 clients; Markdown sources remain included and authoritative.
This source fix is not itself a new client binary release.
