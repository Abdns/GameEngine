"""CPU numerical references for GI. Run: python tests/gi_reference_tests.py

These tests do not execute HLSL or prove GPU correctness. The DDA reference follows
GiSampling.h, while its oracle independently intersects every occupied voxel AABB.
Shader parameters are read from GiInterop.h. Keep the reference in sync when the
shader traversal changes; shader compilation and GPU validation remain separate.
"""

from __future__ import annotations

import math
from pathlib import Path
import random
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
INTEROP = ROOT / "engine/source/Graphics/Vulkan/shaders/interop"
CONSTANTS = (INTEROP / "GiInterop.h").read_text(encoding="utf-8")


def scalar_define(name):
    value = re.search(r"^#define\s+" + name + r"\s+([0-9.eE+-]+)[fu]?\s*$", CONSTANTS, re.M)
    if not value:
        raise ValueError(f"Expected scalar shader setting {name}")
    return float(value.group(1))


GRID = int(scalar_define("VOLUME_GRID_SIZE"))
EXTENT = scalar_define("VOLUME_WORLD_EXTENT")
TILE = int(scalar_define("RC_SCREEN_TILE"))
MAX_X = int(scalar_define("RC_SCREEN_MAX_X"))
MAX_Y = int(scalar_define("RC_SCREEN_MAX_Y"))
HISTORY_SECONDS = scalar_define("GI_HISTORY_SECONDS")


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def normalized(v):
    length = math.sqrt(dot(v, v))
    if length == 0:
        raise ValueError("A ray must have a nonzero direction")
    return tuple(x / length for x in v)


def dda_hit(occupied, origin, direction, start, span, size=GRID, extent=EXTENT):
    """Double-precision model of GiTraceVoxel; no radiance or shader execution."""
    if span <= 0:
        return None
    ray = normalized(direction)
    inverse = tuple(1.0 / r if abs(r) > 1e-8 else 1e20 for r in ray)
    a = tuple((-extent - o) * inv for o, inv in zip(origin, inverse))
    b = tuple((extent - o) * inv for o, inv in zip(origin, inverse))
    enter = max(start, max(min(x, y) for x, y in zip(a, b)))
    leave = min(start + span, min(max(x, y) for x, y in zip(a, b)))
    if leave <= enter:
        return None
    cell_size = 2 * extent / size
    travel = enter
    cell = [min(size - 1, max(0, math.floor((o + r * travel + extent) / cell_size)))
            for o, r in zip(origin, ray)]
    step = [1 if r >= 0 else -1 for r in ray]
    boundary = [-extent + (c + (s > 0)) * cell_size for c, s in zip(cell, step)]
    next_cross = [(p - o) * inv if abs(r) > 1e-8 else 1e20
                  for p, o, inv, r in zip(boundary, origin, inverse, ray)]
    delta = [abs(inv) * cell_size for inv in inverse]
    for _ in range(3 * size + 3):
        if travel >= leave or any(c < 0 or c >= size for c in cell):
            return None
        crossing = min(next_cross)
        if min(leave, crossing) > travel and tuple(cell) in occupied:
            return tuple(cell)
        advance = [n <= crossing for n in next_cross]
        for axis in range(3):
            if advance[axis]:
                cell[axis] += step[axis]
                next_cross[axis] += delta[axis]
        travel = crossing
    raise AssertionError("DDA exceeded its geometric step bound")


def brute_hit(occupied, origin, direction, start, span, size=GRID, extent=EXTENT):
    """Independent oracle: intersect each occupied AABB, choose nearest interval.

    Voxel ownership is half open [lo, hi) on parallel axes. A zero-length tangent
    contact does not occupy a ray interval. No traversal steps, entry nudges, or
    simultaneous-crossing tolerances are shared with the shader model.
    """
    if span <= 0:
        return None
    ray = normalized(direction)
    cell_size = 2 * extent / size
    hits = []
    for cell in occupied:
        entry, exit_ = start, start + span
        for axis in range(3):
            lo = -extent + cell[axis] * cell_size
            hi = lo + cell_size
            o, r = origin[axis], ray[axis]
            if r == 0:
                if not lo <= o < hi:
                    break
            else:
                times = sorted(((lo - o) / r, (hi - o) / r))
                entry = max(entry, times[0])
                exit_ = min(exit_, times[1])
            if exit_ <= entry:
                break
        else:
            if exit_ > entry:
                hits.append((entry, cell))
    return min(hits)[1] if hits else None


