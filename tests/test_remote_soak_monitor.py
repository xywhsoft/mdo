"""The Windows soak observer itself must keep a fixed ctypes type cache."""
import ctypes
import os
import unittest
from test_remote_soak import memory, observe_clock, record_failure, SoakGateError


class ObserverMemory(unittest.TestCase):
    def test_clock_adjustment_and_execution_gap_have_distinct_receipts(self):
        state={}
        observe_clock(state,100,99,10,1000,1090)
        with self.assertRaises(SoakGateError) as failed:
            observe_clock(state,101,100,10,1000,1097)
        record_failure(state,failed.exception)
        self.assertEqual(state['failure_code'],'clock_diverged')
        self.assertEqual(state['clock']['offset_seconds'],6)
        self.assertEqual(state['clock']['tick_gap_seconds'],1)
        self.assertFalse(state['acceptance_72h'])
        with self.assertRaises(SoakGateError) as failed:
            observe_clock(state,117,101)
        self.assertEqual(failed.exception.code,'observation_gap')
        self.assertEqual(state['clock']['tick_gap_seconds'],16)

    def test_original_limits_are_not_relaxed_and_error_text_is_not_recorded(self):
        for args in [(100,85,None,None,None),(100,99,10,1000,1095),
                     (100,99,10,1000,1085),(99,100,None,None,None)]:
            with self.subTest(args=args),self.assertRaises(SoakGateError):
                observe_clock({},*args)
        state={}
        try:
            raise RuntimeError('secret bearer credential must stay private')
        except RuntimeError as error:
            record_failure(state,error)
        self.assertEqual(state['failure_code'],'unclassified')
        self.assertTrue(state['error_frames'])
        self.assertNotIn('secret bearer',str(state))

    @unittest.skipUnless(os.name=='nt','Windows process metrics')
    def test_repeated_samples_do_not_retain_new_structure_types(self):
        memory(os.getpid()); before=len(ctypes._pointer_type_cache)
        for _ in range(500):
            self.assertGreater(memory(os.getpid())['private'],0)
        self.assertEqual(len(ctypes._pointer_type_cache),before)


if __name__=='__main__': unittest.main()
