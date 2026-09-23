import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class ConfigContractTests(unittest.TestCase):
    def test_runtime_updates_are_revisioned_transactions(self) -> None:
        public = (ROOT / "app/include/mdo/settings.h").read_text(encoding="utf-8")
        service = (ROOT / "app/src/config/service.c").read_text(encoding="utf-8")
        self.assertIn("ExpectedRevision", public)
        self.assertIn("MDO_SETTINGS_STATUS_CONFLICT", public)
        self.assertIn("MDO_SETTINGS_STATUS_ROLLBACK_FAILED", public)
        self.assertIn("MdoConfigExport", service)
        self.assertIn("MdoConfigImport(Domain", service)
        self.assertIn("MdoSettingsRollbackRuntime", service)
        self.assertIn("g_MdoSettings.Degraded = true", service)
        self.assertIn("MdoConfigGetSnapshot(&Snapshot->Config)", service)
        self.assertIn("MdoConfigGetAgentSettings(&Snapshot->Agent)", service)
        self.assertIn("MdoConfigGetWebSettings(&Snapshot->Web)", service)

    def setUp(self) -> None:
        self.defaults = json.loads(
            (ROOT / "app/default-home/config/defaults.json").read_text(
                encoding="utf-8"))
        self.source = (ROOT / "app/src/config/config.c").read_text(
            encoding="utf-8")

    def test_defaults_have_versioned_three_domain_shape(self) -> None:
        self.assertEqual(self.defaults["schema_version"], 1)
        self.assertEqual(
            set(self.defaults),
            {"schema_version", "settings", "models", "permissions"},
        )

    def test_ling_is_protected_and_declares_all_online_protocols(self) -> None:
        provider = next(item for item in self.defaults["models"]["providers"]
                        if item["id"] == "ling")
        ling = next(item for item in self.defaults["models"]["items"]
                    if item["id"] == "ling-3.0-tiny")
        self.assertTrue(provider["builtin"])
        self.assertFalse(provider["editable"])
        self.assertFalse(provider["removable"])
        self.assertEqual(provider["credential"]["secret_ref"],
                         "env:MDO_LING_API_KEY")
        self.assertEqual(set(provider["endpoints"]), {
            "chat_completions", "responses", "anthropic_messages",
        })
        self.assertTrue(ling["builtin"])
        self.assertTrue(ling["free"])
        self.assertFalse(ling["editable"])
        self.assertFalse(ling["removable"])
        self.assertEqual(ling["provider"], provider["id"])
        self.assertEqual(ling["wire_model"], "ling-3.0-tiny")
        self.assertEqual(set(ling["protocols"]), {
            "openai-chat-completions",
            "openai-responses",
            "anthropic-messages",
        })
        self.assertIn(ling["default_protocol"], ling["protocols"])
        self.assertEqual(ling["window"]["context_tokens"], 131072)
        self.assertGreater(ling["window"]["max_output_tokens"], 0)
        self.assertIn("streaming", ling["capabilities"])
        self.assertIn(ling["default_reasoning_effort"],
                      ling["reasoning_efforts"])

    def test_configuration_files_are_user_patches(self) -> None:
        self.assertIn('"config/settings.json"', self.source)
        self.assertIn('"config/models.json"', self.source)
        self.assertIn('"config/permissions.json"', self.source)
        self.assertIn('MdoConfigNormalizePatch', self.source)
        self.assertIn('MdoHomeAtomicWrite', self.source)

    def test_secret_and_ling_rules_are_server_side(self) -> None:
        self.assertIn('MdoConfigSecretsValidate', self.source)
        self.assertIn('MDO_CONFIG_ERROR_PROTECTED', self.source)
        self.assertIn('xrtValueEqual(pProvider, pProtectedProvider)', self.source)
        self.assertIn('xrtValueEqual(pItem, pProtectedModel)', self.source)

    def test_preview_and_commit_share_the_same_prepare_path(self) -> None:
        self.assertGreaterEqual(self.source.count("MdoConfigPrepareImportLocked("), 3)


if __name__ == "__main__":
    unittest.main()
