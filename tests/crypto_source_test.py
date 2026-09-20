"""Verify vendored crypto files are exact copies of the checksum-pinned release."""
import hashlib
from pathlib import Path
import tarfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CryptoSourceTests(unittest.TestCase):
    def test_exact_upstream_files(self):
        path = ROOT / "third_party/sources/monocypher-4.0.3.tar.gz"
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                         "8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66")
        self.assertEqual(hashlib.sha512(path.read_bytes()).hexdigest(),
                         "40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965")
        with tarfile.open(path, "r:gz") as archive:
            for source in ("src/monocypher.c", "src/monocypher.h", "src/optional/monocypher-ed25519.c", "src/optional/monocypher-ed25519.h", "LICENCE.md"):
                member = archive.getmember("monocypher-4.0.3/" + source)
                self.assertTrue(member.isfile())
                self.assertEqual((ROOT / "third_party/monocypher" / Path(source).name).read_bytes(),
                                 archive.extractfile(member).read())


if __name__ == "__main__":
    unittest.main()
