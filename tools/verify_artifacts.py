"""Verify the saved seed and its recorded source inputs, not a release signature."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "artifacts/manifest.json"


def inputs():
    paths = [ROOT / "Makefile", ROOT / "LICENSE", ROOT / "NOTICE.md"]
    for directory in ("include", "lib", "servers", "system", "tests", "tools", "third_party"):
        paths.extend(p for p in (ROOT / directory).rglob("*")
                     if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc")
    paths.extend(p for p in (ROOT / "artifacts").rglob("*")
                 if p.is_file() and p != MANIFEST and p.name != "README.md")
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(paths)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--record", action="store_true",
                        help="Maintainer only: replace the record after rebuilding and testing")
    args = parser.parse_args()
    actual = inputs()
    if args.record:
        MANIFEST.write_text(json.dumps({"format": 1, "sha256": actual}, indent=2) + "\n")
        print("Recorded", len(actual), "source and artifact hashes")
        return
    expected = json.loads(MANIFEST.read_text())["sha256"]
    failures = [name for name in sorted(expected.keys() | actual.keys())
                if expected.get(name) != actual.get(name)]
    if failures:
        raise SystemExit("Saved artifact/source mismatch:\n" + "\n".join(failures))
    record = json.loads((ROOT / "artifacts/build.json").read_text())
    image = ROOT / "artifacts/loader.img"
    if (image.stat().st_size != record["image_bytes"] or
            actual["artifacts/loader.img"] != record["image_sha256"]):
        raise SystemExit("Image does not match build.json")
    terminal = ROOT / "artifacts/terminal.img"
    if (terminal.stat().st_size != record["terminal_image_bytes"] or
            actual["artifacts/terminal.img"] != record["terminal_image_sha256"]):
        raise SystemExit("Terminal image does not match build.json")
    print("PASS saved image, source inputs, upstream source bundles, and licenses (SHA-256)")


if __name__ == "__main__":
    main()
