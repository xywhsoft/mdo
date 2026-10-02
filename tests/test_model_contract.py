"""MDO-6A model provider and immutable catalog contracts."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "app/include/mdo/models.h"
SOURCE = ROOT / "app/src/models/catalog.c"
BOOTSTRAP = ROOT / "app/src/bootstrap/bootstrap.c"


class ModelContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.defaults = json.loads(
            (ROOT / "app/default-home/config/defaults.json").read_text(
                encoding="utf-8"))
        self.header = HEADER.read_text(encoding="utf-8")
        self.source = SOURCE.read_text(encoding="utf-8")

    def test_provider_and_model_are_separate(self) -> None:
        models = self.defaults["models"]
        self.assertEqual(len(models["providers"]), 1)
        self.assertEqual(len(models["items"]), 1)
        provider = models["providers"][0]
        model = models["items"][0]
        self.assertEqual(model["provider"], provider["id"])
        self.assertIn("credential", provider)
        self.assertNotIn("credential", model)
        self.assertIn("endpoints", provider)
        self.assertNotIn("endpoints", model)

    def test_ling_declares_three_online_wire_dialects(self) -> None:
        provider = self.defaults["models"]["providers"][0]
        model = self.defaults["models"]["items"][0]
        self.assertEqual(set(provider["endpoints"]), {
            "chat_completions", "responses", "anthropic_messages",
        })
        self.assertEqual(set(model["protocols"]), {
            "openai-chat-completions", "openai-responses",
            "anthropic-messages",
        })

    def test_public_catalog_is_snapshot_based_and_non_secret(self) -> None:
        for symbol in (
            "MdoModelCatalogSnapshot", "MdoModelCatalogRef",
            "MdoModelCatalogRelease", "MdoModelCatalogDefault",
            "MdoModelCatalogProfile", "MdoModelClientCreate",
        ):
            self.assertIn(symbol, self.header)
        self.assertIn("HasCredentialReference", self.header)
        self.assertNotIn("const char* CredentialReference", self.header)
        self.assertNotIn("ApiKey", self.header)
        self.assertNotIn("Secret", self.header)
        self.assertIn("xrtAtomic32CompareExchange", self.source)
        self.assertIn("MdoModelCatalogRef(pCandidate)", self.source)

    def test_profile_maps_protocol_to_xllm_provider(self) -> None:
        self.assertIn("XLLM_PROVIDER_OPENAI_COMPAT", self.source)
        self.assertIn("XLLM_PROVIDER_OPENAI_RESPONSES", self.source)
        self.assertIn("XLLM_PROVIDER_ANTHROPIC", self.source)
        self.assertIn("xllmModelProfileValidate", self.source)

    def test_client_factory_resolves_late_and_clears_temporary_key(self) -> None:
        secrets = (ROOT / "app/src/security/secrets.c").read_text(
            encoding="utf-8")
        self.assertIn("MdoSecretResolve", self.source)
        self.assertIn("MdoSecretRelease(&Secret)", self.source)
        self.assertIn("xllmClientCreate(&Config", self.source)
        self.assertIn("MDO_ORNITH_RESPONSES_URL", self.source)
        self.assertIn("xrtSecureZero", secrets)

    def test_bootstrap_owns_model_manager_before_runtime(self) -> None:
        text = BOOTSTRAP.read_text(encoding="utf-8")
        self.assertLess(text.index("MdoModelManagerInit()"),
                        text.index("xworkRuntimeCreate"))
        self.assertIn("MDO_BOOTSTRAP_MODELS_READY", text)
        self.assertIn("MdoModelManagerUnit()", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
