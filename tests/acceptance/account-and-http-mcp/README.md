# Account lifecycle and public HTTP MCP acceptance

The account runtime test uses an isolated xadmin and native client. It verifies password and browser/PKCE login, strict callbacks and CSRF, encrypted remembered credentials and temporary credentials, restart and renewal, failed account-switch preservation, independent logout/cancellation, and login-gated search. It does not change the online account database.

```powershell
python tests/test_account_runtime.py --host .build/host/xs.exe
node --test --test-reporter=tap tests/test_account_allowance.mjs tests/test_account_balance.mjs
```

The two Node tests cover the shared GLM/Flash allowance pool, weights, expired VIP, unlimited and stale values, and cash balance, credit, reserved amounts and unavailable values. This is not an all-platform visual account-page test.

The live HTTP MCP check uses the [official public DeepWiki endpoint](https://docs.devin.ai/work-with-devin/deepwiki-mcp), `https://mcp.deepwiki.com/mcp`, with ordinary TLS verification. Through the packed page and its normal one-time permission prompts, the loopback model searches for the tool, loads its schema, calls `read_wiki_structure` for the public `modelcontextprotocol/python-sdk` repository, and receives its result before replying.

Four model requests and three successful tools complete one run with no final error or browser console errors. The first two requests contain no remote MCP schemas; only the selected schema is then loaded. Each of the three operations receives a one-time approval; no permanent grant is created. The remote result is 3,775 characters. The receipt stores its hash and length, not its content.

The only remote call reads public repository information. Private workspace content, credentials and model requests are not sent to the remote service. No stress test, paid model call, phone installation or release is performed. This checks the measured HTTP path, not compatibility with every MCP server or protocol revision.

For an opt-in repeat, start a disposable fixture:

```powershell
python tests/manual_mcp_http_qa.py --packed .build/conversation-acceptance/mdo-recovery-ack-final.exe --directory .build/mcp-http-manual --permission balanced --duration 900
```

In its page, configure HTTP MCP ID `deepwiki`, protocol `2025-11-25`, and the endpoint above. Send `mcp-http` and approve each operation once. The fixture accepts only that prompt and limits itself to four model turns. It is not run by default CI and needs no model key.

`acceptance.json` records the binary, commands, native probe, model/tool counts and result hashes. `http-mcp-completed.jpg` shows the completed packed-page run.
