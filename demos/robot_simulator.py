"""Minimal 2D robot simulator for the Ku robotics demo.

The robot lives on a 2D plane. It has:
- position (x, y)
- velocity (vx, vy)
- a target to reach
- memory of where it has been

This is intentionally tiny — the point is to show the Ku C ABI FFI bridge,
not to build a real simulator.
"""


class Robot2D:
    """A trivial 2D point robot with velocity control."""

    def __init__(self, x: float = 0.0, y: float = 0.0):
        self.x = x
        self.y = y
        self.vx = 0.0
        self.vy = 0.0
        self.target_x = 10.0
        self.target_y = 10.0
        self.step_count = 0
        self.history: list[tuple[float, float]] = [(x, y)]

    def sensor_position(self) -> tuple[float, float]:
        """Return current (x, y) position."""
        return (self.x, self.y)

    def sensor_distance_to_target(self) -> float:
        """Return Euclidean distance to target."""
        dx = self.target_x - self.x
        dy = self.target_y - self.y
        return (dx * dx + dy * dy) ** 0.5

    def sensor_target_bearing(self) -> float:
        """Return angle to target in radians."""
        import math
        dx = self.target_x - self.x
        dy = self.target_y - self.y
        return math.atan2(dy, dx)

    def actuate(self, ax: float, ay: float, dt: float = 0.1):
        """Apply acceleration for dt seconds. Clamps velocity."""
        max_speed = 2.0
        self.vx += ax * dt
        self.vy += ay * dt
        # Clamp speed
        speed = (self.vx ** 2 + self.vy ** 2) ** 0.5
        if speed > max_speed:
            scale = max_speed / speed
            self.vx *= scale
            self.vy *= scale
        self.x += self.vx * dt
        self.y += self.vy * dt
        self.step_count += 1
        self.history.append((self.x, self.y))

    def memory_distance_traveled(self) -> float:
        """Return total distance traveled (memory of past positions)."""
        total = 0.0
        for i in range(1, len(self.history)):
            dx = self.history[i][0] - self.history[i - 1][0]
            dy = self.history[i][1] - self.history[i - 1][1]
            total += (dx * dx + dy * dy) ** 0.5
        return total

    def is_at_target(self, tolerance: float = 0.5) -> bool:
        return self.sensor_distance_to_target() <= tolerance


def make_simulator():
    """Create a fresh simulator instance."""
    return Robot2D(x=0.0, y=0.0)
