#!/usr/bin/env python3
"""Create a normalized KernelSU module ZIP from the release tree."""

from __future__ import annotations

import os
import re
import sys
import zipfile
from pathlib import Path


FILES = (
    "module.prop",
    "customize.sh",
    "service.sh",
    "share_logs.sh",
    "webui.png",
    "bin/a2h_apply",
    "config/packages.txt",
    "config/package_states",
    "config/state",
    "bin/a2h_patch",
    "bin/a2h_trigger",
    "post-fs-data.sh",
    "wrapper.sh",
    "webroot/index.html",
    "zygisk/arm64-v8a/a2h_hook.so",
)

EXECUTABLE = {
    "customize.sh",
    "service.sh",
    "share_logs.sh",
    "bin/a2h_apply",
    "bin/a2h_patch",
    "bin/a2h_trigger",
    "post-fs-data.sh",
    "wrapper.sh",
}

TEXT_FILES = {
    "module.prop",
    "customize.sh",
    "service.sh",
    "share_logs.sh",
    "bin/a2h_apply",
    "config/packages.txt",
    "config/package_states",
    "config/state",
    "post-fs-data.sh",
    "wrapper.sh",
    "webroot/index.html",
}


def read_version(root: Path) -> str:
    module_prop = (root / "module.prop").read_text(encoding="utf-8")
    for line in module_prop.splitlines():
        if line.startswith("version="):
            version = line.split("=", 1)[1].strip()
            if re.fullmatch(r"v[0-9A-Za-z][0-9A-Za-z._-]*", version):
                return version
            raise ValueError(f"Invalid module version: {version!r}")
    raise ValueError("module.prop does not contain version=")


def verify_archive(path: Path) -> None:
    with zipfile.ZipFile(path, "r") as archive:
        names = archive.namelist()
        if names != list(FILES):
            raise ValueError("ZIP file list/order does not match the release manifest")
        bad_member = archive.testzip()
        if bad_member is not None:
            raise zipfile.BadZipFile(f"CRC check failed: {bad_member}")
        for info in archive.infolist():
            if "\\" in info.filename or info.filename.startswith("/"):
                raise ValueError(f"Non-portable ZIP path: {info.filename}")
            expected = 0o755 if info.filename in EXECUTABLE else 0o644
            actual = (info.external_attr >> 16) & 0o777
            file_type = (info.external_attr >> 16) & 0o170000
            if info.create_system != 3 or file_type != 0o100000 or actual != expected:
                raise ValueError(
                    f"Invalid ZIP mode for {info.filename}: {actual:o}, expected {expected:o}"
                )


def package(root: Path) -> Path:
    root = root.resolve(strict=True)
    version = read_version(root)
    output = root / f"a2h_hook_{version}.zip"
    temporary = output.with_suffix(".zip.tmp")
    temporary.unlink(missing_ok=True)

    try:
        with zipfile.ZipFile(
            temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9
        ) as archive:
            for relative in FILES:
                source = root / relative
                if not source.is_file():
                    raise FileNotFoundError(f"Required module file missing: {relative}")
                data = source.read_bytes()
                if relative in TEXT_FILES:
                    if data.startswith(b"\xef\xbb\xbf"):
                        raise ValueError(f"UTF-8 BOM is forbidden: {relative}")
                    if b"\r" in data:
                        raise ValueError(f"CRLF/CR is forbidden: {relative}")

                info = zipfile.ZipInfo.from_file(source, relative)
                info.create_system = 3
                info.compress_type = zipfile.ZIP_DEFLATED
                mode = 0o100755 if relative in EXECUTABLE else 0o100644
                info.external_attr = mode << 16
                archive.writestr(info, data, compresslevel=9)
                print(f"  + {relative} mode={mode & 0o777:o}")

        verify_archive(temporary)
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)

    print(f"ZIP created: {output}")
    return output


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
    try:
        package(root)
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        print(f"Packaging failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
