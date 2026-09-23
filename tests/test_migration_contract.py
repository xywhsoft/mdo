"""Static contracts for explicit, non-destructive legacy migration."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
HEADER = (ROOT / "app/include/mdo/migration.h").read_text(encoding="utf-8")
SOURCE = (ROOT / "app/src/migration/manager.c").read_text(encoding="utf-8")
SOURCES = (ROOT / "app/sources.json").read_text(encoding="utf-8")
API = (ROOT / "app/src/api/migration.c").read_text(encoding="utf-8")
ROUTER = (ROOT / "app/src/api/router.c").read_text(encoding="utf-8")


class MigrationContractTests(unittest.TestCase):
    def test_preview_is_public_and_has_a_confirmation_token(self) -> None:
        for symbol in (
            "MdoLegacyMigrationPreview",
            "MdoLegacyMigrationDiscover",
            "MdoMigrationPreview",
            "PreviewToken",
            "ConflictCount",
            "UnsupportedCount",
        ):
            self.assertIn(symbol, HEADER)
        self.assertIn('"src/migration/manager.c"', SOURCES)

    def test_only_the_two_documented_legacy_sources_are_resolved(self) -> None:
        self.assertIn("xrtPathExecutable()", SOURCE)
        self.assertIn('xrtPathJoin(Parent, "data")', SOURCE)
        self.assertIn("xrtPathHome()", SOURCE)
        self.assertIn('xrtPathJoin(Base, ".mdo")', SOURCE)

    def test_preview_is_bounded_anchored_and_rejects_links(self) -> None:
        for token in (
            "MDO_MIGRATION_MAX_FILES",
            "MDO_MIGRATION_MAX_ENTRIES",
            "MDO_MIGRATION_MAX_TOTAL",
            "MDO_MIGRATION_MAX_DEPTH",
            "xrtRootOpen(SourceAbsolute)",
            "xrtRootDirOpen(Root, Path, XDIR_STAT)",
            "XFILE_NOFOLLOW",
            "link or unsupported file type",
        ):
            self.assertIn(token, SOURCE)

    def test_preview_is_content_bound_and_never_writes(self) -> None:
        self.assertIn("xrtSha256Update", SOURCE)
        self.assertIn("xrtSha256Final", SOURCE)
        self.assertIn("legacy source changed", SOURCE)
        for forbidden in (
            "MdoHomeOpenWrite",
            "MdoHomeAtomicWrite",
            "xrtDirCreateAll",
            "xrtPathRename",
            "xrtDirRemoveAll",
        ):
            self.assertNotIn(forbidden, SOURCE)

    def test_read_only_api_exposes_the_full_preview(self) -> None:
        self.assertIn('"/api/v1/migrations/legacy"', ROUTER)
        self.assertIn("XHTTP_METHOD_GET | XHTTP_METHOD_HEAD", ROUTER)
        self.assertIn("MdoLegacyMigrationDiscover", API)
        self.assertIn('"requires_confirmation"', API)
        for field in (
            "source_id", "source_path", "target_path", "found", "valid",
            "importable", "file_count", "conflict_count",
            "unsupported_count", "preview_token",
        ):
            self.assertIn(f'"{field}"', API)


if __name__ == "__main__":
    unittest.main()
