#!/usr/bin/env python3
"""Independently validate a normalized A2HHook KernelSU module ZIP."""

from __future__ import annotations

import argparse
import re
import struct
import sys
import zipfile
from pathlib import Path, PurePosixPath


FILES = (
    "module.prop",
    "customize.sh",
    "service.sh",
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
    "webroot/coolapk.png",
)

EXECUTABLE = {
    "customize.sh", "service.sh", "bin/a2h_apply", "bin/a2h_patch",
    "bin/a2h_trigger", "post-fs-data.sh", "wrapper.sh",
}

TEXT = {
    "module.prop", "customize.sh", "service.sh", "bin/a2h_apply",
    "config/packages.txt", "config/package_states", "config/state",
    "post-fs-data.sh", "wrapper.sh", "webroot/index.html",
}

OFFICIAL = (
    "com.kugou.android", "com.tencent.qqmusic", "com.netease.cloudmusic",
    "cn.kuwo.player", "com.miui.player", "com.luna.music",
)

PT_INTERP = 3
PT_DYNAMIC = 2
DT_NEEDED = 1


def properties(data: bytes) -> dict[str, str]:
    text = data.decode("utf-8")
    values: dict[str, str] = {}
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"invalid module.prop line: {line!r}")
        key, value = line.split("=", 1)
        if key in values:
            raise ValueError(f"duplicate module.prop key: {key}")
        values[key] = value
    return values


def elf_program_headers(data: bytes) -> list[tuple[int, int, int]]:
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("expected little-endian ELF64")
    machine = struct.unpack_from("<H", data, 18)[0]
    if machine != 183:
        raise ValueError(f"expected AArch64 e_machine=183, got {machine}")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    if phentsize < 56 or phoff + phentsize * phnum > len(data):
        raise ValueError("invalid ELF program header table")
    values: list[tuple[int, int, int]] = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        p_type = struct.unpack_from("<I", data, offset)[0]
        p_offset, p_filesz = struct.unpack_from("<QQ", data, offset + 8)[0], struct.unpack_from("<Q", data, offset + 32)[0]
        values.append((p_type, p_offset, p_filesz))
    return values


def validate(path: Path) -> None:
    with zipfile.ZipFile(path, "r") as archive:
        infos = archive.infolist()
        names = [item.filename for item in infos]
        if len(names) != len(set(names)):
            raise ValueError("duplicate ZIP members")
        if names != list(FILES):
            raise ValueError(f"member list/order mismatch: {names!r}")
        if archive.testzip() is not None:
            raise ValueError("CRC verification failed")

        for info in infos:
            name = info.filename
            posix = PurePosixPath(name)
            if info.is_dir() or name.startswith("/") or "\\" in name or ".." in posix.parts:
                raise ValueError(f"non-portable member path: {name}")
            mode = (info.external_attr >> 16) & 0o777
            file_type = (info.external_attr >> 16) & 0o170000
            expected = 0o755 if name in EXECUTABLE else 0o644
            if info.create_system != 3 or file_type != 0o100000 or mode != expected:
                raise ValueError(f"invalid Unix mode for {name}: system={info.create_system} type={file_type:o} mode={mode:o}")
            data = archive.read(name)
            if name in TEXT:
                data.decode("utf-8")
                if data.startswith(b"\xef\xbb\xbf") or b"\r" in data:
                    raise ValueError(f"BOM or CR line ending in {name}")

        prop = properties(archive.read("module.prop"))
        required = {"id", "name", "version", "versionCode", "author", "description", "webui", "webuiIcon"}
        if not required <= prop.keys() or prop.get("id") != "a2h_hook":
            raise ValueError("invalid module metadata")
        version = prop.get("version", "")
        if re.fullmatch(r"v[0-9A-Za-z][0-9A-Za-z._-]*", version) is None:
            raise ValueError(f"invalid module version: {version!r}")
        if path.name != f"a2h_hook_{version}.zip":
            raise ValueError(f"ZIP filename does not match module version {version}")

        packages = archive.read("config/packages.txt").decode("utf-8").splitlines()
        states = archive.read("config/package_states").decode("utf-8").splitlines()
        if len(packages) != 10 or tuple(packages[:6]) != OFFICIAL:
            raise ValueError("default package table is not 10 slots with six official packages")
        if states != ["1"] * 6 + ["0"] * 4:
            raise ValueError("default package state table is invalid")
        if archive.read("config/state") != b"disabled\n":
            raise ValueError("default mode is not whitelist/disabled")

        patcher = archive.read("bin/a2h_patch")
        trigger = archive.read("bin/a2h_trigger")
        patcher_ph = elf_program_headers(patcher)
        elf_program_headers(trigger)
        if any(item[0] in (PT_INTERP, PT_DYNAMIC) for item in patcher_ph):
            raise ValueError("a2h_patch is not a static ELF")
        embedded = version.removeprefix("v").encode("ascii")
        if embedded not in patcher:
            raise ValueError(f"a2h_patch does not embed version {embedded.decode()}")

        forbidden = ("zygisk/", "a2h_hook.so", "a2h_inject", "tests/", "action.sh")
        if any(any(token in name for token in forbidden) for name in names):
            raise ValueError("legacy/test/action content leaked into module ZIP")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("zip", type=Path)
    args = parser.parse_args()
    try:
        validate(args.zip.resolve(strict=True))
    except (OSError, UnicodeError, ValueError, zipfile.BadZipFile, struct.error) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print(f"PASS: normalized A2HHook module ZIP: {args.zip}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