def oct_decode(f):
    n = [f[0], f[1], 1 - abs(f[0]) - abs(f[1])]
    t = max(-n[2], 0)
    n[0] += -t if n[0] >= 0 else t
    n[1] += -t if n[1] >= 0 else t
    return normalized(n)


def spherical_triangle(a, b, c):
    return 2 * math.atan2(abs(dot(a, cross(b, c))),
                          max(1 + dot(a, b) + dot(b, c) + dot(c, a), 1e-8))


def oct_solid_angle(u, v, resolution):
    lo = (2 * u / resolution - 1, 2 * v / resolution - 1)
    hi = (2 * (u + 1) / resolution - 1, 2 * (v + 1) / resolution - 1)
    a, b = oct_decode(lo), oct_decode((hi[0], lo[1]))
    c, d = oct_decode(hi), oct_decode((lo[0], hi[1]))
    return spherical_triangle(a, b, c) + spherical_triangle(a, c, d)


def filter_radiance(previous, current, occupancy, history_valid, dt, seconds):
    """Inject's temporal contract: only RGB is filtered; occupancy is current."""
    if not occupancy:
        return (0.0, 0.0, 0.0, 0.0)
    response = 1 - math.exp(-max(dt, 0) / seconds) if seconds > 0 else 1
    if not history_valid:
        response = 1
    return tuple(old + (new - old) * response
                 for old, new in zip(previous[:3], current)) + (float(occupancy),)


