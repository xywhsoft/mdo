"""Static contracts for the MDO-4 Skill catalog API and format."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "app/include/mdo/skills.h"
FORMAT = ROOT / "docs/skill-format.md"
MANAGER = ROOT / "app/src/skills/manager.c"


class SkillContractTests(unittest.TestCase):
    def test_catalog_api_uses_owned_snapshots_and_lazy_content(self) -> None:
        text = HEADER.read_text(encoding="utf-8")
        self.assertIn("typedef struct MdoSkillCatalog MdoSkillCatalog", text)
        self.assertIn("MdoSkillCatalogSnapshot(void)", text)
        self.assertIn("MdoSkillCatalogRef", text)
        self.assertIn("MdoSkillCatalogRelease", text)
        self.assertIn("MdoSkillCatalogLoadBody", text)
        self.assertIn("MdoSkillCatalogLoadResource", text)
        self.assertIn("MDO_SKILL_TRUST_EXTERNAL_REFERENCE", text)

    def test_format_requires_explicit_resources_and_shadowing(self) -> None:
        text = FORMAT.read_text(encoding="utf-8")
        self.assertIn("scripts/", text)
        self.assertIn("templates/", text)
        self.assertIn("assets/", text)
        self.assertIn("cannot be opened", text)
        self.assertIn("never falls back", text)
        self.assertIn("Startup reads only the front matter", text)
        self.assertIn("EXTERNAL_REFERENCE", text)

    def test_manager_validates_and_caches_content_lazily(self) -> None:
        text = MANAGER.read_text(encoding="utf-8")
        self.assertIn("MdoHomeExternalStat", text)
        self.assertIn("MdoHomeOpenRead", text)
        self.assertIn("xrtReadAtFull", text)
        self.assertIn("MdoSkillCatalogLoadBody", text)
        self.assertIn("MdoSkillCatalogLoadResource", text)
        self.assertIn("MdoSkillsSameFile", text)
        self.assertIn("CachedBody", text)
        self.assertIn("MDO_SKILL_DIAGNOSTIC_FRONTMATTER", text)
        self.assertNotIn("xrtVfsReadAll", text)


if __name__ == "__main__":
    unittest.main()
