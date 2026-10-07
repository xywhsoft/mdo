"""Small local Git fixtures for restoring an unpublished locked host revision."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import build_mdo


def git(root, *args):
    return subprocess.check_output(['git', *args], cwd=root, stderr=subprocess.DEVNULL).decode().strip()


class HostBundle(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='host-bundle-',dir=build_mdo.ROOT/'.build')
        self.base=Path(self.temp.name);self.root=self.base/'mdo';self.root.mkdir()
        self.source=self.base/'source';self.source.mkdir()
        git(self.source,'init');git(self.source,'config','user.name','Fixture')
        git(self.source,'config','user.email','fixture@example.invalid')
        (self.source/'source.c').write_text('base\n');git(self.source,'add','source.c')
        git(self.source,'commit','-m','base');base=git(self.source,'rev-parse','HEAD')
        self.clone=self.base/'xs';git(self.base,'clone',str(self.source),str(self.clone))
        git(self.source,'checkout','-b','codex/recovery')
        (self.source/'source.c').write_text('repaired\n');git(self.source,'commit','-am','repaired')
        self.expected=git(self.source,'rev-parse','HEAD')
        self.bundle=self.root/'host.bundle'
        git(self.source,'bundle','create',str(self.bundle),'codex/recovery','^'+base)
        self.lock={'xserver':{'commit':self.expected,'source_bundle':{
            'path':'host.bundle','ref':'refs/heads/codex/recovery',
            'sha256':hashlib.sha256(self.bundle.read_bytes()).hexdigest()}}}
        self.old_root=build_mdo.ROOT;build_mdo.ROOT=self.root
        self.addCleanup(self.cleanup)

    def cleanup(self):
        build_mdo.ROOT=self.old_root
        checkout=self.root/'.build/locked-xs'/self.expected
        if checkout.exists():git(self.clone,'worktree','remove',str(checkout))
        self.temp.cleanup()

    def test_fresh_clone_imports_only_into_separate_checkout(self):
        original=git(self.clone,'rev-parse','HEAD')
        (self.clone/'source.c').write_text('user changes\n')
        path=build_mdo.restore_bundled_xserver([self.clone],self.lock)
        self.assertEqual(git(path,'rev-parse','HEAD'),self.expected)
        self.assertEqual((path/'source.c').read_text(),'repaired\n')
        self.assertEqual(git(self.clone,'rev-parse','HEAD'),original)
        self.assertEqual((self.clone/'source.c').read_text(),'user changes\n')
        self.assertEqual(build_mdo.restore_bundled_xserver([self.clone],self.lock),path)

    def test_corrupt_bundle_does_not_mutate_source(self):
        before=git(self.clone,'rev-parse','HEAD');self.bundle.write_bytes(b'corrupt')
        with self.assertRaises(build_mdo.BuildError):
            build_mdo.restore_bundled_xserver([self.clone],self.lock)
        self.assertEqual(git(self.clone,'rev-parse','HEAD'),before)
        self.assertFalse((self.root/'.build/locked-xs').exists())

    def test_explicit_source_path_still_requires_the_locked_revision(self):
        with self.assertRaises(build_mdo.BuildError):
            build_mdo.find_xserver(self.clone,self.lock)
        self.assertFalse((self.root/'.build/locked-xs').exists())

if __name__=='__main__':unittest.main()
