#!/usr/bin/env python3
"""Independently validate a normalized A2HHook KernelSU module ZIP."""

from __future__ import annotations

import argparse
import io
import re
import struct
import sys
import zipfile
from pathlib import Path, PurePosixPath


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
    "customize.sh", "service.sh", "bin/a2h_apply", "bin/a2h_patch",
    "bin/a2h_trigger", "bin/a2h_audio_watch", "post-fs-data.sh", "wrapper.sh", "uninstall.sh",
}

TEXT = {
    "module.prop", "LICENSE", "customize.sh", "service.sh", "bin/a2h_apply",
    "config/packages.txt", "config/package_states", "config/state",
    "config/game_auto_pause", "bin/a2h_audio_watch",
    "post-fs-data.sh", "wrapper.sh", "uninstall.sh", "webroot/index.html",
}

OFFICIAL = (
    "com.kugou.android", "com.tencent.qqmusic", "com.netease.cloudmusic",
    "cn.kuwo.player", "com.miui.player", "com.luna.music",
)

PT_INTERP = 3
PT_DYNAMIC = 2
DT_NEEDED = 1
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


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


def apk_signing_ids(data: bytes) -> set[int]:
    eocd = data.rfind(b"PK\x05\x06", max(0, len(data) - 65557))
    if eocd < 0 or eocd + 22 > len(data):
        raise ValueError("companion APK EOCD is missing")
    central_offset = struct.unpack_from("<I", data, eocd + 16)[0]
    if central_offset < 32 or data[central_offset - 16:central_offset] != b"APK Sig Block 42":
        raise ValueError("companion APK signing block is missing")
    block_size = struct.unpack_from("<Q", data, central_offset - 24)[0]
    block_start = central_offset - block_size - 8
    if block_start < 0 or struct.unpack_from("<Q", data, block_start)[0] != block_size:
        raise ValueError("companion APK signing block size mismatch")
    ids: set[int] = set()
    position = block_start + 8
    pairs_end = central_offset - 24
    while position < pairs_end:
        if position + 12 > pairs_end:
            raise ValueError("truncated companion APK signing pair")
        pair_size = struct.unpack_from("<Q", data, position)[0]
        if pair_size < 4 or pair_size > pairs_end - position - 8:
            raise ValueError("invalid companion APK signing pair size")
        ids.add(struct.unpack_from("<I", data, position + 8)[0])
        position += 8 + pair_size
    if position != pairs_end:
        raise ValueError("companion APK signing pairs do not fill the block")
    return ids


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


