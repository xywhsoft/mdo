"""MDO-1 dual Home source contracts."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
HOME_C = (ROOT / "app" / "src" / "storage" / "home.c").read_text(encoding="utf-8")
HOME_H = (ROOT / "app" / "include" / "mdo" / "home.h").read_text(encoding="utf-8")
BOOTSTRAP_C = (ROOT / "app" / "src" / "bootstrap" / "bootstrap.c").read_text(
    encoding="utf-8")


class HomeContractTests(unittest.TestCase):
    def test_public_operations_are_distinct(self) -> None:
        for symbol in (
            "MdoResourceOpenRead",
            "MdoHomeOpenWrite",
            "MdoResourceMaterialize",
        ):
            self.assertIn(symbol, HOME_H)
            self.assertIn(symbol, HOME_C)

    def test_external_overlay_is_read_only_and_higher_priority(self) -> None:
        self.assertIn("xrtVfsDiskCreate(g_MdoHome.Path, XVFS_DISK_READ)", HOME_C)
        self.assertIn('"/app/default-home", 1000', HOME_C)
        self.assertIn("xrtVfsOpen(g_MdoHome.ApplicationVfs", HOME_C)

    def test_writes_are_anchored_and_lazy(self) -> None:
        self.assertIn("xrtDirCreateAll(g_MdoHome.Path)", HOME_C)
        self.assertIn("xrtRootFileOpen(Root, Path", HOME_C)
        self.assertIn("XFILE_NOFOLLOW", HOME_C)
        init = HOME_C.index("bool MdoHomeInit(void)")
        ensure = HOME_C.index("static bool MdoHomeEnsureLocked(void)")
        self.assertNotIn("xrtDirCreateAll", HOME_C[init:])
        self.assertIn("xrtDirCreateAll", HOME_C[ensure:init])

    def test_home_override_prefers_application_arguments(self) -> None:
        resolver = HOME_C[HOME_C.index("static char* MdoHomeResolvePath"):
                          HOME_C.index("static char* MdoResourcePath")]
        self.assertIn("xsAppArgumentCount()", resolver)
        self.assertIn('strcmp(sArgument, "--home")', resolver)
        self.assertIn('xrtEnvLookup("MDO_HOME"', resolver)
        self.assertLess(resolver.index("xsAppArgumentCount()"),
                        resolver.index('xrtEnvLookup("MDO_HOME"'))

    def test_bootstrap_owns_one_long_lived_runtime(self) -> None:
        self.assertEqual(BOOTSTRAP_C.count("xworkRuntimeCreate("), 1)
        self.assertEqual(BOOTSTRAP_C.count("xworkRuntimeRelease("), 1)
        self.assertLess(BOOTSTRAP_C.index("MdoHomeInit()"),
                        BOOTSTRAP_C.index("xworkRuntimeCreate("))

    def test_legacy_source_is_not_referenced(self) -> None:
        self.assertNotIn("app_bak", HOME_C)
        self.assertNotIn("app_bak", BOOTSTRAP_C)


if __name__ == "__main__":
    unittest.main(verbosity=2)
