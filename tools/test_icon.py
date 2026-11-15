"""ImageWalker 3.0's cyclic pinwheel and the frozen historical icon assets."""

import hashlib
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
from generate_icon import draw
from iconlib import Canvas

ROOT = Path(__file__).resolve().parents[1]
LEGACY = {
    "res-extra/ArtMate.ico": "5b7070cac0bd1506a381331f169679512d9aeb9d32183287b2bedf176a790525",
    "res-extra/Includes.ico": "ced15a06d70196e940517967fa77ff3d82eab777ba468a338bbb68a43e7fe909",
    "res-extra/ico00001.ico": "7c78323435016bbc6a17cf760f72d8aef262243d3953c37c5c8da0c160613c49",
    "res-extra/icon1.ico": "c2dae53bff95f142d0a4d3a6ad99cffa8809b3a0413c0946864db46ea1b7a430",
    "res-extra/idr_main.ico": "c4b04430a04116d17d91f6c1606c67c5ab17365482fbb8a89fd3a8f6f0097809",
    "src10/res/imagewalker.ico": "7c78323435016bbc6a17cf760f72d8aef262243d3953c37c5c8da0c160613c49",
    "src20/res/imagewalker.ico": "7c78323435016bbc6a17cf760f72d8aef262243d3953c37c5c8da0c160613c49",
    "src20/res/default_file.ico": "8a90ba49f79b8c9a555b6097dba96aecee14811ef97c54b3f0141fca405d87cb",
    "src20/res/folder.ico": "7679f904b1697db40248c57a66b9895912024f38efcc4db848b80d1b90cc953f",
    "src22/Res/ImageWalker.ico": "7c78323435016bbc6a17cf760f72d8aef262243d3953c37c5c8da0c160613c49",
    "src22/Res/default_file.ico": "8a90ba49f79b8c9a555b6097dba96aecee14811ef97c54b3f0141fca405d87cb",
    "src22/Res/folder.ico": "7679f904b1697db40248c57a66b9895912024f38efcc4db848b80d1b90cc953f",
    "src23/Res/ImageWalker.ico": "c4b04430a04116d17d91f6c1606c67c5ab17365482fbb8a89fd3a8f6f0097809",
    "src23/Res/default_file.ico": "8a90ba49f79b8c9a555b6097dba96aecee14811ef97c54b3f0141fca405d87cb",
    "src23/Res/folder.ico": "7679f904b1697db40248c57a66b9895912024f38efcc4db848b80d1b90cc953f",
}


class IconTests(unittest.TestCase):
    def test_legacy_icons_are_unchanged(self) -> None:
        for name, expected in LEGACY.items():
            with self.subTest(asset=name):
                self.assertEqual(expected, hashlib.sha256((ROOT / name).read_bytes()).hexdigest())

    def test_all_four_overlaps_form_a_cycle_at_desktop_sizes(self) -> None:
        for size in (16, 32, 48, 256):
            canvas = Canvas(size)
            draw(canvas)
            pixels = canvas.rgba()

            def colour(x: int, y: int) -> tuple[int, ...]:
                offset = (int(y * size / 100) * size + int(x * size / 100)) * 4
                return tuple(pixels[offset:offset + 3])

            with self.subTest(size=size):
                red, green, blue = colour(34, 34)
                self.assertGreater(green, max(red, blue) * 2)
                red, green, blue = colour(61, 34)
                self.assertGreater(blue, max(red, green) * 2)
                red, green, blue = colour(61, 61)
                self.assertGreater(red, max(green, blue) * 2)
                red, green, blue = colour(34, 61)
                self.assertGreater(min(red, green), blue * 2)

    def test_centre_and_corners_remain_transparent(self) -> None:
        canvas = Canvas(100)
        draw(canvas)
        pixels = canvas.rgba()
        for x, y in ((0, 0), (99, 0), (0, 99), (99, 99), (50, 50)):
            self.assertEqual(0, pixels[(y * 100 + x) * 4 + 3])


if __name__ == "__main__":
    unittest.main()
