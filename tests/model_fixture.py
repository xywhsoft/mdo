"""Explicit personal test connection; product installs intentionally have none."""
def model_catalog(model_id="fixture", endpoint="http://127.0.0.1:9/v1"):
    return {"default_model": model_id, "providers": [{
        "id": "fixture-provider", "name": "Fixture", "builtin": False,
        "editable": True, "removable": True, "verify_peer": True, "timeout_ms": 120000,
        "endpoints": {"chat_completions": endpoint},
    }], "items": [{
        "id": model_id, "name": "Fixture model", "provider": "fixture-provider",
        "wire_model": model_id, "builtin": False, "free": False, "editable": True, "removable": True,
        "protocols": ["openai-chat-completions"], "default_protocol": "openai-chat-completions",
        "capabilities": ["text-input", "tool-result-input", "text-output", "streaming", "tool-call-output"],
        "window": {"mode": "shared-context", "context_tokens": 240128, "max_input_tokens": 240127,
                   "max_output_tokens": 16384, "output_reserve_tokens": 8192, "summary_tokens": 4096},
        "reasoning_efforts": ["none"], "default_reasoning_effort": "none", "attachments": [],
    }]}


def conversation_catalog(environment=None):
    """Explicit multi-protocol personal profile for historical conversation probes.

    The wire ID matches these loopback mocks, but it is an explicitly configured
    personal provider, not a bundled model or an online-auth bypass.
    Endpoint overrides are test inputs, not product environment backdoors.
    """
    environment = environment or {}
    catalog = model_catalog("ornith-1.5-35b")
    catalog["providers"][0].update(
        credential={"secret_ref": "env:MDO_ORNITH_API_KEY"},
        endpoints={"chat_completions": environment.get("MDO_ORNITH_CHAT_COMPLETIONS_URL", "http://127.0.0.1:9/v1/chat/completions"),
                   "responses": environment.get("MDO_ORNITH_RESPONSES_URL", "http://127.0.0.1:9/v1/responses"),
                   "anthropic_messages": environment.get("MDO_ORNITH_ANTHROPIC_URL", "http://127.0.0.1:9/v1/messages")})
    catalog["items"][0].update(protocols=["openai-chat-completions", "openai-responses", "anthropic-messages"],
        default_protocol="openai-responses", reasoning_efforts=["none", "low", "medium", "high"],
        default_reasoning_effort="medium")
    catalog["items"][0]["capabilities"] += ["reasoning-output", "reasoning-control", "parallel-tool-calls", "json-output"]
    return catalog


def prepare_conversation_models(site, home, environment):
    """Seed only owned source-test Homes; preserve derived media/window profiles."""
    import json
    from pathlib import Path
    defaults = Path(site) / "default-home/config/defaults.json"
    if (Path(home) / "config/models.json").exists(): return
    document = json.loads(defaults.read_text(encoding="utf-8"))
    catalog = document["models"]
    if not catalog["items"]: catalog = conversation_catalog(environment)
    else:
        for provider in catalog["providers"]:
            if provider["id"] == "fixture-provider":
                provider["endpoints"] = conversation_catalog(environment)["providers"][0]["endpoints"]
    document["models"] = catalog
    defaults.write_text(json.dumps(document),encoding="utf-8")
