"""Download hash-pinned build tools. Never run downloaded installers."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import platform
import subprocess
import tarfile
import tempfile


def host_entries():
    manifest = json.loads(Path(__file__).with_name("toolchains.json").read_text())
    host = platform.system() + "-" + platform.machine()
    if host not in manifest["hosts"]:
        raise SystemExit("No pinned toolchain for " + host)
    return manifest["hosts"][host]


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def install(entry, destination):
    archive = destination / entry["url"].rsplit("/", 1)[1]
    if archive.is_symlink():
        raise SystemExit("Refusing symlink archive: " + str(archive))
    if not archive.exists():
        with tempfile.TemporaryDirectory(prefix="tcs-download-", dir=destination) as temp:
            incoming = Path(temp) / archive.name
            subprocess.run(["curl", "--fail", "--location", "--proto", "=https",
                            "--proto-redir", "=https", "--tlsv1.2", "--retry", "2",
                            "--max-time", "180", entry["url"],
                            "--output", str(incoming)], check=True)
            if digest(incoming) != entry["sha256"]:
                raise SystemExit("Checksum mismatch: " + archive.name)
            incoming.rename(archive)
    if digest(archive) != entry["sha256"]:
        raise SystemExit("Checksum mismatch: " + archive.name)
    target = destination / entry["directory"]
    marker = target / ".tcs-archive-sha256"
    if target.exists() or target.is_symlink():
        if (target.is_symlink() or not target.is_dir() or marker.is_symlink() or
                not marker.is_file() or marker.read_text().strip() != entry["sha256"]):
            raise SystemExit("Unrecognized tool directory; choose a fresh --directory: " + str(target))
        print("Reusing", target)
        return
    # Only regular files/directories in the expected root may be extracted.
    with tarfile.open(archive) as bundle:
        for member in bundle.getmembers():
            name = PurePosixPath(member.name)
            if (name.is_absolute() or ".." in name.parts or not name.parts or
                name.parts[0] != entry["directory"] or
                not (member.isfile() or member.isdir())):
                raise SystemExit("Unsafe archive member: " + member.name)
    with tempfile.TemporaryDirectory(prefix="tcs-extract-", dir=destination) as temp:
        subprocess.run(["tar", "-xf", str(archive), "-C", temp], check=True)
        extracted = Path(temp) / entry["directory"]
        (extracted / marker.name).write_text(entry["sha256"] + "\n")
        extracted.rename(target)
    print("Verified and extracted", target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, default=Path(".tools"))
    parser.add_argument("--path", choices=("microkit", "zig"),
                        help="Print the default tool path without downloading")
    args = parser.parse_args()
    entries = host_entries()
    if args.path:
        entry = next(e for e in entries if e["directory"].startswith(args.path + "-"))
        path = args.directory / entry["directory"]
        print(path / "zig" if args.path == "zig" else path)
        return
    destination = args.directory.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    for entry in entries:
        install(entry, destination)


if __name__ == "__main__":
    main()
