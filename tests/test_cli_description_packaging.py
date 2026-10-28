"""Exercise real portable archive generation; payloads are never executed."""
import json
from pathlib import Path
import zipfile
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/runtime/build"))
from optional_tools import package


class DescriptionPackagingTests(unittest.TestCase):
    def test_description_and_prompt_are_independent_archive_fields(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp); stage = base / "stage"; stage.mkdir(); (stage / "tool.exe").write_bytes(b"Not executable")
            metadata = dict(id="fixture", name="Fixture", version="1.0.0", platform="windows-x86_64", description="For humans", prompt="MODEL_ONLY", entry="tool.exe", license="MIT", copyright="Copyright Original Developer", links=[], commands=[dict(name="fixture", entry="tool.exe", args=[])])
            row = package(stage, metadata, base)
            with zipfile.ZipFile(base/row['file']) as archive:
                self.assertIsNone(archive.testzip());manifest=json.loads(archive.read('extension.json'));config=json.loads(archive.read('tool.json'))
                self.assertEqual(archive.read('tool.exe'),b'Not executable')
            self.assertEqual(manifest["description"], metadata["description"])
            self.assertEqual(config["prompt"], metadata["prompt"])
            self.assertEqual(manifest["copyright"], metadata["copyright"])
            self.assertNotIn('files',manifest);self.assertNotIn('base64',manifest);self.assertEqual(manifest['kind'],'external-tools')
            old = {k: v for k, v in metadata.items() if k not in ("description", "copyright")}
            self.assertEqual(package(stage, old, base)["description"], "")
            self.assertEqual(package(stage, old, base)["copyright"], "")
            for invalid in (None, [], "x"*4097, "nul\0text"):
                with self.assertRaises(ValueError): package(stage, {**metadata, "copyright": invalid}, base)
            old["id"] = "git"
            self.assertTrue(package(stage, old, base)["description"])
            for invalid in (None, [], "x" * 2049, "nul\0text"):
                with self.assertRaises(ValueError): package(stage, {**metadata, "description": invalid}, base)


if __name__ == "__main__": unittest.main()
