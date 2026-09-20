"""Offline bootstrap tests; no real tools are downloaded or executed."""
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "fetch_tools", Path(__file__).resolve().parents[1] / "tools/fetch_tools.py")
fetch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fetch)


class BootstrapTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / "tool.tar.gz"
        self.entry = {"url": "https://example.invalid/tool.tar.gz", "directory": "tool"}

    def bundle(self, name="tool/file", link=None):
        with tarfile.open(self.archive, "w:gz") as bundle:
            member = tarfile.TarInfo(name)
            if link:
                member.type = tarfile.SYMTYPE
                member.linkname = link
                bundle.addfile(member)
            else:
                member.size = 4
                bundle.addfile(member, io.BytesIO(b"test"))
        self.entry["sha256"] = fetch.digest(self.archive)

    def test_install_and_reuse(self):
        self.bundle()
        fetch.install(self.entry, self.root)
        self.assertEqual((self.root / "tool/file").read_bytes(), b"test")
        with patch.object(fetch.subprocess, "run") as run:
            fetch.install(self.entry, self.root)
            run.assert_not_called()

    def test_bad_checksum_is_rejected(self):
        self.bundle()
        self.entry["sha256"] = "0" * 64
        with self.assertRaisesRegex(SystemExit, "Checksum mismatch"):
            fetch.install(self.entry, self.root)
        self.assertFalse((self.root / "tool").exists())

    def test_unrecognized_existing_directory_is_preserved(self):
        self.bundle()
        (self.root / "tool").mkdir()
        (self.root / "tool/user-file").write_text("keep")
        with self.assertRaisesRegex(SystemExit, "Unrecognized"):
            fetch.install(self.entry, self.root)
        self.assertEqual((self.root / "tool/user-file").read_text(), "keep")

    def test_unsafe_members_are_rejected(self):
        for name in ("../escape", "/absolute", "wrong-root/file", "tool/../../escape"):
            with self.subTest(name=name):
                self.bundle(name)
                with self.assertRaisesRegex(SystemExit, "Unsafe archive"):
                    fetch.install(self.entry, self.root)

    def test_symlink_member_is_rejected(self):
        self.bundle(link="../../escape")
        with self.assertRaisesRegex(SystemExit, "Unsafe archive"):
            fetch.install(self.entry, self.root)

    def test_symlink_archive_is_rejected(self):
        self.archive.symlink_to(self.root / "missing")
        with self.assertRaisesRegex(SystemExit, "Refusing symlink"):
            fetch.install(self.entry, self.root)

    def test_failed_extraction_can_be_retried(self):
        self.bundle()
        with patch.object(fetch.subprocess, "run", side_effect=RuntimeError("interrupted")):
            with self.assertRaises(RuntimeError):
                fetch.install(self.entry, self.root)
        self.assertFalse((self.root / "tool").exists())
        fetch.install(self.entry, self.root)


if __name__ == "__main__":
    unittest.main()
