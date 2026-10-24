"""Check release probe dispatch without requiring a device or a compiler."""
from pathlib import Path
import ast
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import qa_release
import prepare_release
from test_optional_tools_runtime import run as optional_tool_probe


class ReleaseGateTests(unittest.TestCase):
    def test_native_workbench_probes_use_the_packed_candidate(self):
        commands = qa_release.native_workbench_probe_commands(Path("candidate.exe"), Path("evidence"))
        self.assertEqual(len(commands), 5)
        for mode, command in zip(("start", "display", "application", "changes", "terminal"), commands):
            self.assertTrue(Path(command[1]).is_file())
            self.assertEqual(command[2:5], ["--packed", "candidate.exe", "--" + mode])
            self.assertEqual(command[5:], ["--evidence", str(Path("evidence") / ("native-" + mode))])

    def test_optional_tool_payload_probes_require_an_explicit_stage(self):
        class Inventory:
            def glob(self, _):
                return [Path("test_optional_tools_runtime.py"), Path("test_api_runtime.py")]
        with patch.object(qa_release, "TESTS", Inventory()), patch.object(qa_release, "run") as run, patch("builtins.print") as emit:
            self.assertEqual(qa_release.runtime_probes(Path("host"), Path("packed")), 1)
            self.assertTrue(any("ARTIFACT CHECK NOT RUN" in call.args[0] for call in emit.call_args_list))
            run.reset_mock()
            self.assertEqual(qa_release.runtime_probes(Path("host"), Path("packed"),
                optional_tools_stage=Path("stage"), optional_tools_platform="windows-x86_64"), 2)
            command = next(call.args[0] for call in run.call_args_list if "test_optional_tools_runtime.py" in call.args[0][1])
            self.assertEqual(command[2:], ["--stage", "stage", "--platform", "windows-x86_64"])
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            (stage / "input.json").write_text("[]", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "No optional tools selected"):
                optional_tool_probe(stage, "windows-x86_64", None)

    def test_concurrent_release_input_changes_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="release-inputs-") as temporary:
            root = Path(temporary)
            (root / "app").mkdir()
            (root / "app/main.c").write_text("original")
            (root / "tools").mkdir()
            (root / "tools/host-profile.json").write_text("profile")
            (root / "deps.lock").write_text("dependency")
            with patch.object(qa_release, "ROOT", root):
                baseline = qa_release.release_inputs()
                qa_release.verify_release_inputs(baseline)
                for name in ("app/main.c", "deps.lock", "tools/host-profile.json"):
                    file = root / name
                    original = file.read_text()
                    file.write_text("concurrent edit")
                    with self.assertRaises(qa_release.GateError):
                        qa_release.verify_release_inputs(baseline)
                    file.write_text(original)
                (root / "app/new.c").write_text("added during tests")
                with self.assertRaises(qa_release.GateError):
                    qa_release.verify_release_inputs(baseline)

    def test_packed_recovery_probes_use_this_candidate_and_required_report(self):
        candidate = Path("final-candidate")
        commands = qa_release.packed_probe_commands(candidate, Path("qa-output"))
        self.assertEqual(len(commands), 13)
        for command in commands:
            self.assertTrue(Path(command[1]).is_file())
            self.assertEqual(command[2:4], ["--packed", str(candidate)])
        fork = next(c for c in commands if c[1].endswith("test_packed_fork_recovery.py"))
        self.assertEqual(fork[4:], ["--report", str(Path("qa-output/fork-recovery.json"))])
        grep = next(c for c in commands if c[1].endswith("test_grep_regex_packed.py"))
        self.assertEqual(grep[4:], ["--report", str(Path("qa-output/grep-regex.json"))])

    def test_publication_rejects_stale_source_or_dependency_records(self):
        record = {"version_name": "0.1.0", "xs_commit": "current", "xrt_commit": "runtime",
                  "application_source_sha256": "source", "host_profile_sha256": "profile", "toolpack_revision": 2,
                  "products": [{"filename": row[3]} for row in prepare_release.TARGETS]}
        release = {"version_name": "0.1.0", "toolpack_revision": 2}
        lock = {"xserver": {"commit": "current"}, "xrt": {"commit": "runtime"}}
        prepare_release.verify_record(record, release, lock, "source", "profile")
        for changed in ({**record, "xs_commit": "old"}, {**record, "version_name": "0.1.0-dev"},
                        {**record, "xrt_commit": "old"}, {**record, "host_profile_sha256": "old-profile"},
                        {**record, "toolpack_revision": 1},
                        {**record, "application_source_sha256": "old-source"},
                        {**record, "products": record["products"][:-1]}):
            with self.assertRaises(ValueError):
                prepare_release.verify_record(changed, release, lock, "source", "profile")

    def test_each_desktop_runtime_probe_has_a_supported_command(self):
        device = {"test_android_conversation_runtime.py", "test_android_device_runtime.py"}
        host, packed = Path("candidate-host"), Path("candidate-packed")
        for probe in sorted((ROOT / "tests").glob("test_*_runtime.py")):
            if probe.name in device:
                continue
            source = probe.read_text(encoding="utf-8")
            command = qa_release.runtime_probe_command(probe, host, packed)
            for option in command[2::2]:
                if option.startswith("--"):
                    self.assertIn(option, source, probe.name)
            for call in ast.walk(ast.parse(source)):
                if not isinstance(call, ast.Call) or not isinstance(call.func, ast.Attribute) or call.func.attr != "add_argument":
                    continue
                if any(k.arg == "required" and isinstance(k.value, ast.Constant) and k.value.value is True for k in call.keywords):
                    option = call.args[0].value
                    # A probe may offer alternative packed and development modes.
                    if option == "--host" and any(arg.startswith("--packed") for arg in command):
                        continue
                    self.assertIn(option, command, probe.name)
            # Packed probes must never silently test the old root executable.
            if "--packed" in command or "--packed-path" in command:
                self.assertIn(str(packed), command)

    def test_device_probes_are_reported_and_desktop_probes_still_run(self):
        class Inventory:
            def glob(self, _):
                return [Path("test_android_device_runtime.py"), Path("test_api_runtime.py"),
                        Path("test_android_conversation_runtime.py")]
        with patch.object(qa_release, "TESTS", Inventory()), patch.object(qa_release, "run") as run, patch("builtins.print") as emit:
            self.assertEqual(qa_release.runtime_probes(Path("host"), Path("packed")), 1)
            self.assertEqual(run.call_count, 1)
            self.assertIn("test_api_runtime.py", run.call_args.args[0][1])
            self.assertEqual(sum("DEVICE CHECK NOT RUN" in c.args[0] for c in emit.call_args_list), 2)


if __name__ == "__main__":
    unittest.main()
