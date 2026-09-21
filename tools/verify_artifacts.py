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
    release = ROOT / "artifacts/terminal-release.img"
    release_record = record["release_terminal"]
    if (release_record["config"] != "release" or
            release.stat().st_size != release_record["image_bytes"] or
            actual["artifacts/terminal-release.img"] != release_record["image_sha256"]):
        raise SystemExit("Release-kernel terminal image does not match build.json")
    isolation = ROOT / "artifacts/isolation.img"
    isolation_record = record["isolation_test"]
    if (isolation_record["config"] != "release" or
            isolation_record["test_only"] is not True or
            isolation.stat().st_size != isolation_record["image_bytes"] or
            actual["artifacts/isolation.img"] != isolation_record["image_sha256"]):
        raise SystemExit("Isolation test image does not match build.json")
    boot = ROOT / "artifacts/boot-test.img"
    boot_record = record["boot_context_test"]
    if (boot_record["config"] != "release" or boot_record["test_only"] is not True or
            boot.stat().st_size != boot_record["image_bytes"] or
            actual["artifacts/boot-test.img"] != boot_record["image_sha256"]):
        raise SystemExit("Boot context test image does not match build.json")
    admin = ROOT / "artifacts/admin-test.img"
    admin_record = record["admin_ipc_test"]
    if (admin_record["config"] != "release" or admin_record["test_only"] is not True or
            admin.stat().st_size != admin_record["image_bytes"] or
            actual["artifacts/admin-test.img"] != admin_record["image_sha256"]):
        raise SystemExit("Administration IPC test image does not match build.json")
    for name, mode in (("interactive", 0), ("interactive-fixture", 1)):
        path = ROOT / "artifacts" / (name + ".img")
        profile = record[name.replace("-", "_")]
        if (profile["config"] != "release" or profile["launch_mode"] != mode or
                profile["production_ready"] is not False or
                path.stat().st_size != profile["image_bytes"] or
                actual["artifacts/" + path.name] != profile["image_sha256"]):
            raise SystemExit("Interactive image does not match build.json: " + name)
    lifecycle = ROOT / "artifacts/lifecycle-test.img"
    profile = record["lifecycle_runtime_test"]
    if (profile["config"] != "release" or profile["test_only"] is not True or
            profile["production_ready"] is not False or
            lifecycle.stat().st_size != profile["image_bytes"] or
            actual["artifacts/lifecycle-test.img"] != profile["image_sha256"]):
        raise SystemExit("Lifecycle test image does not match build.json")
    print("PASS nine saved images, source inputs, upstream source bundles, and licenses (SHA-256)")


if __name__ == "__main__":
    main()
