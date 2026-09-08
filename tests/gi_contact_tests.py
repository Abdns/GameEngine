"""CPU regressions for GI origin placement and receiver shell exclusion.

Run: python tests/gi_contact_tests.py
The fixtures voxelize analytic sphere surfaces and planes by geometric bounds;
they do not execute the mesh sampling shader. They isolate origin placement from
sampling holes, radiance, temporal filtering, and the final lighting response.
The receiver traversal is a CPU model; these tests do not execute HLSL.
"""

import math
import random
import unittest

from gi_reference_tests import EXTENT, GRID, dda_hit, dot, normalized, scalar_define


CELL = 2 * EXTENT / GRID
SEARCH_CELLS = scalar_define("GI_SURFACE_SEARCH_VOXELS")
SIZE = 32
DOMAIN = SIZE * CELL / 2


def position(coordinate):
    return tuple(value * CELL - DOMAIN for value in coordinate)


def voxel(point):
    return tuple(math.floor((value + DOMAIN) / CELL) for value in point)


def shifted(point, normal, distance):
    return tuple(p + n * distance for p, n in zip(point, normal))


def add_plane(cells, axis, coordinate, normal):
    index = math.floor(coordinate)
    other = [a for a in range(3) if a != axis]
    for first in range(SIZE):
        for second in range(SIZE):
            cell = [0, 0, 0]
            cell[axis], cell[other[0]], cell[other[1]] = index, first, second
            cells[tuple(cell)] = normal


def add_sphere(cells, center, radius):
    # Independent sphere/AABB shell intersection: closest and farthest corner
    # distances bracket r. Interior air is intentionally absent from occupancy,
    # as in a surface voxelizer; teleporting into it must never be called safe.
    for z in range(SIZE):
        for y in range(SIZE):
            for x in range(SIZE):
                cell = (x, y, z)
                lo = position(cell)
                hi = tuple(value + CELL for value in lo)
                nearest = sum(max(a - c, c - b, 0) ** 2
                              for a, b, c in zip(lo, hi, center))
                farthest = sum(max(abs(a - c), abs(b - c)) ** 2
                               for a, b, c in zip(lo, hi, center))
                if nearest <= radius * radius <= farthest:
                    mid = tuple((a + b) * 0.5 for a, b in zip(lo, hi))
                    cells[cell] = normalized(tuple(p - c for p, c in zip(mid, center)))


def old_origin(cells, local, normal):
    origin = shifted(local, normal, CELL * 1.05)
    for _ in range(4):
        if voxel(origin) not in cells:
            break
        origin = shifted(origin, normal, CELL * 0.25)
    return origin


def surface_origin(cells, local, normal):
    length2 = dot(normal, normal)
    if length2 <= 1e-12 or not all(math.isfinite(value) for value in normal):
        return local
    normal = normalized(normal)
    epsilon = CELL * 0.001
    near = shifted(local, normal, epsilon)
    origin, travel = near, epsilon
    for _ in range(8):
        cell = voxel(origin)
        if any(index < 0 or index >= SIZE for index in cell):
            return origin
        if cell not in cells:
            return origin
        if dot(cells[cell], normal) < -0.1:
            return near
        boundary = tuple(-DOMAIN + (index + (n > 0)) * CELL
                         for index, n in zip(cell, normal))
        distance = [(b - p) / n if abs(n) > 1e-8 else 1e20
                    for b, p, n in zip(boundary, origin, normal)]
        advance = max(min(distance), 0) + epsilon
        if travel + advance > CELL * SEARCH_CELLS:
            return near
        origin = shifted(origin, normal, advance)
        travel += advance
    return near


