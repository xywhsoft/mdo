def template(identity="qa-resource", mode="remote"):
    config = dict(schema_version=1, name="Fixture MCP", description="Isolated MCP service",
                  protocol_version="2025-11-25", startup_timeout_ms=10000,
                  request_timeout_ms=30000, limits=dict(message_bytes=4194304, tools=256),
                  tools=dict(allow=[], deny=[]), security=dict(default_effects=["external-service"],
                  permission_profile="balanced", trust_read_only_annotations=False), auto_reconnect=True)
    config["transport"] = dict(type="streamable-http", endpoint="https://example.invalid/mcp", headers=[])
    inputs = []
    if mode == "key":
        inputs.append(dict(id="token", type="text", label="API key", description="Private credential", required=True, secret=True))
        config["transport"]["headers"] = [dict(name="Authorization", secret_ref="input:token")]
    elif mode == "oauth":
        config["oauth"] = dict(enabled=True, client_id="fixture-client", scope="read")
    elif mode == "stdio":
        config["transport"] = dict(type="stdio", program=dict(input="program"), arguments=[], environment=[], inherit_environment=False)
        inputs.append(dict(id="program", type="file", label="Program", description="Installed executable", required=True, secret=False))
    return dict(format="mdo.mcp-template.v1", id=identity, operator="Fixture operator", inputs=inputs,
                variants=[dict(id="default", label="Default", platforms=["windows-x86_64", "linux-x86_64", "linux-x86_64-glibc", "linux-x86_64-musl", "android-arm64-v8a"], config=config)])
