"""Cross-language version ordering and release/Android migration contracts."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import version_code


class VersionCodeTests(unittest.TestCase):
    def test_hex_bytes_and_prefix_independent_order(self):
        for text, value in (("60000000", 0x60000000), ("00000601", 0x601),
                            ("00.01.00.00", 0x10000), ("ff.ff.ff.ff", 0xFFFFFFFF)):
            self.assertEqual(version_code.parse(text), value)
        self.assertLess(version_code.parse("60000000") & version_code.ORDER_MASK,
                        version_code.parse("00000601") & version_code.ORDER_MASK)
        self.assertLess(0x6000FFFF & version_code.ORDER_MASK, version_code.VERSION_1_0)

    def test_malformed_or_numeric_codes_are_not_guessed(self):
        for text in (None, 60000000, "", "601", "0x00000601", "000006010", "00000601\n",
                     " 00000601", "00.0.06.01", "00-00-06-01", "0000060G", "０００００６０１"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                version_code.parse(text)

    def test_official_prefix_cutover_and_initial_bridge(self):
        for name, code in (("0.6.0", "60000000"), ("0.6.1", "60000601"),
                           ("0.10.0", "60000A00"), ("0.255.255", "6000FFFF"),
                           ("1.0.0", "00010000"), ("255.255.255", "00FFFFFF")):
            self.assertEqual(version_code.release_code(dict(version_name=name, version_code=code)), code)
        for name, code in (("1.0.0", "60010000"), ("0.6.1", "00000601"),
                           ("0.6.1", "60000602"), ("256.0.0", "00000000")):
            with self.subTest(name=name), self.assertRaises(ValueError):
                version_code.release_code(dict(version_name=name, version_code=code))

    def test_actual_release_retains_monotonic_android_install_ids(self):
        release = json.loads((ROOT / "app/release.json").read_text(encoding="utf-8"))
        self.assertEqual(version_code.release_code(release), release["version_code"])
        for edition, previous in (("lite", 60000007), ("full", 60000008)):
            self.assertGreater(release[f"android_{edition}_build_id"], previous)
            self.assertLessEqual(release[f"android_{edition}_build_id"], 2_100_000_000)

    def test_shared_c_codec_and_uint32_boundaries(self):
        header = ROOT / "app/include/mdo/version_code.h"
        server = ROOT.parent / "home/host/xywhsoft_ai/plugin/mdo/include/version_code.h"
        if server.is_file():
            self.assertEqual(header.read_bytes(), server.read_bytes())
        compiler = shutil.which("gcc")
        if not compiler:
            self.skipTest("gcc unavailable for native codec regression")
        with tempfile.TemporaryDirectory(prefix="version-code-") as raw:
            path = Path(raw)
            source = path / "check.c"
            source.write_text('#include "' + header.as_posix() + '"\n' + r'''
int main(void) {
    uint32_t value=0;char text[9];
    const char* bad[]={"", "0x00000601", "000006010", "00000601\n", "00.0.06.01", "0000060G"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)if(MdoVersionCodeParse(bad[i],&value))return 1;
    if(MdoVersionCodeParse(NULL,&value)||MdoVersionCodeParse("00000601",NULL))return 2;
    if(!MdoVersionCodeParse("ff.ff.ff.ff",&value)||value!=UINT32_MAX)return 3;
    MdoVersionCodeFormat(value,text);if(strcmp(text,"FFFFFFFF"))return 4;
    if(MdoVersionCodeCompare(0x60000000,0x00000601)>=0)return 5;
    if(MdoVersionCodeCompare(0x6000FFFF,0x00010000)>=0)return 6;
    if(MdoVersionCodeCompare(0x60000601,0x00010000)>=0)return 7;
    for(unsigned tag=0;tag<256;++tag) {
        uint32_t code=(tag<<24)|0x00000601;
        if(MdoVersionCodeCompare(code,0x601))return 8;
        MdoVersionCodeFormat(code,text);
        if(!MdoVersionCodeParse(text,&value)||value!=code)return 9;
    }
    return 0;
}
''', encoding="utf-8")
            exe = path / "check.exe"
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(exe)], check=True, capture_output=True)
            subprocess.run([str(exe)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
