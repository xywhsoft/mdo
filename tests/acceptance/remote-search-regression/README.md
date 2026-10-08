# Remote control and search bounded regression

All native probes pass against a disposable Home and a loopback website running the current unified mdo plugin. They do not alter the maintained website, public accounts or existing conversations. Search result tests use synthetic loopback responses, not paid search providers.

The old relay tests tried to enable the removed standalone `device-relay` plugin. They now share the established isolated website activation fixture and accept a separate `--website-host`. The fixture permits its two local account-isolation registrations; xadmin's registration throttling is tested separately. Mail remains disabled. The WS fixture preserves real relative includes and uses the compact host's PEM trust-store importer; certificate and hostname verification remain enabled. The live model is added through the ordinary settings API with a write nonce and ETag instead of an obsolete built-in anonymous-model override.

Passed native test files:

- `test_remote_service_runtime.py`: encrypted, stable, account-specific identities; registration, listing, one-use tickets, text/binary relay, revoke/reactivation and logout cancellation.
- `test_remote_manager.py`: default off, write gate, nonblocking jobs, reconnect without registration, native Unit/restore, superseded work, account switching and logout.
- `test_remote_bridge.py`: actual project/session writes, attachments, offset/digest validation, read-only scope, response streaming and lost-write receipt/reconnect.
- `test_remote_live.py`: streamed replies and exact cursor resume/HTTP parity, questions and permission decisions. Three runs, five model completions and two tool completions, including one intentional permission denial; three successful run ends and no final model error.
- `test_remote_reload.py`: one actual TCC generation replacement joins open HTTP/live channels and preserves endpoint metadata; a new nonce/runtime fences old connections.
- `test_remote_background.py`: CPU lease acquisition, renewal and release on Unit, disable, account switch and logout. This uses a deterministic platform adapter, not a physical Android power-management test.
- `test_remote_websocket.py`: sixteen small functional WS/WSS cases, plus two quiet connections with two native heartbeats each. Trust/hostname rejection, malformed frames, close/ping, Unicode, binary and cancellation are checked. No pressure or traffic-generation loop is used.
- `test_remote_loopback.py`: actual native routes, target tokens, preconditions, binary/HEAD/chunked decoding, malformed/truncated responses, scope and cancellation.
- `test_search_packed.py`: current packed candidate login-gated preference and legacy option removal.
- `test_search_api_runtime.py`: POST/auth, UTF-8, valid/empty results, business/HTTP errors, redirects and size limits.

Run native checks with `--host .build/host/xs.exe`; relay checks also accept `--website-host D:/GIT/home/xs.exe`. Run the quiet heartbeat check with `--keepalive`, the packed search check with `--packed .build/conversation-acceptance/mdo-recovery-ack-final.exe`, and the frontend transport group with `node --test tests/test_remote_transport.mjs`.

`acceptance.json` records binary hashes, source revision and measured live event counts. Native source probes include current checkout changes and do not mean the earlier packed candidate contains later history/remote changes. This stage changes test infrastructure only. Android hardware, clipboard contents and Markdown download-to-disk remain unconfirmed. No phone installation or release is performed.
