"""The Windows soak observer itself must keep a fixed ctypes type cache."""
import ctypes
import os
import unittest
from test_remote_soak import memory


class ObserverMemory(unittest.TestCase):
    @unittest.skipUnless(os.name=='nt','Windows process metrics')
    def test_repeated_samples_do_not_retain_new_structure_types(self):
        memory(os.getpid()); before=len(ctypes._pointer_type_cache)
        for _ in range(500):
            self.assertGreater(memory(os.getpid())['private'],0)
        self.assertEqual(len(ctypes._pointer_type_cache),before)


if __name__=='__main__': unittest.main()
