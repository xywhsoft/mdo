# Project and session daily-operation acceptance

The packed Windows candidate runs from a disposable directory with a separate Home and loopback model. The page performs these ordinary operations:

- Choose a workspace through the directory picker, create a project and select its default model.
- Switch between project and default new tasks, retaining their independent drafts.
- Create a project conversation, rename and pin it, archive/unarchive it, then move it to the recoverable trash and restore it. History and the unsent session draft remain intact; archived/trashed sessions cannot send.
- Filter the sidebar by title and return to the original session.
- Change the project name and default directory. Existing session metadata remains bound to `workspace-alpha`; a new session uses `workspace-beta`. A sentinel workspace file remains unchanged.
- Restart only the disposable host after its three runs finish. The old page correctly stops writes at its restart fence; explicit reconnect and page reload restore the history and saved draft. A subsequent conversation succeeds. The default session draft and default new-task draft also survive.

Four model requests each execute once, with four successful run-end events and no model error event or browser console error. The second turn contains the two expected user messages, not a replay of the prior request. Native portable files corroborate the bindings and three surviving drafts.

Bounded automatic checks also pass:

```powershell
python tests/test_project_binding_runtime.py --host .build/host/xs.exe
python tests/test_session_runtime.py --host .build/host/xs.exe
python tests/test_backup_export_runtime.py --host .build/host/xs.exe
node --test --test-reporter=tap tests/test_project_dialog.mjs tests/test_project_identity.mjs tests/test_project_draft_selection.mjs tests/test_session_action_focus.mjs tests/test_session_export_full_text.mjs tests/test_session_export_images.mjs tests/test_session_export_i18n.mjs tests/test_backup_export_dialog.mjs
```

The project probe checks 60 binding/publication conditions. The session probe covers creation, history, recovery, archive and trash. Complete JSON backup transfer and readback retain model/UI history, image bytes/names, draft, queue and artifacts over HTTP and TLS. TLS uses only the fixture's own self-signed loopback certificate. The Node group contains 28 passing tests.

`acceptance.json` records native and browser scope, binary hash, bindings, request counts and portable drafts. Screenshots show the restored session and saved new-task draft. This stage has no production-code change.

The native browser connector did not report a saved Markdown download after the real export click. Actual download-to-disk and clipboard contents remain unconfirmed; passing serialization and complete-backup readback do not substitute for those checks. No phone, paid model, public account, original session, pressure test or release is involved.
