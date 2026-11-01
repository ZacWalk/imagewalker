import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import chmbuild


ROOT = Path(__file__).resolve().parent.parent


class StrictBuildTests(unittest.TestCase):
    def run_build(self, output: Path, strict: bool, warning: bool) -> int:
        project = chmbuild.Project(ROOT / "Help" / "imagewalker.hhp")
        if warning:
            project.warnings.append("deliberately missing topic")
        arguments = [str(project.path), "-o", str(output)]
        if strict:
            arguments.append("--strict")
        with (
            patch.object(chmbuild, "Project", return_value=project),
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            return chmbuild.main(arguments)

    def setUp(self) -> None:
        scratch = ROOT / "tmp"
        scratch.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="chmbuild-tests-", dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name) / "help.chm"

    def test_strict_warnings_preserve_existing_output(self) -> None:
        original = b"previous successful help"
        self.output.write_bytes(original)
        self.assertEqual(self.run_build(self.output, strict=True, warning=True), 1)
        self.assertEqual(self.output.read_bytes(), original)

    def test_strict_warnings_do_not_create_output(self) -> None:
        self.assertEqual(self.run_build(self.output, strict=True, warning=True), 1)
        self.assertFalse(self.output.exists())

    def test_strict_valid_project_builds_a_chm(self) -> None:
        self.assertEqual(self.run_build(self.output, strict=True, warning=False), 0)
        self.assertEqual(self.output.read_bytes()[:4], b"ITSF")

    def test_nonstrict_warnings_still_allow_a_build(self) -> None:
        self.assertEqual(self.run_build(self.output, strict=False, warning=True), 0)
        self.assertEqual(self.output.read_bytes()[:4], b"ITSF")


if __name__ == "__main__":
    unittest.main()
