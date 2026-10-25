"""Do not pack an older native SDK after introducing a required host API."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import build_mdo


class NativeOverlayReceiptTests(unittest.TestCase):
    def test_stale_overlay_is_rejected_and_a_fresh_build_can_be_recorded(self):
        lock={"sdk_overlay":{"sha256":"new-sdk"},"xserver":{"commit":"abcdef0"+"1"*33,"required_extensions":[]}}
        with tempfile.TemporaryDirectory(prefix="native-overlay-")as raw,patch.object(build_mdo,"host_profile_digest",return_value="fixture-profile"):
            host=Path(raw)/"host.exe";host.write_bytes(b"fixture binary")
            receipt=host.with_name(host.name+".build.json")
            value={"schema_version":1,"revision":"abcdef0","profile_sha256":"fixture-profile","extensions":[],"binary_sha256":hashlib.sha256(host.read_bytes()).hexdigest()}
            for overlay in (None,"previous-sdk"):
                old=dict(value)
                if overlay:old["sdk_overlay_sha256"]=overlay
                receipt.write_text(json.dumps(old),encoding="utf-8")
                with self.assertRaises(build_mdo.BuildError):build_mdo.verify_host_receipt(host,lock,True)
            build_mdo.stamp_host_overlay(host,lock)
            build_mdo.verify_host_receipt(host,lock,True)
            host.write_bytes(b"changed binary")
            with self.assertRaises(build_mdo.BuildError):build_mdo.stamp_host_overlay(host,lock)
            with self.assertRaises(build_mdo.BuildError):build_mdo.verify_host_receipt(host,lock,True)


if __name__=="__main__":unittest.main()