def receiver_hit(cells, origin, direction, normal, start=0, span=DOMAIN * 2):
    """Strict DDA plus a bounded receiver-only initial shell exemption."""
    ray = normalized(direction)
    inverse = tuple(1 / r if abs(r) > 1e-8 else 1e20 for r in ray)
    a = tuple((-DOMAIN - o) * inv for o, inv in zip(origin, inverse))
    b = tuple((DOMAIN - o) * inv for o, inv in zip(origin, inverse))
    enter = max(start, max(min(x, y) for x, y in zip(a, b)))
    leave = min(start + span, min(max(x, y) for x, y in zip(a, b)))
    if leave <= enter:
        return None
    valid_normal = dot(normal, normal) > 1e-12 and all(math.isfinite(n) for n in normal)
    normal = normalized(normal) if valid_normal else (0, 0, 0)
    prefix = start <= 0 and enter <= 0 and valid_normal and dot(normal, ray) > 0
    cell = list(voxel(shifted(origin, ray, enter)))
    cell = [min(SIZE - 1, max(0, c)) for c in cell]
    step = [1 if r >= 0 else -1 for r in ray]
    next_cross = [((-DOMAIN + (c + (s > 0)) * CELL - o) * inv) if abs(r) > 1e-8 else 1e20
                  for c, s, o, inv, r in zip(cell, step, origin, inverse, ray)]
    delta = [abs(inv) * CELL for inv in inverse]
    travel = enter
    for i in range(3 * SIZE + 3):
        if travel >= leave or any(c < 0 or c >= SIZE for c in cell):
            return None
        crossing = min(next_cross)
        cell_end = min(leave, crossing)
        if cell_end > travel:
            key = tuple(cell)
            if key in cells:
                stored = normalized(cells[key])
                own = prefix and i < 8 and cell_end <= CELL * SEARCH_CELLS
                own = own and dot(stored, normal) >= 0.9 and dot(stored, ray) > 0
                if not own:
                    return key
            else:
                prefix = False
        advance = [value <= crossing for value in next_cross]
        for axis in range(3):
            if advance[axis]:
                cell[axis] += step[axis]
                next_cross[axis] += delta[axis]
        travel = crossing
    raise AssertionError("Receiver DDA exceeded its geometric step bound")


class ContactOriginTests(unittest.TestCase):
    def assert_gap_stays_outside_neighbor(self, cells, receiver, normal, center, radius):
        old = old_origin(cells, receiver, normal)
        old_distance = math.sqrt(sum((a - b) ** 2 for a, b in zip(old, center)))
        self.assertLess(old_distance, radius, "Fixture must reproduce entering the neighboring sphere")
        self.assertNotIn(voxel(old), cells, "The old result is misleading interior air")

        origin = surface_origin(cells, receiver, normal)
        self.assertNotIn(voxel(origin), cells)
        self.assertGreater(math.sqrt(sum((a - b) ** 2 for a, b in zip(origin, center))), radius)
        # Finding the gap does not remove the neighbor's real occlusion.
        hit = dda_hit(set(cells), origin, normal, 0, DOMAIN * 2, size=SIZE, extent=DOMAIN)
        self.assertIsNotNone(hit)
        self.assertLess(dot(cells[hit], normal), -0.1)
        tangent = (1, 0, 0) if normal[1] else (0, 1, 0)
        self.assertIsNone(dda_hit(set(cells), origin, tangent, 0, DOMAIN * 2,
                                 size=SIZE, extent=DOMAIN))

    def test_plane_and_sphere_one_air_cell_is_not_skipped(self):
        cells = {}
        receiver = position((16.5, 12.99, 16.5))
        radius = 0.5
        bottom = position((16.5, 14.01, 16.5))
        center = shifted(bottom, (0, 1, 0), radius)
        add_plane(cells, 1, 12.99, (0, 1, 0))
        add_sphere(cells, center, radius)
        self.assertAlmostEqual(bottom[1] - receiver[1], CELL * 1.02)
        self.assert_gap_stays_outside_neighbor(cells, receiver, (0, 1, 0), center, radius)

    def test_two_spheres_one_air_cell_is_not_skipped(self):
        cells = {}
        receiver = position((12.99, 16.5, 16.5))
        radius = 0.5
        first_center = shifted(receiver, (-1, 0, 0), radius)
        second_edge = position((14.01, 16.5, 16.5))
        second_center = shifted(second_edge, (1, 0, 0), radius)
        add_sphere(cells, first_center, radius)
        add_sphere(cells, second_center, radius)
        self.assert_gap_stays_outside_neighbor(cells, receiver, (1, 0, 0), second_center, radius)

    def test_closed_adjacent_wall_is_not_crossed_to_find_air(self):
        cells = {}
        receiver = position((16.5, 12.99, 16.5))
        add_plane(cells, 1, 12.99, (0, 1, 0))
        add_plane(cells, 1, 13.2, (0, -1, 0))
        origin = surface_origin(cells, receiver, (0, 1, 0))
        self.assertEqual(voxel(origin)[1], 12)
        self.assertIn(voxel(origin), cells)
        self.assertIsNotNone(dda_hit(set(cells), origin, (0, 1, 0), 0, DOMAIN,
                                     size=SIZE, extent=DOMAIN))

    def test_air_start_is_kept_before_nearby_wall(self):
        cells = {}
        add_plane(cells, 1, 14.01, (0, -1, 0))
        receiver = position((16.5, 13.4, 16.5))
        origin = surface_origin(cells, receiver, (0, 1, 0))
        self.assertEqual(voxel(origin)[1], 13)
        self.assertIsNotNone(dda_hit(set(cells), origin, (0, 1, 0), 0, DOMAIN,
                                     size=SIZE, extent=DOMAIN))

    def test_occupied_search_cannot_exceed_distance_budget(self):
        cells = {}
        for row in range(12, 18):
            add_plane(cells, 1, row + 0.2, (0, 1, 0))
        receiver = position((16.5, 12.5, 16.5))
        origin = surface_origin(cells, receiver, (0, 1, 0))
        self.assertLessEqual(math.dist(receiver, origin), CELL * SEARCH_CELLS)
        self.assertIn(voxel(origin), cells)

    def test_axis_parallel_and_nonunit_normals_are_finite(self):
        for normal in ((1, 0, 0), (-1, 0, 0), (0, 7, 0), (0, -4, 0), (0, 0, 2)):
            cells = {(12, 12, 12): normalized(normal)}
            receiver = position((12.5, 12.5, 12.5))
            origin = surface_origin(cells, receiver, normal)
            self.assertTrue(all(math.isfinite(value) for value in origin))
            self.assertNotIn(voxel(origin), cells)
            self.assertLessEqual(math.dist(receiver, origin), CELL * SEARCH_CELLS)

    def test_invalid_normal_does_not_create_nan_origin(self):
        receiver = position((12.5, 12.5, 12.5))
        for normal in ((0, 0, 0), (float("nan"), 0, 0), (float("inf"), 0, 0)):
            self.assertEqual(surface_origin({}, receiver, normal), receiver)


