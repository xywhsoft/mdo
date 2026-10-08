"""Interactive Agent run ownership and retention contracts."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class RunManagerContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.header = (ROOT / "app/include/mdo/runs.h").read_text(encoding="utf-8")
        self.source = (ROOT / "app/src/runs/manager.c").read_text(encoding="utf-8")
        self.bootstrap = (ROOT / "app/src/bootstrap/bootstrap.c").read_text(
            encoding="utf-8"
        )

    def test_public_surface_has_bounded_start_cancel_and_owned_snapshots(self) -> None:
        self.assertIn("#define MDO_RUN_PROMPT_CAPACITY (64u * 1024u + 1u)", self.header)
        self.assertIn("#define MDO_RUN_FINAL_TEXT_LIMIT (64u * 1024u)", self.header)
        self.assertIn("bool MdoRunStart(", self.header)
        self.assertIn("bool MdoRunCancel(", self.header)
        self.assertIn("MdoRunSnapshot* MdoRunSnapshotCreate(", self.header)
        self.assertIn("MdoRunSnapshot* MdoRunSnapshotRef(", self.header)
        self.assertIn("void MdoRunSnapshotRelease(", self.header)
        self.assertIn("bool MdoRunSnapshotResult(", self.header)

    def test_registry_publishes_only_started_runs_and_releases_live_owners(self) -> None:
        self.assertIn("Ready = true;", self.source)
        self.assertIn("if ( Ready && Run != NULL && !Stopping )", self.source)
        self.assertIn("MdoAgentRunDestroy(Items[i].Run);", self.source)
        self.assertIn("MdoSessionRelease(Items[i].Session);", self.source)
        self.assertIn("xrtCancelDestroy(Items[i].Cancel);", self.source)
        self.assertIn("g_MdoRuns.StartingCount != 0u ||", self.source)
        self.assertIn("g_MdoRuns.FinishingCount != 0u", self.source)
        self.assertIn("xrtCondBroadcast(g_MdoRuns.Changed)", self.source)

    def test_resume_rechecks_the_recovery_view_before_start(self) -> None:
        self.assertIn("RecoveryToken", self.header)
        self.assertIn("const xwork_resume_options* ResumeOptions", self.header)
        self.assertIn("MdoAgentSessionRecoverySnapshot", self.source)
        self.assertIn("MdoAgentRecoverySnapshotToken", self.source)
        self.assertIn("strcmp(CurrentToken, ExpectedToken)", self.source)
        self.assertLess(
            self.source.index("MdoRunsRecoveryTokenValid(Agent"),
            self.source.index("MdoAgentRunCreate(Agent"),
        )

    def test_callbacks_are_pinned_and_shutdown_precedes_session_manager(self) -> None:
        self.assertIn("Options->OnOwnerRetain(Options->OwnerUserData)", self.source)
        self.assertIn("Options.OnOwnerRelease(Options.OwnerUserData)", self.source)
        self.assertLess(
            self.bootstrap.index("MdoRunManagerUnit();"),
            self.bootstrap.index("MdoSessionManagerUnit();"),
        )
        self.assertLess(
            self.bootstrap.index("MdoSessionManagerInit(g_MdoBootstrap.Runtime)"),
            self.bootstrap.index("MdoRunManagerInit(g_MdoBootstrap.Runtime"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
