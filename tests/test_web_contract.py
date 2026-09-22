import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class WebContractTests(unittest.TestCase):
    def test_default_web_settings_are_bounded_and_portable(self) -> None:
        defaults = json.loads((ROOT / "app/default-home/config/defaults.json").read_text(
            encoding="utf-8"))
        web = defaults["settings"]["web"]
        self.assertTrue(web["enabled"])
        self.assertFalse(web["allow_http"])
        self.assertFalse(web["allow_private_networks"])
        self.assertLessEqual(web["max_response_bytes"], 16 * 1024 * 1024)
        self.assertLessEqual(web["max_text_bytes"], 1024 * 1024)
        self.assertLessEqual(web["max_documents"], 128)
        self.assertEqual(web["search"]["provider"], "brave")
        self.assertTrue(web["search"]["endpoint"].startswith("https://"))
        self.assertEqual(web["search"]["secret_ref"],
                         "env:MDO_BRAVE_SEARCH_API_KEY")

    def test_web_tools_are_native_bounded_tools(self) -> None:
        source = (ROOT / "app/src/web/manager.c").read_text(encoding="utf-8")
        for name in ("web_search", "web_open", "web_find"):
            self.assertIn(f'Definitions[', source)
            self.assertIn(f'"{name}"', source)
        self.assertIn("XS_FETCH_PUBLIC_ADDRESSES_ONLY", source)
        self.assertIn("MdoSecretResolve", source)
        self.assertIn("\"untrusted\"", source)
        self.assertIn("MDO_WEB_TOOL_RESULT_LIMIT", source)
        self.assertNotIn("system(", source)

    def test_browser_automation_is_not_merged_into_web_fetch(self) -> None:
        source = (ROOT / "app/src/web/manager.c").read_text(encoding="utf-8")
        self.assertNotIn("browser_click", source)
        self.assertNotIn("browser_screenshot", source)


if __name__ == "__main__":
    unittest.main()
