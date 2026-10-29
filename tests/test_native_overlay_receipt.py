"""Do not pack an older native SDK after introducing a required host API."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import build_mdo
import build_android


class NativeOverlayReceiptTests(unittest.TestCase):
    def test_android_fresh_packer_is_stamped_before_reuse_validation(self):
        lock=json.loads((build_mdo.ROOT/'deps.lock').read_text(encoding='utf-8'))
        class ReachedPack(RuntimeError):pass
        with tempfile.TemporaryDirectory(prefix='android-host-receipt-')as raw:
            root=Path(raw);app=root/'app';app.mkdir();host_dir=root/'host';host_dir.mkdir()
            names=('xs.exe',)if os.name=='nt'else('xs',)
            def build_or_pack(command,**kwargs):
                if Path(command[1]).name!='build.py':raise ReachedPack()
                self.assertEqual(command[command.index('--frontend')+1],'none')
                self.assertEqual(Path(command[command.index('--output')+1]).name,names[0])
                for name in names:
                    host=host_dir/name;host.write_bytes(b'new Android packer')
                    receipt={'schema_version':1,'profile_sha256':build_mdo.host_profile_digest(False),
                        'revision':lock['xserver']['commit'],'extensions':lock['xserver']['required_extensions'],
                        'binary_sha256':hashlib.sha256(host.read_bytes()).hexdigest(),
                        'icon_sha256':hashlib.sha256(build_mdo.ICON_PATH.read_bytes()).hexdigest()}
                    host.with_name(name+'.build.json').write_text(json.dumps(receipt),encoding='utf-8')
            with patch.object(build_mdo,'APP',app),patch.object(build_mdo,'find_xserver',return_value=root/'sdk'), \
                    patch.object(build_mdo,'verify_dependencies'),patch.object(build_mdo,'prepare'), \
                    patch.object(build_android.subprocess,'run',side_effect=build_or_pack):
                with self.assertRaises(ReachedPack):
                    build_android.main(['--sdk','unused','--java-home','unused','--host-dir',str(host_dir),'--build-dir',str(root/'out')])
            for name in names:
                receipt=json.loads((host_dir/(name+'.build.json')).read_text(encoding='utf-8'))
                self.assertEqual(receipt['sdk_overlay_sha256'],lock['sdk_overlay']['sha256'])

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
