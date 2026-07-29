#!/usr/bin/env python3
"""Create or verify a portable SHA-256 manifest for a compatibility archive."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


MANIFEST_NAME = "CHECKSUMS.sha256"


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def archive_files(root: Path) -> list[Path]:
    return sorted(
        (
            path
            for path in root.rglob("*")
            if path.is_file() and path.name != MANIFEST_NAME
        ),
        key=lambda path: path.relative_to(root).as_posix(),
    )


def write_manifest(root: Path) -> None:
    lines = [
        f"{digest(path)}  {path.relative_to(root).as_posix()}"
        for path in archive_files(root)
    ]
    (root / MANIFEST_NAME).write_text(
        "\n".join(lines) + ("\n" if lines else ""), encoding="utf-8", newline="\n"
    )
    print(f"wrote {MANIFEST_NAME}: {len(lines)} files")


def verify_manifest(root: Path) -> bool:
    manifest = root / MANIFEST_NAME
    if not manifest.is_file():
        print(f"missing {MANIFEST_NAME}")
        return False
    expected: dict[str, str] = {}
    for line_number, line in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        if not line:
            continue
        if len(line) < 67 or line[64:66] != "  ":
            print(f"invalid manifest line {line_number}")
            return False
        expected[line[66:]] = line[:64].lower()

    actual_paths = {
        path.relative_to(root).as_posix(): path for path in archive_files(root)
    }
    ok = True
    for relative in sorted(set(expected) | set(actual_paths)):
        if relative not in expected:
            print(f"UNTRACKED {relative}")
            ok = False
        elif relative not in actual_paths:
            print(f"MISSING   {relative}")
            ok = False
        else:
            actual = digest(actual_paths[relative])
            if actual != expected[relative]:
                print(f"MISMATCH  {relative}")
                ok = False
    print(f"verify {'OK' if ok else 'FAIL'}: {len(actual_paths)} files")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("write", "verify"))
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    if not root.is_dir():
        parser.error("root must be a directory")
    if args.command == "write":
        write_manifest(root)
        return 0
    return 0 if verify_manifest(root) else 1


if __name__ == "__main__":
    raise SystemExit(main())