class ReceiverShellTests(unittest.TestCase):
    def strict_hit(self, cells, origin, direction, start=0, span=DOMAIN * 2):
        return dda_hit(set(cells), origin, direction, start, span, size=SIZE, extent=DOMAIN)

    def test_lower_sphere_shell_does_not_hide_lit_floor(self):
        floor, sphere = {}, {}
        add_plane(floor, 1, 12.99, (0, 1, 0))
        radius = 0.5
        center = shifted(position((16.5, 13.09, 16.5)), (0, 1, 0), radius)
        add_sphere(sphere, center, radius)
        cells = dict(floor)
        cells.update(sphere)
        for angle in (0, 15, 30):
            radians = math.radians(angle)
            normal = (math.sin(radians), -math.cos(radians), 0)
            receiver = shifted(center, normal, radius)
            origin = surface_origin(cells, receiver, normal)
            self.assertIn(voxel(origin), sphere)
            self.assertEqual(self.strict_hit(cells, origin, (0, -1, 0)), voxel(origin))
            hit = receiver_hit(cells, origin, (0, -1, 0), normal)
            self.assertIn(hit, floor, "Real floor remains an opaque, front-facing hit")
            self.assertGreater(dot(cells[hit], (0, 1, 0)), 0.9)

    def test_lower_sphere_can_see_open_direction_after_its_own_shell(self):
        cells = {}
        radius = 0.5
        center = shifted(position((16.5, 13.09, 16.5)), (0, 1, 0), radius)
        add_plane(cells, 1, 12.99, (0, 1, 0))
        add_sphere(cells, center, radius)
        normal = (0.5, -math.sqrt(0.75), 0)
        origin = surface_origin(cells, shifted(center, normal, radius), normal)
        direction = (1, 0.1, 0)
        self.assertGreater(dot(normal, direction), 0)
        self.assertIn(self.strict_hit(cells, origin, direction), cells)
        self.assertIsNone(receiver_hit(cells, origin, direction, normal))

    def test_close_spheres_preserve_opposing_surface(self):
        first, second = {}, {}
        radius = 0.5
        first_edge = position((12.99, 16.5, 16.5))
        first_center = shifted(first_edge, (-1, 0, 0), radius)
        second_center = shifted(position((13.09, 16.5, 16.5)), (1, 0, 0), radius)
        add_sphere(first, first_center, radius)
        add_sphere(second, second_center, radius)
        cells = dict(first)
        cells.update(second)
        for angle in (0, 15):
            radians = math.radians(angle)
            normal = (math.cos(radians), math.sin(radians), 0)
            origin = surface_origin(cells, shifted(first_center, normal, radius), normal)
            hit = receiver_hit(cells, origin, (1, 0, 0), normal)
            self.assertIn(hit, second)
            self.assertLess(cells[hit][0], -0.9)

    def test_adjacent_opposing_or_unrelated_wall_stops_exemption(self):
        origin = position((12.5, 12.5, 12.5))
        for wall_normal in ((-1, 0, 0), (0, 1, 0)):
            cells = {(12, 12, 12): (1, 0, 0), (13, 12, 12): wall_normal}
            self.assertEqual(receiver_hit(cells, origin, (1, 0, 0), (1, 0, 0)), (13, 12, 12))

    def test_air_permanently_ends_exemption_even_for_aligned_later_wall(self):
        cells = {(12, 12, 12): (1, 0, 0), (14, 12, 12): (1, 0, 0)}
        origin = position((12.5, 12.5, 12.5))
        self.assertEqual(receiver_hit(cells, origin, (1, 0, 0), (1, 0, 0)), (14, 12, 12))

    def test_filled_prefix_cannot_seek_air_past_distance_budget(self):
        cells = {(x, 12, 12): (1, 0, 0) for x in range(12, 18)}
        origin = position((12.5, 12.5, 12.5))
        self.assertEqual(receiver_hit(cells, origin, (1, 0, 0), (1, 0, 0)), (14, 12, 12))

    def test_strict_or_inward_or_later_interval_never_exempts_receiver(self):
        cells = {(x, 12, 12): (1, 0, 0) for x in range(10, 18)}
        origin = position((12.5, 12.5, 12.5))
        for direction, normal, start in (((1, 0, 0), (0, 0, 0), 0),
                                         ((-1, 0, 0), (1, 0, 0), 0),
                                         ((1, 0, 0), (1, 0, 0), CELL)):
            self.assertEqual(receiver_hit(cells, origin, direction, normal, start),
                             self.strict_hit(cells, origin, direction, start))

    def test_opposing_normal_in_shared_start_cell_is_never_ignored(self):
        origin = position((12.5, 12.5, 12.5))
        cells = {(12, 12, 12): (-1, 0, 0)}
        self.assertEqual(receiver_hit(cells, origin, (1, 0, 0), (1, 0, 0)), (12, 12, 12))

    def test_exact_negative_axis_boundary_preserves_the_next_wall(self):
        origin = position((13, 12.5, 12.5))
        cells = {(13, 12, 12): (1, 0, 0), (12, 12, 12): (-1, 0, 0), (11, 12, 12): (1, 0, 0)}
        self.assertEqual(self.strict_hit(cells, origin, (-1, 0, 0)), (12, 12, 12))
        self.assertEqual(receiver_hit(cells, origin, (-1, 0, 0), (-7, 0, 0)), (11, 12, 12))

    def test_disabled_receiver_matches_strict_random_grids(self):
        rng = random.Random(7319)
        for _ in range(150):
            cells = {tuple(rng.randrange(SIZE) for _ in range(3)): (1, 0, 0) for _ in range(200)}
            origin = position(tuple(rng.uniform(-1, SIZE + 1) for _ in range(3)))
            direction = tuple(rng.uniform(-1, 1) for _ in range(3))
            start = rng.uniform(0, CELL * 3)
            self.assertEqual(receiver_hit(cells, origin, direction, (0, 0, 0), start),
                             self.strict_hit(cells, origin, direction, start))


if __name__ == "__main__":
    unittest.main(verbosity=2)