def validate(path: Path, expected_version: str | None = None, expected_code: str | None = None) -> None:
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
            if info.date_time != ZIP_TIMESTAMP:
                raise ValueError(f"non-reproducible ZIP timestamp for {name}: {info.date_time!r}")
            if info.compress_size > info.file_size:
                raise ValueError(f"inefficient ZIP compression for {name}")
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
        version_code = prop.get("versionCode", "")
        if re.fullmatch(r"v[0-9A-Za-z][0-9A-Za-z._-]*", version) is None:
            raise ValueError(f"invalid module version: {version!r}")
        if re.fullmatch(r"[1-9][0-9]*", version_code) is None:
            raise ValueError(f"invalid module versionCode: {version_code!r}")
        if expected_version is not None and version != expected_version:
            raise ValueError(f"module version {version!r} does not match expected {expected_version!r}")
        if expected_code is not None and version_code != expected_code:
            raise ValueError(f"module versionCode {version_code!r} does not match expected {expected_code!r}")
        if path.name != f"a2h_hook_{version}.zip":
            raise ValueError(f"ZIP filename does not match module version {version}")

        license_text = archive.read("LICENSE").decode("utf-8")
        if "GNU GENERAL PUBLIC LICENSE" not in license_text or "Version 3, 29 June 2007" not in license_text:
            raise ValueError("module ZIP does not contain the GPLv3 license text")

        packages = archive.read("config/packages.txt").decode("utf-8").splitlines()
        states = archive.read("config/package_states").decode("utf-8").splitlines()
        if len(packages) != 10 or tuple(packages[:6]) != OFFICIAL:
            raise ValueError("default package table is not 10 slots with six official packages")
        if states != ["1"] * 6 + ["0"] * 4:
            raise ValueError("default package state table is invalid")
        if archive.read("config/state") != b"disabled\n":
            raise ValueError("default mode is not whitelist/disabled")
        if archive.read("config/game_auto_pause") != b"enabled\n":
            raise ValueError("default game auto-pause policy is not Xiaomi stock/enabled")

        audio_watcher = archive.read("bin/a2h_audio_watch").decode("utf-8")
        watcher_markers = (
            "audio_track_message", "scenario", "/data/system/packages.list",
            'exec 3< "$CFG_STATES"', 'exec 4< "$CFG_PKGS"',
            '"$SU_BIN" "$lease_uid" -c "$lease_command"',
            '"$TRIGGER_RUNTIME --lease $lease_token $lease_session"',
            'chmod 0711 "$RUNTIME_DIR"', 'chmod 0555 "$trigger_tmp"',
            '"$trigger_uid" -lt 10000', "mkfifo", "logcat_pid", "watch_restart_delay",
            "APM_AudioPolicyManager:D", "AudioPolicyManager:D",
            "*'stopOutput()'*|*'stoptOutput()'*",
            'policy_port_file="$PORT_DIR/$policy_port"',
            'lease_worker_start=$(process_starttime "$lease_worker")',
            'FALLBACK_LEASE_SECONDS:-70',
        )
        if not all(marker in audio_watcher for marker in watcher_markers):
            raise ValueError("audio UID watcher lifecycle contract is incomplete")
        if any(package in audio_watcher for package in (
            "com.tencent.tmgp.sgame", "com.tencent.game.rhythmmaster",
        )):
            raise ValueError("audio UID watcher hardcodes a game package")

        companion = archive.read("companion/a2h_companion.apk")
        signing_ids = apk_signing_ids(companion)
        if 0xF05368C0 not in signing_ids:
            raise ValueError("companion APK is not signed with APK Signature Scheme v3")
        with zipfile.ZipFile(io.BytesIO(companion), "r") as apk:
            apk_names = apk.namelist()
            required_apk = {
                "AndroidManifest.xml", "classes.dex", "resources.arsc",
                "assets/index.html", "assets/coolapk.webp",
                "assets/donate-wechat-pay.webp", "assets/donate-wechat.webp",
                "assets/donate-alipay.webp", "assets/payment-wechat-pay.webp",
                "assets/payment-wechat-reward.webp", "assets/payment-alipay.webp",
            }
            if len(apk_names) != len(set(apk_names)) or not required_apk <= set(apk_names):
                raise ValueError("companion APK members are incomplete or duplicated")
            if apk.testzip() is not None:
                raise ValueError("companion APK CRC verification failed")
            if apk.read("assets/index.html") != archive.read("webroot/index.html"):
                raise ValueError("companion APK WebUI asset differs from module WebUI")
            if apk.read("assets/coolapk.webp") != archive.read("webroot/coolapk.webp"):
                raise ValueError("companion APK Coolapk asset differs from module WebUI")
            for asset in (
                "donate-wechat-pay.webp", "donate-wechat.webp", "donate-alipay.webp",
                "payment-wechat-pay.webp", "payment-wechat-reward.webp", "payment-alipay.webp",
            ):
                if apk.read(f"assets/{asset}") != archive.read(f"webroot/{asset}"):
                    raise ValueError(f"companion APK {asset} differs from module WebUI")

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
    parser.add_argument("--expected-version")
    parser.add_argument("--expected-code")
    args = parser.parse_args()
    try:
        validate(
            args.zip.resolve(strict=True),
            expected_version=args.expected_version,
            expected_code=args.expected_code,
        )
    except (OSError, UnicodeError, ValueError, zipfile.BadZipFile, struct.error) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print(f"PASS: normalized A2HHook module ZIP: {args.zip}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
