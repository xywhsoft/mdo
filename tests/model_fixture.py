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