def screen_probe_count(pixels, cap):
    return min((pixels + TILE - 1) // TILE, cap)


def screen_sample(index, pixels):
    return min(index * TILE + TILE // 2, pixels - 1)


class OpaqueTraversalTests(unittest.TestCase):
    def assert_trace(self, occupied, origin, direction, start=0, span=100, size=8, extent=4):
        expected = brute_hit(occupied, origin, direction, start, span, size, extent)
        result = dda_hit(occupied, origin, direction, start, span, size, extent)
        self.assertEqual(result, expected,
                         (origin, direction, start, span, expected, result))

    def test_parallel_axes_and_both_directions(self):
        for axis in range(3):
            for sign in (-1, 1):
                origin = [-3.5, -3.5, -3.5]
                origin[axis] = sign * 5
                direction = [0, 0, 0]
                direction[axis] = -sign
                self.assert_trace({(0, 0, 0)}, origin, direction)
                origin[(axis + 1) % 3] = 4.25
                self.assert_trace({(0, 0, 0)}, origin, direction)

    def test_diagonal_rays_and_exact_cell_boundaries(self):
        occupied = {(0, 0, 0), (3, 3, 3), (7, 7, 7), (4, 4, 4)}
        for origin, direction in (
                ((-5, -5, -5), (1, 1, 1)),
                ((5, 5, 5), (-1, -1, -1)),
                ((0, 0, 0), (1, 1, 1)),
                ((0, 0, 0), (-1, -1, -1)),
                ((-4, -3.5, -3.5), (1, 0, 0)),
                ((4, -3.5, -3.5), (-1, 0, 0)),
                ((-5, -4, -3.5), (1, 0, 0)),
                ((-5, 4, -3.5), (1, 0, 0))):
            self.assert_trace(occupied, origin, direction)

    def test_interval_start_and_end_do_not_skip_blockers(self):
        self.assert_trace({(0, 0, 0)}, (-3.00005, -3.5, -3.5), (1, 0, 0), span=2)
        self.assert_trace({(0, 0, 0)}, (-3.5, -3.5, -3.5), (1, 0, 0), span=0.00001)
        self.assert_trace({(2, 0, 0)}, (-5, -3.5, -3.5), (1, 0, 0), span=3)
        self.assert_trace({(2, 0, 0)}, (-5, -3.5, -3.5), (1, 0, 0), start=3, span=1)
        self.assert_trace({(2, 0, 0)}, (-5, -3.5, -3.5), (1, 0, 0), span=0)

    def test_nearly_simultaneous_crossings_preserve_short_intersections(self):
        self.assert_trace({(0, 1, 0)}, (-3.5, -3.5, -3.5), (1, 1.000001, 0), span=2)

    def test_seeded_random_sparse_grids_against_slab_oracle(self):
        rng = random.Random(90210)
        for trial in range(1500):
            occupied = {tuple(rng.randrange(8) for _ in range(3)) for _ in range(45)}
            origin = tuple(rng.uniform(-6, 6) for _ in range(3))
            direction = [rng.uniform(-1, 1) for _ in range(3)]
            if trial % 3 == 0:
                direction[(trial // 3) % 3] = 0
            start, span = rng.uniform(0, 2), rng.uniform(0.1, 15)
            self.assert_trace(occupied, origin, direction, start, span)

    def test_current_scene_scale_and_full_diagonal(self):
        occupied = {(GRID - 1,) * 3}
        self.assert_trace(occupied, (-EXTENT,) * 3, (1, 1, 1),
                          span=2 * EXTENT * math.sqrt(3), size=GRID, extent=EXTENT)

    def test_visible_neighbor_can_have_unrelated_directional_occlusion(self):
        # The receiver sees the neighbor, but their parallel far rays encounter
        # different geometry. A segment-visible probe is not an occlusion oracle
        # for the receiver. This fixture motivated receiver-ray confirmation.
        occupied = {(4, 4, 4)}
        receiver = (-2.0, 1.5, 0.5)
        neighbor = (-2.0, 0.5, 0.5)
        direction = (1.0, 0.0, 0.0)
        self.assertIsNone(brute_hit(occupied, receiver, (0, -1, 0), 0, 1, 8, 4))
        self.assertIsNotNone(brute_hit(occupied, neighbor, direction, 1.3125, 12, 8, 4))
        self.assertIsNone(brute_hit(occupied, receiver, direction, 1.3125, 12, 8, 4))
        self.assert_trace(occupied, receiver, direction, start=1.3125, span=12)

    def test_receiver_confirmation_keeps_real_occlusion(self):
        occupied = {(4, 4, 4), (4, 5, 4)}
        receiver = (-2.0, 1.5, 0.5)
        self.assertEqual(brute_hit(occupied, receiver, (1, 0, 0), 1.3125, 12, 8, 4),
                         (4, 5, 4))
        self.assert_trace(occupied, receiver, (1, 0, 0), start=1.3125, span=12)


class TemporalTests(unittest.TestCase):
    def test_removed_surface_stops_emitting_and_occluding_immediately(self):
        for dt in (0, 1 / 240, 1 / 30, 0.2):
            self.assertEqual(filter_radiance((12, 5, 1, 1), (0, 0, 0), 0, False,
                                             dt, HISTORY_SECONDS), (0, 0, 0, 0))
        grid = {(4, 4, 4)}
        self.assertIsNotNone(dda_hit(grid, (0.01, 0.01, 0.01), (1, 0, 0), 0, 10, 8, 4))
        self.assertIsNone(dda_hit(set(), (0.01, 0.01, 0.01), (1, 0, 0), 0, 10, 8, 4))

    def test_new_or_changed_surface_rejects_history(self):
        self.assertEqual(filter_radiance((8, 2, 9, 0), (1, 3, 2), 1, False,
                                         0, HISTORY_SECONDS), (1, 3, 2, 1))

    def test_occupancy_does_not_ramp_with_filtered_radiance(self):
        result = filter_radiance((0, 0, 0, 1), (1, 1, 1), 1, True,
                                 1 / 120, HISTORY_SECONDS)
        self.assertTrue(0 < result[0] < 1)
        self.assertEqual(result[3], 1)

    def test_equal_elapsed_time_matches_analytic_response(self):
        previous, current = (1.0, 3.0, 2.0, 1.0), (4.0, 0.5, 8.0)
        duration = 0.4
        analytic = tuple(new + (old - new) * math.exp(-duration / HISTORY_SECONDS)
                         for old, new in zip(previous, current))
        partitions = [[1 / fps] * round(duration * fps) for fps in (30, 60, 120, 240)]
        partitions.append([0.01, 0.06, 0.004, 0.126, 0.2])
        for frames in partitions:
            self.assertAlmostEqual(sum(frames), duration)
            value = previous
            for dt in frames:
                value = filter_radiance(value, current, 1, True, dt, HISTORY_SECONDS)
            for actual, expected in zip(value, analytic):
                self.assertAlmostEqual(actual, expected, places=12)

    def test_disabled_temporal_filter_uses_current_light(self):
        self.assertEqual(filter_radiance((1, 1, 1, 1), (2, 3, 4), 1, True, 0, 0),
                         (2, 3, 4, 1))


class AngularIntegrationTests(unittest.TestCase):
    def test_octahedral_texels_cover_one_sphere(self):
        for resolution in (4, 8, 16, 32, 64, 128):
            weights = [oct_solid_angle(u, v, resolution)
                       for v in range(resolution) for u in range(resolution)]
            self.assertGreater(min(weights), 0)
            self.assertAlmostEqual(math.fsum(weights), 4 * math.pi, places=11)

    def test_constant_radiance_is_independent_of_receiver_orientation(self):
        radiance = (0.2, 1.5, 4.0)
        for resolution in (4, 8, 16):
            for normal in ((1, 0, 0), (0, -1, 0), normalized((1, 2, 3)),
                           normalized((-5, 1, -2))):
                total = [0, 0, 0]
                weight_sum = 0
                for v in range(resolution):
                    for u in range(resolution):
                        ray = oct_decode((2 * (u + 0.5) / resolution - 1,
                                          2 * (v + 0.5) / resolution - 1))
                        weight = max(dot(normal, ray), 0) * oct_solid_angle(u, v, resolution)
                        weight_sum += weight
                        for channel in range(3):
                            total[channel] += radiance[channel] * weight
                for actual, expected in zip(total, radiance):
                    self.assertAlmostEqual(actual / weight_sum, expected, places=12)


class ScreenSamplingTests(unittest.TestCase):
    def test_odd_tiny_and_oversize_framebuffers_stay_inside_allocations(self):
        dimensions = [(1, 1), (2, 3), (3, 2), (5, 6), (1279, 719), (1281, 721),
                      (1919, 1079), (1921, 1081), (3840, 2160), (3841, 2161),
                      (7680, 4320)]
        for width, height in dimensions:
            for pixels, cap in ((width, MAX_X), (height, MAX_Y)):
                count = screen_probe_count(pixels, cap)
                self.assertTrue(0 < count <= cap)
                for index in range(count):
                    self.assertTrue(0 <= screen_sample(index, pixels) < pixels)
                if pixels <= cap * TILE:
                    self.assertLess(pixels - 1 - screen_sample(count - 1, pixels), TILE)

    def test_shared_merge_gather_has_one_probe_per_group(self):
        group = int(scalar_define("RC_GROUP_SIZE"))
        base_resolution = int(scalar_define("RC_DIR_RES"))
        first = int(scalar_define("RC_SCREEN_HANDOFF"))
        last = int(scalar_define("RC_CASCADE_COUNT"))
        tile_size = int(scalar_define("RC_PROBE_SIZE")) * base_resolution
        for cascade in range(first, last - 1):
            resolution = base_resolution << cascade
            if resolution < group:
                continue  # The shader uses the per-thread fallback in this case.
            self.assertEqual(tile_size % group, 0)
            self.assertEqual(resolution % group, 0)
            for begin in range(0, tile_size, group):
                self.assertEqual(begin // resolution, (begin + group - 1) // resolution)


if __name__ == "__main__":
    unittest.main(verbosity=2)
