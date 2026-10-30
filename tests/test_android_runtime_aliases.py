"""APK payloads may share exact bytes, but library identity must never conflict."""
import importlib.util
from contextlib import nullcontext
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("runtime_bundle", ROOT / "tools/android/runtime_bundle.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class RuntimeAliases(unittest.TestCase):
    def pack(self, rows, aliases=None, *, full_policy=False):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            defaults = {"busybox/busybox": b"\x7fELFbusybox"}
            if any(name.startswith('git/') for name in rows):
                defaults['git/git'] = b'\x7fELFgit'
            if any(name.startswith('ffmpeg/') for name in rows):
                defaults['ffmpeg/ffmpeg'] = b'\x7fELFffmpeg'
            self.recipes = {}
            contents = {**defaults, **rows}
            for name, data in contents.items():
                p = root / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data)
            if aliases is not None:
                p = root / "git/native-aliases.json"; p.parent.mkdir(exist_ok=True); p.write_text(json.dumps(aliases))
            out = io.BytesIO()
            # Alias/ELF contracts use tiny selected tool sets. Policy tests
            # below exercise the real, fixed full-APK selection without mocks.
            selection = tuple(sorted({name.split('/')[0] for name in contents}))
            policy = nullcontext() if full_policy else patch.object(bundle, 'FULL_APK_TOOLS', selection)
            with policy, zipfile.ZipFile(out, "w") as archive:
                bundle.add_runtime(archive, root, "full", 50000017, 4)
            with zipfile.ZipFile(out) as archive:
                paths = archive.namelist()
                meta = json.loads(archive.read("assets/mdo-runtime.json"))
                self.extension_ids = {extension['id'] for extension in meta['extensions']}
                for extension in meta['extensions']:
                    payload=archive.read(extension['asset'])
                    self.assertEqual(__import__('hashlib').sha256(payload).hexdigest(),extension['sha256'])
                    with zipfile.ZipFile(io.BytesIO(payload)) as tool:
                        self.assertIsNone(tool.testzip())
                        manifest=json.loads(tool.read('extension.json'))
                        self.assertEqual(manifest['kind'],'external-tools')
                        self.assertIn('tool.json',tool.namelist())
                        self.assertNotIn('files',manifest)
                        self.recipes[extension['id']] = json.loads(tool.read('tool.json'))
            return paths, {r['tool']+'/'+r["path"]: r for r in meta["files"]}

    @staticmethod
    def original_tools():
        return {name: b'\x7fELF' + name.encode() for name in (
            'busybox/busybox', 'curl/curl', 'jq/jq', 'openssh/ssh',
            'aria2/aria2c', 'ripgrep/rg', '7zip/7zz', 'python/bin/python3')}

    def test_full_contains_only_original_eight_tools_even_when_store_tools_exist(self):
        rows = {**self.original_tools(), **{name: b'\x7fELFextra' for name in (
            'git/git', 'sqlite/sqlite3', 'poppler/pdfinfo', 'ffmpeg/ffmpeg',
            'imagemagick/magick', 'future-store-tool/program')}}
        paths, files = self.pack(rows, full_policy=True)
        expected = {'busybox','curl','jq','openssh','aria2','ripgrep','7zip','python'}
        self.assertEqual(self.extension_ids, expected)
        self.assertEqual({name.split('/')[0] for name in files}, expected)
        self.assertEqual({name for name in paths if name.startswith('assets/mdo-extensions/')},
                         {'assets/mdo-extensions/' + name + '.zip' for name in expected})
        self.assertFalse(any('git' in name or 'ffmpeg' in name or 'future-store-tool' in name for name in paths))

    def test_full_refuses_an_incomplete_original_tool_set(self):
        rows = self.original_tools(); del rows['python/bin/python3']
        with self.assertRaisesRegex(ValueError, 'Missing bundled Android tool: python'):
            self.pack(rows, full_policy=True)

    def test_identical_libraries_and_programs_share_payload(self):
        paths, files = self.pack({"git/git": b"\x7fELFgit", "git/alias": b"\x7fELFgit", "git/libshared.so": b"\x7fELFlib", "ffmpeg/libshared.so": b"\x7fELFlib"}, {"git/libexec/git-core/git-add": "git/git"})
        self.assertEqual(files["git/git"]["native"], files["git/alias"]["native"])
        self.assertEqual(files["git/git"]["native"], files["git/libexec/git-core/git-add"]["native"])
        self.assertEqual(files["git/libshared.so"]["native"], files["ffmpeg/libshared.so"]["native"])
        self.assertEqual(len([p for p in paths if p.startswith("lib/")]), 4)

    def test_optional_tools_export_programs_and_companions_not_libraries(self):
        self.pack({'poppler/lib/libfirst.so': b'\x7fELFlibrary',
                   'poppler/pdfinfo': b'\x7fELFpdfinfo',
                   'poppler/pdftotext': b'\x7fELFpdftotext',
                   'poppler/libexec/private': b'\x7fELFprivate',
                   'imagemagick/lib/libfirst.so': b'\x7fELFlibrary',
                   'imagemagick/magick': b'\x7fELFmagick',
                   'ffmpeg/ffprobe': b'\x7fELFffprobe'})
        for tool, names in [('poppler', {'pdfinfo', 'pdftotext'}),
                            ('imagemagick', {'magick'}),
                            ('ffmpeg', {'ffmpeg', 'ffprobe'})]:
            recipe = self.recipes[tool]
            self.assertEqual({row['name'] for row in recipe['commands']}, names)
            self.assertIn(recipe['program'], names)

    def test_shared_libraries_alone_do_not_define_an_external_tool(self):
        with self.assertRaisesRegex(ValueError, 'No native program for poppler'):
            self.pack({'poppler/lib/libfirst.so': b'\x7fELFlibrary'})

    def test_conflicting_soname_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "conflicting native"):
            self.pack({"git/libshared.so": b"\x7fELFa", "ffmpeg/libshared.so": b"\x7fELFb"})

    def test_cpp_shared_library_keeps_its_loader_identity(self):
        paths, files = self.pack({"ffmpeg/libmdo_ext_c++_shared.so": b"\x7fELFC++"})
        self.assertEqual(files["ffmpeg/libmdo_ext_c++_shared.so"]["native"], "libmdo_ext_c++_shared.so")
        self.assertIn("lib/arm64-v8a/libmdo_ext_c++_shared.so", paths)
        with self.assertRaisesRegex(ValueError, "invalid native runtime name"):
            self.pack({"ffmpeg/libbad name.so": b"\x7fELFbad"})

    def test_aliases_cannot_escape_or_replace_files(self):
        for alias, target in [("git/../escape", "git/git"), ("other/alias", "git/git"), ("git/git", "git/git"), ("git/alias", "missing"), ("git/line\nbreak", "git/git")]:
            with self.subTest(alias=alias), self.assertRaises(ValueError):
                self.pack({"git/git": b"\x7fELFgit"}, {alias: target})

    def test_lite_never_includes_tool_programs(self):
        out = io.BytesIO()
        with zipfile.ZipFile(out, "w") as archive:
            bundle.add_runtime(archive, ROOT / "tools/runtime/android-arm64-v8a", "lite", 50000015, 4)
        with zipfile.ZipFile(out) as archive:
            self.assertEqual(archive.namelist(), ["assets/mdo-runtime.json"])
            metadata = json.loads(archive.read("assets/mdo-runtime.json"))
            self.assertEqual(metadata["files"], [])
            self.assertEqual(metadata['extensions'],[])
            self.assertEqual(metadata["build_id"], "50000015")
            self.assertEqual(metadata["version_code"], json.loads((ROOT / "app/release.json").read_text())["version_code"])


if __name__ == "__main__": unittest.main()
