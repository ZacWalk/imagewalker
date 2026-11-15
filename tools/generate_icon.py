"""The ImageWalker 3.0 four-colour mark; never writes a historical release's resources."""

import sys
from pathlib import Path

sys.dont_write_bytecode = True
from iconlib import Canvas, generate

ROOT = Path(__file__).resolve().parents[1]


def draw(canvas: Canvas) -> None:
    """Four overlapping squares with cyclic pinwheel occlusion, not a four-tile stack."""
    def tile(x: float, y: float, colour: tuple[int, int, int],
             clips: tuple[tuple[float, float, float, float], ...] = ((0, 0, 100, 100),)) -> None:
        def gradient(px: float, py: float) -> tuple[int, int, int]:
            amount = max(0.25, 1.0 - 0.65 * ((px - x) + (py - y)) / 88)
            return tuple(round(channel * amount) for channel in colour)
        for left, top, right, bottom in clips:
            l, t, r, b = max(x, left), max(y, top), min(x + 44, right), min(y + 44, bottom)
            canvas.rect(l, t, r - l, b - t, gradient)

    tile(28, 2, (32, 230, 82))
    tile(54, 28, (38, 89, 234))
    tile(28, 54, (239, 51, 53))
    # The green tile covers yellow at the final overlap, closing the four-way cycle.
    tile(2, 28, (255, 222, 42), ((0, 0, 28, 100), (28, 46, 100, 100)))


if __name__ == "__main__":
    raise SystemExit(generate(draw, ROOT / "src30/Res/ImageWalker.ico"))
