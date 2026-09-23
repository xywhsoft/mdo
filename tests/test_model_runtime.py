"""Bounded MDO-6A model catalog probe through the real xs/TCC runtime."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PROBE_SOURCE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>

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
    unsigned protocol;
    (void)host;

    if ( !MdoHomeInit() || !MdoConfigInit() || !MdoModelManagerInit() ) {
        PrintError("probe_init_error");
        printf("probe_done=1\n");
        return;
    }
    first = MdoModelCatalogSnapshot();
    printf("catalog_one=%llu providers=%zu models=%zu\n",
        (unsigned long long)MdoModelManagerGeneration(),
        MdoModelCatalogProviderCount(first), MdoModelCatalogModelCount(first));
    memset(&provider, 0, sizeof(provider));
    provider.Size = sizeof(provider);
    printf("provider_ok=%d\n",
        MdoModelCatalogProviderFind(first, "ling", &provider) ? 1 : 0);
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
    printf("profile_bad=%d\n", MdoModelCatalogProfile(first, model.Id,
        (MdoModelProtocol)99, &profile, &error) ? 1 : 0);
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
        printf("client_%u=%d generation=%llu output=%u error=%s\n",
            protocol, client != NULL ? 1 : 0,
            (unsigned long long)client_info.ModelGeneration,
            (unsigned)client_info.MaxOutputTokens,
            error.sMessage[0] != '\0' ? error.sMessage : "none");
        if ( client != NULL ) xllmClientDestroy(client);
    }
    client_options.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    client_options.ReasoningEffort = "unsupported";
    memset(&error, 0, sizeof(error));
    {
        xllm_client* client = MdoModelClientCreate(first, &client_options,
            NULL, &error);
        printf("client_bad_reasoning=%d\n", client != NULL ? 1 : 0);
        if ( client != NULL ) xllmClientDestroy(client);
    }
    client_options.ReasoningEffort = NULL;
    client_options.MaxOutputTokens = 16385u;
    memset(&error, 0, sizeof(error));
    {
        xllm_client* client = MdoModelClientCreate(first, &client_options,
            NULL, &error);
        printf("client_bad_output=%d\n", client != NULL ? 1 : 0);
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


def write_site(site: Path) -> int:
    (site / "web").mkdir(parents=True)
    (site / "default-home/config").mkdir(parents=True)
    (site / "src/storage").mkdir(parents=True)
    (site / "src/config").mkdir(parents=True)
    (site / "src/security").mkdir(parents=True)
    (site / "src/models").mkdir(parents=True)
    (site / "include/mdo").mkdir(parents=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json",
        "src/storage/home.c", "src/config/config.c",
        "src/security/secrets.c", "src/models/catalog.c",
        "include/mdo/home.h", "include/mdo/config.h", "include/mdo/models.h",
        "include/mdo/secrets.h", "include/mdo/version.h",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
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
              environment: dict[str, str] | None = None) -> str:
    command = [str(host), "xs.json", "--", "--home", str(home)]
    process_environment = os.environ.copy()
    for name in (
        "MDO_LING_CHAT_COMPLETIONS_URL", "MDO_LING_RESPONSES_URL",
        "MDO_LING_ANTHROPIC_URL", "MDO_LING_API_KEY",
    ):
        process_environment.pop(name, None)
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
    done.wait(timeout=10.0)
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
        output = run_probe(host, site, base / "state", {
            "MDO_LING_CHAT_COMPLETIONS_URL": "https://example.invalid/v1",
            "MDO_LING_RESPONSES_URL": "https://example.invalid/v1",
            "MDO_LING_ANTHROPIC_URL": "https://example.invalid",
            "MDO_LING_API_KEY": "bounded-runtime-probe-key",
        })
        assert "probe_init_error=" not in output, output
        assert "catalog_one=1 providers=1 models=1" in output, output
        assert "provider_ok=1" in output, output
        assert "provider=ling protocols=7 credential_ref=1 verify=1" in output, output
        assert "default_ok=1" in output, output
        assert "model=ling-3.0-tiny wire=ling-3.0-tiny" in output, output
        assert "context=131072 output=16384 efforts=4" in output, output
        assert "profile_1=1 provider=0" in output, output
        assert "profile_2=1 provider=2" in output, output
        assert "profile_3=1 provider=3" in output, output
        assert "profile_bad=0" in output, output
        assert "client_1=1 generation=1 output=16384 error=none" in output, output
        assert "client_2=1 generation=1 output=16384 error=none" in output, output
        assert "client_3=1 generation=1 output=16384 error=none" in output, output
        assert "client_bad_reasoning=0" in output, output
        assert "client_bad_output=0" in output, output
        assert "reload=1" in output, output
        assert "catalog_two=2 old_default=1 new_default=1" in output, output
        assert "probe_done=1" in output, output
    print("model runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
