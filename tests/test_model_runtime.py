"""Bounded MDO-6A model catalog probe through the real xs/TCC runtime."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


from runtime_sources import copy_app_source


ROOT = Path(__file__).resolve().parent.parent
PROBE_SOURCE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>
#include "model-plugin-free.h"

#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"

static void PrintError(const char* label)
{
    const xerror* error = xrtGetError();
    printf("%s=%s\n", label,
        error != NULL ? xrtErrorMessage(error) : "missing-error");
    xrtClearError();
}

void ServiceInit(XS_HostInfo* host)
{
    MdoModelCatalog* first = NULL;
    MdoModelCatalog* second = NULL;
    MdoProviderInfo provider;
    MdoModelInfo model;
    xllm_model_profile profile;
    xllm_error error;
    MdoModelClientOptions client_options;
    MdoModelClientInfo client_info;
    MdoConfigTransportSettings transport;
    unsigned protocol;
    (void)host;

    if ( !MdoHomeInit() || !MdoConfigInit() || !MdoModelManagerInit() ) {
        PrintError("probe_init_error");
        printf("probe_done=1\n");
        return;
    }
    memset(&transport, 0, sizeof(transport));
    transport.Size = sizeof(transport);
    printf("transport_ok=%d path=%s\n",
        MdoConfigGetTransportSettings(&transport) ? 1 : 0,
        transport.CaPemPath);
    if ( transport.CaPemPath[0] != '\0' ) {
        xx509store* system = xrtX509StoreSystem();
        xx509store* combined = MdoModelsLoadCaStore(transport.CaPemPath,
            NULL);
        printf("ca_anchor_counts=system:%zu combined:%zu\n",
            system != NULL ? xrtX509StoreCount(system) : 0u,
            combined != NULL ? xrtX509StoreCount(combined) : 0u);
        xrtX509StoreFree(system);
        xrtX509StoreFree(combined);
    }
    first = MdoModelCatalogSnapshot();
    printf("catalog_one=%llu providers=%zu models=%zu\n",
        (unsigned long long)MdoModelManagerGeneration(),
        MdoModelCatalogProviderCount(first), MdoModelCatalogModelCount(first));
    memset(&provider, 0, sizeof(provider));
    provider.Size = sizeof(provider);
    printf("provider_ok=%d\n",
        MdoModelCatalogProviderFind(first, "fixture-provider", &provider) ? 1 : 0);
    printf("provider=%s protocols=%u credential_ref=%d verify=%d\n",
        provider.Id, (unsigned)provider.Protocols,
        provider.HasCredentialReference ? 1 : 0,
        provider.VerifyPeer ? 1 : 0);
    memset(&model, 0, sizeof(model));
    model.Size = sizeof(model);
    printf("default_ok=%d\n", MdoModelCatalogDefault(first, &model) ? 1 : 0);
    printf("model=%s wire=%s default_protocol=%u context=%llu output=%u efforts=%zu\n",
        model.Id, model.WireModel, (unsigned)model.DefaultProtocol,
        (unsigned long long)model.ContextWindowTokens,
        (unsigned)model.MaxOutputTokens, model.ReasoningEffortCount);
    for ( protocol = MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
          protocol <= MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES; ++protocol ) {
        bool ok;
        memset(&error, 0, sizeof(error));
        ok = MdoModelCatalogProfile(first, model.Id,
            (MdoModelProtocol)protocol, &profile, &error);
        printf("profile_%u=%d provider=%d\n", protocol, ok ? 1 : 0,
            ok ? (int)profile.eProvider : -1);
    }
    memset(&error, 0, sizeof(error));
    bool profile_bad = MdoModelCatalogProfile(first, model.Id,
        (MdoModelProtocol)99, &profile, &error);
    printf("profile_bad=%d kind=%s\n", profile_bad ? 1 : 0,
        MdoModelErrorKind(&error));
    MdoModelClientOptionsInit(&client_options);
    for ( protocol = MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
          protocol <= MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES; ++protocol ) {
        xllm_client* client;
        memset(&client_info, 0, sizeof(client_info));
        client_info.Size = sizeof(client_info);
        client_options.Protocol = (MdoModelProtocol)protocol;
        memset(&error, 0, sizeof(error));
        client = MdoModelClientCreate(first, &client_options, &client_info,
            &error);
        printf("client_%u=%d generation=%llu output=%u error=%s kind=%s\n",
            protocol, client != NULL ? 1 : 0,
            (unsigned long long)client_info.ModelGeneration,
            (unsigned)client_info.MaxOutputTokens,
            error.sMessage[0] != '\0' ? error.sMessage : "none",
            MdoModelErrorKind(&error));
        if ( client != NULL ) xllmClientDestroy(client);
    }
    client_options.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    client_options.ReasoningEffort = "unsupported";
    memset(&error, 0, sizeof(error));
    {
        xllm_client* client = MdoModelClientCreate(first, &client_options,
            NULL, &error);
        printf("client_bad_reasoning=%d kind=%s\n", client != NULL ? 1 : 0,
            MdoModelErrorKind(&error));
        if ( client != NULL ) xllmClientDestroy(client);
    }
    client_options.ReasoningEffort = NULL;
    client_options.MaxOutputTokens = 16385u;
    memset(&error, 0, sizeof(error));
    {
        xllm_client* client = MdoModelClientCreate(first, &client_options,
            NULL, &error);
        printf("client_bad_output=%d kind=%s\n", client != NULL ? 1 : 0,
            MdoModelErrorKind(&error));
        if ( client != NULL ) xllmClientDestroy(client);
    }
    printf("reload=%d\n", MdoModelManagerReload() ? 1 : 0);
    second = MdoModelCatalogSnapshot();
    memset(&model, 0, sizeof(model));
    model.Size = sizeof(model);
    {
        bool old_ok = MdoModelCatalogDefault(first, &model);
        bool new_ok = MdoModelCatalogDefault(second, &model);
        printf("catalog_two=%llu old_default=%d new_default=%d\n",
            (unsigned long long)MdoModelManagerGeneration(),
            old_ok ? 1 : 0, new_ok ? 1 : 0);
    }
    MdoModelCatalogRelease(second);
    MdoModelCatalogRelease(first);
    printf("probe_done=1\n");
}

void ServiceUnit(XS_HostInfo* host)
{
    (void)host;
    MdoModelManagerUnit();
    MdoConfigUnit();
    MdoHomeUnit();
}
'''


def personal_catalog():
    from model_fixture import model_catalog
    catalog = model_catalog("fixture-model", "https://example.invalid/v1")
    catalog["providers"][0].update(
        credential={"secret_ref": "env:MDO_TEST_MODEL_KEY"},
        endpoints={"chat_completions": "https://example.invalid/v1/chat/completions",
                   "responses": "https://example.invalid/v1/responses",
                   "anthropic_messages": "https://example.invalid/v1/messages"})
    catalog["items"][0].update(
        protocols=["openai-chat-completions", "openai-responses", "anthropic-messages"],
        reasoning_efforts=["none", "low", "medium", "high"], default_reasoning_effort="medium")
    catalog["items"][0]["capabilities"].extend(["reasoning-output", "reasoning-control"])
    return catalog


def write_models(home, catalog):
    path = home / "config/models.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"schema_version": 1, "patch": catalog}), encoding="utf-8")


def write_site(site: Path) -> int:
    (site / "web").mkdir(parents=True)
    (site / "default-home/config").mkdir(parents=True)
    (site / "src/storage").mkdir(parents=True)
    (site / "src/config").mkdir(parents=True)
    (site / "src/security").mkdir(parents=True)
    (site / "src/models").mkdir(parents=True)
    (site / "include/mdo").mkdir(parents=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    shutil.copy2(ROOT / "tests/fixtures/model-plugin-free.h", site / "model-plugin-free.h")
    for relative in (
        "default-home/config/defaults.json",
        "src/storage/home.c", "src/storage/home_fs.inc.c", "src/storage/home_purge.inc.c", "src/storage/home_restore.inc.c", "src/config/config.c",
        "src/security/secrets.c", "src/models/catalog.c",
        "include/mdo/home.h", "include/mdo/home_purge.h", "include/mdo/home_restore.h", "include/mdo/session_file_policy.h", "include/mdo/config.h", "include/mdo/models.h",
        "include/mdo/secrets.h", "include/mdo/version.h",
    ):
        copy_app_source(relative, site)
    # Product defaults intentionally contain no connection. Supply an ordinary
    # personal provider explicitly, rather than relying on retired built-ins.
    defaults = site / "default-home/config/defaults.json"
    data = json.loads(defaults.read_text(encoding="utf-8"))
    data["models"] = personal_catalog()
    defaults.write_text(json.dumps(data), encoding="utf-8")
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({
        "services": [{
            "enabled": True,
            "class": "http",
            "name": "mdo-model-probe",
            "ip": "127.0.0.1",
            "port": port,
            "host_default": {
                "enabled": True,
                "name": "probe",
                "path": "web",
                "devlang": "c",
                "devfile": "probe.c",
            },
        }],
    }), encoding="utf-8")
    return port


def run_probe(host: Path, site: Path, home: Path,
              environment: dict[str, str] | None = None, timeout_seconds: float = 10.0) -> str:
    command = [str(host), "xs.json", "--", "--home", str(home)]
    process_environment = os.environ.copy()
    process_environment["MDO_TEST_MODEL_KEY"] = "bounded-runtime-fixture-key"
    process_environment.pop("MDO_TEST_PROXY_PASSWORD", None)
    if environment is not None:
        process_environment.update(environment)
    process = subprocess.Popen(
        command, cwd=site, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
        env=process_environment,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
    )
    lines: list[str] = []
    done = threading.Event()
    assert process.stdout is not None

    def read_output() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.append(line)
            if "probe_done=1" in line:
                done.set()

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    done.wait(timeout=timeout_seconds)
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)
    reader.join(timeout=3.0)
    return "".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"),
    )
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="model-runtime-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        write_site(site)
        missing = run_probe(host, site, base / "missing")
        assert "probe_init_error=" not in missing, missing
        for protocol in (1, 2, 3):
            assert f"client_{protocol}=1" in missing, missing
        assert "error=none" in missing, missing
        valid_home = base / "ca-valid"
        (valid_home / "config").mkdir(parents=True)
        (valid_home / "certs").mkdir()
        (valid_home / "config/settings.json").write_text(json.dumps({
            "schema_version": 1,
            "patch": {"transport": {"ca_pem_path": "certs/test.pem"}},
        }), encoding="utf-8")
        shutil.copy2(ROOT / "tests/gpu-ca.crt", valid_home / "certs/test.pem")
        valid = run_probe(host, site, valid_home)
        assert "transport_ok=1 path=certs/test.pem" in valid, valid
        assert "client_1=1" in valid and "client_3=1" in valid, valid
        counts = re.search(r"ca_anchor_counts=system:(\d+) combined:(\d+)", valid)
        assert counts and int(counts[2]) >= int(counts[1]) + 1, valid
        (valid_home / "certs/test.pem").write_text("invalid PEM", encoding="utf-8")
        invalid = run_probe(host, site, valid_home)
        assert "client_1=0" in invalid and "no valid PEM certificates" in invalid, invalid
        assert "kind=profile_ca_unavailable" in invalid, invalid
        (valid_home / "certs/test.pem").unlink()
        absent = run_probe(host, site, valid_home)
        assert "client_1=0" in absent and "unavailable in the portable Home" in absent, absent
        assert "kind=profile_ca_unavailable" in absent, absent
        for proxy_kind in ("http-connect", "socks5"):
            proxy_home = base / proxy_kind
            (proxy_home / "config").mkdir(parents=True)
            (proxy_home / "config/settings.json").write_text(json.dumps({
                "schema_version": 1, "patch": {"transport": {"proxy": {
                    "kind": proxy_kind, "host": "127.0.0.1", "port": 18080,
                    "user": "probe", "bypass": "localhost",
                    "credential": {"secret_ref": "env:MDO_TEST_PROXY_PASSWORD"},
                }}},
            }), encoding="utf-8")
            proxy_output = run_probe(host, site, proxy_home, {
                "MDO_TEST_PROXY_PASSWORD": "bounded-probe-password",
            })
            assert "client_1=1" in proxy_output and "client_3=1" in proxy_output, proxy_output
            missing_password = run_probe(host, site, proxy_home, {
                "MDO_TEST_PROXY_PASSWORD": "",
            })
            assert "client_1=0" in missing_password, missing_password
            assert "model proxy password reference is unavailable" in missing_password, missing_password
            assert "kind=profile_proxy_unavailable" in missing_password, missing_password
        invalid_home = base / "invalid-endpoint"
        invalid_catalog = personal_catalog()
        invalid_catalog["providers"][0]["endpoints"]["chat_completions"] = "https://example.invalid/\n"
        write_models(invalid_home, invalid_catalog)
        invalid_endpoint = run_probe(host, site, invalid_home)
        assert "client_1=0" in invalid_endpoint and "kind=profile_endpoint_invalid" in invalid_endpoint, invalid_endpoint
        output = run_probe(host, site, base / "state")
        assert "probe_init_error=" not in output, output
        assert "catalog_one=1 providers=1 models=1" in output, output
        assert "provider_ok=1" in output, output
        assert "provider=fixture-provider protocols=7 credential_ref=1 verify=1" in output, output
        assert "default_ok=1" in output, output
        assert "model=fixture-model wire=fixture-model" in output, output
        assert "context=240128 output=16384 efforts=4" in output, output
        assert "profile_1=1 provider=0" in output, output
        assert "profile_2=1 provider=2" in output, output
        assert "profile_3=1 provider=3" in output, output
        assert "profile_bad=0 kind=profile_protocol_unsupported" in output, output
        assert "client_1=1 generation=1 output=16384 error=none" in output, output
        assert "client_2=1 generation=1 output=16384 error=none" in output, output
        assert "client_3=1 generation=1 output=16384 error=none" in output, output
        assert "client_bad_reasoning=0 kind=profile_reasoning_unsupported" in output, output
        assert "client_bad_output=0 kind=profile_output_limit" in output, output
        assert "reload=1" in output, output
        assert "catalog_two=2 old_default=1 new_default=1" in output, output
        assert "probe_done=1" in output, output
        # Missing/malformed personal secret references fail locally without
        # exposing key bytes. These probes create clients, never network calls.
        no_key_home = base / "without-key"
        unavailable = run_probe(host, site, no_key_home, {"MDO_TEST_MODEL_KEY": ""})
        assert "client_1=0" in unavailable and "kind=profile_credential_unavailable" in unavailable, unavailable
        assert "bounded-runtime-fixture-key" not in unavailable
        assert not no_key_home.exists()

        invalid_key_home = base / "invalid-key"
        invalid_catalog = personal_catalog()
        invalid_catalog["providers"][0]["credential"] = {"secret_ref": "file:config/secrets/test.key"}
        write_models(invalid_key_home, invalid_catalog)
        key = invalid_key_home / "config/secrets/test.key"
        key.parent.mkdir()
        key.write_bytes(b"bad\x00credential")
        invalid = run_probe(host, site, invalid_key_home)
        assert "client_1=0" in invalid and "kind=profile_credential_unavailable" in invalid, invalid
        assert "bad\x00credential" not in invalid
    print("model runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
