"""Bounded offline diagnostics, including incomplete and edited journals."""
from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tools.inspect_session import inspect_session, render_report, MAX_RECORD_BYTES


class InspectSessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        (self.path / "snapshot.json").write_text('{}', encoding='utf-8')
        (self.path / "journal.jsonl").write_text('', encoding='utf-8')

    def journal(self, events):
        data = ''.join(json.dumps({'schema_version': 5, 'event_id': index, **event})
                       + '\n' for index, event in enumerate(events, 1))
        (self.path / "ui-events.jsonl").write_text(data, encoding='utf-8')

    def test_usage_and_tool_duration_only_count_completed_calls(self):
        self.journal([
            {'kind': 5, 'tool_call_id': 'a', 'tool_name': 'read', 'occurred_at_us': 1000000},
            {'kind': 6, 'tool_call_id': 'a', 'tool_name': 'read', 'occurred_at_us': 1200000,
             'success': True},
            {'kind': 6, 'tool_call_id': 'missing', 'tool_name': 'read', 'success': False},
            {'kind': 4, 'input_tokens': 12, 'output_tokens': 3},
            {'kind': 10, 'input_tokens': 12, 'output_tokens': 3},
        ])
        before = {p.name: p.read_bytes() for p in self.path.iterdir()}
        report = inspect_session(self.path, 2)
        self.assertEqual(report['model_usage'], {'input_tokens': 12, 'output_tokens': 3})
        self.assertEqual(report['tools']['read'], {'completed': 2, 'failed': 1,
                         'paired': 1, 'elapsed_ms': 200.0})
        self.assertEqual(len(report['recent_events']), 2)
        self.assertEqual(report['warning_count'], 0)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.path.iterdir()})

    def test_malformed_records_and_partial_tail_are_reported(self):
        self.journal([{'kind': 11, 'event_id': 8}, {'kind': 16, 'event_id': 10}])
        with (self.path / 'ui-events.jsonl').open('ab') as file:
            file.write(b'not json\n[]\n{"event_id":11')
        report = inspect_session(self.path)
        self.assertEqual(report['retained_records'], 2)
        self.assertEqual(report['warning_count'], 6)
        self.assertIn('unfinished tail', report['warnings'][-1])

    def test_future_schema_codes_and_truncated_text_are_not_hidden(self):
        self.journal([{'kind': 5, 'schema_version': 99, 'text_truncated': True},
                      {'kind': 500}, {'kind': 'custom_event'}])
        report = inspect_session(self.path)
        self.assertEqual(report['event_counts'], {'custom_event': 1, 'unknown:5': 1, 'unknown:500': 1})
        self.assertIn('truncated text', report['warnings'][0])

    def test_restarted_run_does_not_pair_an_old_call(self):
        self.journal([{'kind': 5, 'run_id': 1, 'tool_call_id': 'x', 'occurred_at_us': 100},
                      {'kind': 0, 'run_id': 1},
                      {'kind': 6, 'run_id': 1, 'tool_call_id': 'x', 'tool_name': 'write',
                       'occurred_at_us': 900}])
        self.assertEqual(inspect_session(self.path)['tools']['write']['paired'], 0)

    def test_oversized_record_is_skipped_without_losing_next_line(self):
        self.journal([{'kind': 11}])
        with (self.path / 'ui-events.jsonl').open('ab') as file:
            file.write(b'X' * (MAX_RECORD_BYTES + 8) + b'\n{"kind":"error"}\n')
        report = inspect_session(self.path)
        self.assertEqual(report['event_counts']['error'], 2)
        self.assertIn('oversized', report['warnings'][0])

    def test_report_escapes_terminal_controls_and_omits_credentials(self):
        self.journal([{'kind': 11, 'text': '\x1b[2Jhello\nnext'}])
        (self.path / 'meta.json').write_text(json.dumps({'title': 'test',
            'api_key': 'never-display'}), encoding='utf-8')
        text = render_report(inspect_session(self.path), True)
        self.assertNotIn('\x1b', text)
        self.assertNotIn('never-display', text)
        self.assertIn('\\u001b', text)

    def test_invalid_input_fails_explicitly(self):
        self.journal([])
        with self.assertRaises(ValueError):
            inspect_session(self.path, 0)
        with self.assertRaises(ValueError):
            inspect_session(self.path / 'missing')
        (self.path / 'ui-events.jsonl').unlink()
        with self.assertRaises(FileNotFoundError):
            inspect_session(self.path)


if __name__ == '__main__':
    unittest.main()
