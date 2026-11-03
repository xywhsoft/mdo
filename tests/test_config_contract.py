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

    def test_models_start_unconfigured(self) -> None:
        self.assertEqual(self.defaults["models"], {"default_model": "", "providers": [], "items": []})

    def test_configuration_files_are_user_patches(self) -> None:
        self.assertIn('"config/settings.json"', self.source)
        self.assertIn('"config/models.json"', self.source)
        self.assertIn('"config/permissions.json"', self.source)
        self.assertIn('MdoConfigNormalizePatch', self.source)
        self.assertIn('MdoHomeAtomicWrite', self.source)

    def test_secrets_remain_references_and_retired_models_are_not_protected(self) -> None:
        self.assertIn("MdoConfigSecretsValidate", self.source)
        self.assertNotIn("pProtectedModel", self.source)
        self.assertIn("MdoConfigRetireBuiltinPatch", self.source)

    def test_preview_and_commit_share_the_same_prepare_path(self) -> None:
        self.assertGreaterEqual(self.source.count("MdoConfigPrepareImportLocked("), 3)


if __name__ == "__main__":
    unittest.main()
