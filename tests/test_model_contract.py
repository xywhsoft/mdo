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

    def test_no_product_connection_is_preinstalled(self) -> None:
        self.assertEqual(self.defaults["models"], {"default_model": "", "providers": [], "items": []})

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
        self.assertNotIn("MDO_ORNITH_RESPONSES_URL", self.source)
        self.assertNotIn("MdoModelsBuiltinCredential", self.source)
        self.assertIn("xrtSecureZero", secrets)

    def test_bootstrap_owns_model_manager_before_runtime(self) -> None:
        text = BOOTSTRAP.read_text(encoding="utf-8")
        self.assertLess(text.index("MdoModelManagerInit()"),
                        text.index("xworkRuntimeCreate"))
        self.assertIn("MDO_BOOTSTRAP_MODELS_READY", text)
        self.assertIn("MdoModelManagerUnit()", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
