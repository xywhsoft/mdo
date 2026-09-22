"""MDO-7E durable schedule API and storage contracts."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class ScheduleContractTests(unittest.TestCase):
    def test_public_api_is_explicit_and_bounded(self) -> None:
        header = (ROOT / "app/include/mdo/schedules.h").read_text(encoding="utf-8")
        for token in (
            "MdoScheduleCreateOptions",
            "MdoScheduleClaim",
            "MdoScheduleManagerInit",
            "MdoScheduleCreate",
            "MdoScheduleSetEnabled",
            "MdoScheduleClaimDue",
            "MdoScheduleFinishTask",
            "MdoScheduleCatalogSnapshot",
            "MDO_SCHEDULE_INPUT_CAPACITY",
        ):
            self.assertIn(token, header)
        self.assertIn("int64 StartAt;", header)
        self.assertIn("uint64 DefinitionRevision;", header)

    def test_manager_uses_xwork_scheduler_and_explicit_clock(self) -> None:
        source = (ROOT / "app/src/schedules/manager.c").read_text(encoding="utf-8")
        for call in (
            "xworkRuntimeRegisterSchedule",
            "xworkRuntimeRestoreSchedule",
            "xworkRuntimeSetScheduleEnabled",
            "xworkRuntimeClaimDueSchedule",
            "xworkRuntimeFinishScheduledTask",
            "xworkScheduleNextOccurrence",
        ):
            self.assertIn(call, source)
        self.assertNotIn("xrtThreadCreate", source)
        self.assertIn("bool MdoScheduleClaimDue(int64 Now", source)

    def test_store_is_revisioned_locked_and_audited(self) -> None:
        source = (ROOT / "app/src/schedules/manager.c").read_text(encoding="utf-8")
        for token in (
            '"schema_version"',
            '"revision"',
            '"runtime_generation"',
            '"next_occurrence_at_us"',
            '"schedules/.writer.lock"',
            '"schedules/audit.jsonl"',
            '"schedules/history/%s.jsonl"',
            "MdoHomeAtomicWrite",
            "xrtFileLock",
            "input_sha256",
        ):
            self.assertIn(token, source)
        self.assertIn("ExpectedRevision", (ROOT / "app/include/mdo/schedules.h").read_text(
            encoding="utf-8"))

    def test_bootstrap_owns_schedule_lifecycle(self) -> None:
        source = (ROOT / "app/src/bootstrap/bootstrap.c").read_text(encoding="utf-8")
        header = (ROOT / "app/include/mdo/bootstrap.h").read_text(encoding="utf-8")
        self.assertIn("MdoScheduleManagerInit(g_MdoBootstrap.Runtime)", source)
        self.assertLess(source.index("MdoSessionManagerUnit();"),
                        source.index("MdoScheduleManagerUnit();"))
        self.assertIn("MDO_BOOTSTRAP_SCHEDULES_READY", header)
        self.assertIn("ScheduleDiagnosticCount", header)
        self.assertIn("SchedulesEnabled", header)

    def test_schedule_switch_is_part_of_typed_config(self) -> None:
        header = (ROOT / "app/include/mdo/config.h").read_text(encoding="utf-8")
        source = (ROOT / "app/src/config/config.c").read_text(encoding="utf-8")
        defaults = (ROOT / "app/default-home/config/defaults.json").read_text(
            encoding="utf-8")
        self.assertIn("bool SchedulesEnabled;", header)
        self.assertIn('MdoConfigKey("schedules")', source)
        self.assertIn('"schedules": true', defaults)


if __name__ == "__main__":
    unittest.main(verbosity=2)
