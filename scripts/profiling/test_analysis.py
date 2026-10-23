"""CPU-only reporting regressions; no native binding, capture or server needed."""
import unittest
from types import SimpleNamespace

from analyze_capture import worker_summary


class FrameWorker:
    def __init__(self, on_demand=True):
        self.first = 10_000_000_000 if on_demand else 0
        start = 10_002_000_000 if on_demand else 102_000_000
        marks = [0, 100_000_000]
        if on_demand:
            marks.append(self.first)
        marks.extend([start, start + 16_000_000, start + 33_000_000])
        self.last = start + 34_000_000
        self.boundaries = list(zip(marks, marks[1:] + [self.last]))

    def get_gpu_contexts(self):
        return [SimpleNamespace(name='GPU', type='Direct3D12', count=7, thread=0)]

    def get_capture_name(self): return 'frame-boundary regression'
    def get_capture_program(self): return 'test'
    def get_first_time(self): return self.first
    def get_last_time(self): return self.last
    def get_frame_boundaries(self): return self.boundaries
    def get_frame_count(self): return len(self.boundaries)
    def get_zone_count(self): return 42
    def get_all_zone_stats(self): return {}
    def get_all_gpu_zone_stats(self): return {}
    def get_threads(self): return []


class RecordedFrameReportingTests(unittest.TestCase):
    def test_on_demand_excludes_boot_connection_and_trailing_intervals(self):
        result = worker_summary(FrameWorker())
        self.assertEqual(result['raw_frame_count'], 6)
        self.assertEqual(result['bounded_frame_count'], 2)
        self.assertEqual(result['excluded_frame_interval_count'], 4)
        self.assertEqual(result['frame_times']['count'], 2)
        self.assertEqual(result['frame_times']['mean_ms'], 16.5)
        self.assertEqual(result['frame_times']['max_ms'], 17)
        self.assertEqual(result['cpu_zone_event_count'], 42)
        self.assertEqual(result['gpu_zone_event_count'], 7)

    def test_requested_skip_is_after_automatic_boundary_filter(self):
        result = worker_summary(FrameWorker(), skip_frames=1)
        self.assertEqual(result['bounded_frame_count'], 2)
        self.assertEqual(result['frame_skip_applied'], 1)
        self.assertEqual(result['frame_times']['count'], 1)
        self.assertEqual(result['frame_times']['mean_ms'], 17)
        self.assertEqual(result['frame_histogram_window_ns']['begin'], 10_018_000_000)

    def test_non_on_demand_still_excludes_synthetic_and_trailing_marks(self):
        result = worker_summary(FrameWorker(on_demand=False))
        self.assertEqual(result['raw_frame_count'], 5)
        self.assertEqual(result['bounded_frame_count'], 2)
        self.assertEqual(result['excluded_frame_interval_count'], 3)
        self.assertEqual(result['frame_times']['mean_ms'], 16.5)

    def test_skip_past_complete_window_is_empty_and_counted(self):
        result = worker_summary(FrameWorker(), skip_frames=100)
        self.assertEqual(result['frame_skip_requested'], 100)
        self.assertEqual(result['frame_skip_applied'], 2)
        self.assertEqual(result['frame_times'], {'count': 0})
        self.assertIsNone(result['frame_histogram_window_ns'])


if __name__ == '__main__':
    unittest.main()
