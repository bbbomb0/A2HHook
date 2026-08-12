#!/usr/bin/env python3
"""Create a normalized KernelSU module ZIP from the release tree."""

from __future__ import annotations

import os
import re
import sys
import zipfile
import zlib
from pathlib import Path


FILES = (
    "module.prop",
    "LICENSE",
    "customize.sh",
    "service.sh",
    "webui.png",
    "companion/a2h_companion.apk",
    "bin/a2h_apply",
    "config/packages.txt",
    "config/package_states",
    "config/state",
    "config/game_auto_pause",
    "bin/a2h_patch",
    "bin/a2h_trigger",
    "bin/a2h_audio_watch",
    "post-fs-data.sh",
    "wrapper.sh",
    "uninstall.sh",
    "webroot/index.html",
    "webroot/coolapk.webp",
    "webroot/donate-wechat-pay.webp",
    "webroot/donate-wechat.webp",
    "webroot/donate-alipay.webp",
    "webroot/payment-wechat-pay.webp",
    "webroot/payment-wechat-reward.webp",
    "webroot/payment-alipay.webp",
)

EXECUTABLE = {
    "customize.sh",
    "service.sh",
    "bin/a2h_apply",
    "bin/a2h_patch",
    "bin/a2h_trigger",
    "bin/a2h_audio_watch",
    "post-fs-data.sh",
    "wrapper.sh",
    "uninstall.sh",
}

TEXT_FILES = {
    "module.prop",
    "LICENSE",
    "customize.sh",
    "service.sh",
    "bin/a2h_apply",
    "bin/a2h_audio_watch",
    "config/packages.txt",
    "config/package_states",
    "config/state",
    "config/game_auto_pause",
    "post-fs-data.sh",
    "wrapper.sh",
    "uninstall.sh",
    "webroot/index.html",
}

ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def select_compression(data: bytes) -> int:
    compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
    compressed = compressor.compress(data) + compressor.flush()
    return zipfile.ZIP_DEFLATED if len(compressed) < len(data) else zipfile.ZIP_STORED


def read_version(root: Path) -> str:
    module_prop = (root / "module.prop").read_text(encoding="utf-8")
    versions = [
        line.split("=", 1)[1].strip()
        for line in module_prop.splitlines()
        if line.startswith("version=")
    ]
    version_codes = [
        line.split("=", 1)[1].strip()
        for line in module_prop.splitlines()
        if line.startswith("versionCode=")
    ]
    if len(versions) != 1:
        raise ValueError("module.prop must contain exactly one version=")
    if len(version_codes) != 1:
        raise ValueError("module.prop must contain exactly one versionCode=")

    version = versions[0]
    version_code = version_codes[0]
    if not re.fullmatch(r"v[0-9A-Za-z][0-9A-Za-z._-]*", version):
        raise ValueError(f"Invalid module version: {version!r}")
    if not re.fullmatch(r"[1-9][0-9]*", version_code):
        raise ValueError(f"Invalid module versionCode: {version_code!r}")
    return version


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
            if info.compress_size > info.file_size:
                raise ValueError(f"Inefficient ZIP compression for {info.filename}")


def package(root: Path) -> Path:
    root = root.resolve(strict=True)
    if len(FILES) != len(set(FILES)):
        raise ValueError("Release manifest contains duplicate paths")
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

                info = zipfile.ZipInfo(filename=relative, date_time=ZIP_TIMESTAMP)
                info.create_system = 3
                compression = select_compression(data)
                info.compress_type = compression
                mode = 0o100755 if relative in EXECUTABLE else 0o100644
                info.external_attr = mode << 16
                archive.writestr(
                    info,
                    data,
                    compress_type=compression,
                    compresslevel=9 if compression == zipfile.ZIP_DEFLATED else None,
                )
                method = "deflate" if compression == zipfile.ZIP_DEFLATED else "store"
                print(f"  + {relative} mode={mode & 0o777:o} method={method}")

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
