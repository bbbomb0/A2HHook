#!/usr/bin/env python3
"""Run non-destructive source, fixture, and release-tree regression checks."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass, asdict
from pathlib import Path


OFFICIAL = (
    "com.kugou.android",
    "com.tencent.qqmusic",
    "com.netease.cloudmusic",
    "cn.kuwo.player",
    "com.miui.player",
    "com.luna.music",
)

EXPECTED_RELEASE = ("v1.5.8", "1580")

HAL_CASES = {
    "OS2.0.208.0.VONCNXM": {
        "relative": "devices/25060RK16C__dali/OS2.0.208.0.VONCNXM/originals/hal/audio.primary.mediatek(os2.0.208).so",
        "sha256": "ebd8b02f5508efc1ce253c5a809300489d93f367b8ef266e6a630d1a42e69733",
        "size": 4437728,
        "build_id": "349d72e91436b550d5a1e3da2a1cc02b",
        "symbol": 0x3E3B90,
        "symbol_size": 160,
        "update": 0x3974A0,
        "stream": 0x361870,
        "policy": 0x396C60,
        "output_pool": 0x3A45C0,
        "device_layout": 0x27A620,
        "stream_open": 0x366B50,
        "app_call": 0x41ED60,
        "delete_call": 0x415BB0,
        "strlen_call": 0x4166C0,
    },
    "OS2.0.218.0.VONCNXM": {
        "relative": "devices/25060RK16C__dali/OS2.0.218.0.VONCNXM/originals/hal/audio.primary.mediatek(os2.0.218).so",
        "sha256": "ba543c1fd331d20ca149ae38c80600acc331d7a3d09bad6582ca485163cf9d13",
        "size": 4454128,
        "build_id": "b83078b3502a148fb846652aa3bf1f7d",
        "symbol": 0x3E4280,
        "symbol_size": 160,
        "update": 0x397B20,
        "stream": 0x361F00,
        "policy": 0x3972E0,
        "output_pool": 0x3A4C70,
        "device_layout": 0x27A920,
        "stream_open": 0x3671D0,
        "app_call": 0x41F460,
        "delete_call": 0x4162A0,
        "strlen_call": 0x416DB0,
    },
    "OS3.0.302.0.WONCNXM": {
        "relative": "devices/25060RK16C__dali/OS3.0.302.0.WONCNXM/originals/hal/audio.primary.mediatek.so",
        "sha256": "fe48c8d2070a318ef4ab1f6d05a37c213e0b2abc5a95d910885ab1decc468924",
        "size": 4454112,
        "build_id": "fb4737aed1aed36ca755604fad655461",
        "symbol": 0x3E3FC0,
        "symbol_size": 160,
        "update": 0x397800,
        "stream": 0x361B20,
        "policy": 0x396FC0,
        "output_pool": 0x3A4950,
        "device_layout": 0x27A870,
        "stream_open": 0x366DF0,
        "app_call": 0x41F1B0,
        "delete_call": 0x415FE0,
        "strlen_call": 0x416AF0,
    },
    "OS3.0.305.0.WONCNXM": {
        "relative": "devices/25060RK16C__dali/OS3.0.305.0.WONCNXM/originals/hal/audio.primary.mediatek.so",
        "sha256": "da028039d1a823189ed0c3df3ab80bfd5d52a9849038f32ef5ac5e229b2af6fa",
        "size": 4454072,
        "build_id": "ad3569054ba54d7178354e85fbad3ce6",
        "symbol": 0x3E4020,
        "symbol_size": 160,
        "update": 0x397860,
        "stream": 0x361B80,
        "policy": 0x397020,
        "output_pool": 0x3A49B0,
        "device_layout": 0x27A8D0,
        "stream_open": 0x366E50,
        "app_call": 0x41F210,
        "delete_call": 0x416040,
        "strlen_call": 0x416B50,
    },
}

UPDATE_FLAGS_STOCK = bytes.fromhex(
    "c8011837680e41f9006977f8080040f9088940f900013fd6"
    "08304039e8002037"
)
UPDATE_FLAGS_GAME_HANDOFF_LEGACY = bytes.fromhex(
    "080c40b91f091e7281010054680190371f2003d51f2003d5"
    "1f2003d51f2003d5"
)
UPDATE_FLAGS_GAME_ACTIVE_FAILED = bytes.fromhex(
    "092441f90a2841f93f010aeba0000054080c40b91f091e72"
    "01010054e8009037"
)
IDLE_CLEAR_DISK_CONTEXT = bytes.fromhex(
    "f3031f2a3a00805288034039"
)
IDLE_CLEAR_HELPER_FIXED = bytes.fromhex(
    "04000014f3031f2a1f641439"
)
OUTPUT_FLAGS_MEMBER_SEQUENCE = bytes.fromhex(
    "84b640b9bf0200728800360a8900162a2511881a85b600b9"
)
UPDATE_APP_STOCK_TEMPLATE = bytes.fromhex(
    "689e42f91f0500f181010054689a42f9094140390a1140f9"
    "084500913f01007200018a9a0000000017000052c8024039"
    "a800083714000014"
)
UPDATE_APP_DISK_TEMPLATE = bytes.fromhex(
    "689e42f91f0500f181010054689a42f9094140390a1140f9"
    "084500913f01007200018a9a0000000017000052c8024039"
    "a80008371400001437008052c802403928020836a3e8ffd0"
)
STREAM_EVENT_OFFSETS = (0x1F94, 0x2170, 0x22B4)
STREAM_EVENT_SIZES = (28, 28, 4)
STREAM_EVENT_STOCK_TEMPLATE = (
    bytes.fromhex(
        "08d40f3640070091f5cb40f97b0a40f9"
        "000000001f4000b1a2500054"
    ),
    bytes.fromhex(
        "580600b018c746f90803403988010836"
        "e6cb40f9c3ecfff0639c2591"
    ),
    bytes.fromhex("d8fdff17"),
)
STREAM_EVENT_INLINE_TEMPLATE = bytes.fromhex(
    "1f671439089f42f91f0500f169000054"
    "28008052086b143900000014"
)
STREAM_EVENT_RECOMPUTE_LEGACY = bytes.fromhex("1f67143900000014")
STREAM_EVENT_HANDOFF_LEGACY = bytes.fromhex("1901001440070091")
STREAM_REF_PATCH_OFF = 0x2030
STREAM_REF_PATCH_BYTES = 148
STREAM_REF_DELETE_BL_OFF = 0x10
STREAM_UPDATE_CALL_TEMPLATE = bytes.fromhex(
    "600a40f90000000085fdff17"
)
STREAM_APP_MAP_MANAGER_ADD = bytes.fromhex("00831491")
OUTPUT_POOL_MANAGER_ARG_MOV = bytes.fromhex("f60300aa")
STREAM_REF_STOCK_TEMPLATE = bytes.fromhex(
    "e8034939152840b968000036e02b41f920c9029415040034"
    "480600b008c546f90801403908ce0f36f5cb40f9760a40f9"
    "e003099141070091f70309917d1efc97f73700f9420600b0"
    "428046f9c0821491e1030991e3a30191e4ff0891d5eb0294"
    "072840b9c3ecfff0639c259141edff9021c0339122ecfff0"
    "42ac0b916000805204cf8052e50303aae60315aa25c90294"
    "79000014"
)
STREAM_REF_VARIABLE_WORD_MASKS = {
    0x10: 0xFC000000,
    0x18: 0x9F00001F,
    0x3C: 0xFC000000,
    0x44: 0x9F00001F,
    0x5C: 0xFC000000,
    0x68: 0xFFC003FF,
    0x70: 0xFFC003FF,
    0x74: 0x9F00001F,
    0x78: 0xFFC003FF,
    0x8C: 0xFC000000,
}
GAME_POLICY_STOCK = bytes.fromhex(
    "7f060071e90740f9e8179f1a7f020071ea179f1a"
    "1f011a6a291540f94a791f53ab835ff840059f1a"
)
GAME_POLICY_CONCURRENT_TEMPLATE = bytes.fromhex(
    "000000147f020071e8079f1aea179f1a"
    "e90740f91f011a6a291540f94a791f53"
    "ab835ff840059f1a"
)
GAME_POLICY_RELAXED = bytes.fromhex(
    "9f6a14397f020071e8079f1aea179f1a"
    "e90740f91f011a6a291540f94a791f53"
    "ab835ff840059f1a"
)
GAME_POLICY_PUBLIC_RELAXED = bytes.fromhex(
    "7f060071e90740f9e8179f1a7f020071"
    "ea179f1a7f020071291540f94a791f53"
    "ab835ff840019a1a"
)
UPDATE_CONCURRENT_HELPER_TEMPLATE = bytes.fromhex(
    "08010012886a1439210000141f2003d5"
    "886a5439c8030034953a41f9963e41f9"
    "f60100b4170180d2180080d2a96a77f8"
    "090100b42a2541f92b2941f97f010aeb"
    "89000054180700111f0b0071a2010054"
    "d60600f1f7420091a1feff54899e42f9"
    "3f0500f1880000540000001408000014"
    "0000001428008052886a143904000014"
    "48008052886a1439fa031f2a00000014"
    "1f2003d51f2003d51f2003d51f641439"
    "1f68143900000014f40300aa00000014"
)
UPDATE_CONCURRENT_HELPER_BYTES = 208
UPDATE_CONCURRENT_PREVIOUS_BYTES = 176
UPDATE_CONCURRENT_PREVIOUS2_BYTES = 160
UPDATE_CONCURRENT_LEGACY_BYTES = 64
CONCURRENT_PREVIOUS_SHIFT = UPDATE_CONCURRENT_HELPER_BYTES - UPDATE_CONCURRENT_PREVIOUS_BYTES
CONCURRENT_PREVIOUS2_SHIFT = UPDATE_CONCURRENT_HELPER_BYTES - UPDATE_CONCURRENT_PREVIOUS2_BYTES
CONCURRENT_LEGACY_SHIFT = UPDATE_CONCURRENT_HELPER_BYTES - UPDATE_CONCURRENT_LEGACY_BYTES
CONCURRENT_OPEN_HELPER_OFF = 0x00
CONCURRENT_OPEN_UPDATE_BL_OFF = 0x08
CONCURRENT_OPEN_RETURN_B_OFF = 0x10
CONCURRENT_CORE_OFF = 0x20
CONCURRENT_MAIN_OFF = 0x30
CONCURRENT_PENDING_DECAY_B_OFF = 0x88
CONCURRENT_ENTRY_B_OFF = 0x90
CONCURRENT_POLICY_RETURN_B_OFF = 0xAC
CONCURRENT_IDLE_RETURN_B_OFF = 0xC4
CONCURRENT_ZERO_INIT_RETURN_B_OFF = 0xCC
CONCURRENT_LEGACY_POLICY_RETURN_B_OFF = 0x28
CONCURRENT_LEGACY_IDLE_RETURN_B_OFF = 0x34
CONCURRENT_LEGACY_ZERO_INIT_RETURN_B_OFF = 0x3C
STREAM_OPEN_EPILOGUE_EXPECTED_OFF = 0x9EC
STREAM_OPEN_EPILOGUE_BYTES = 36
STREAM_OPEN_EVENT_BYTES = 4
STREAM_OPEN_THIS_MOV = 0xAA0003F3
STREAM_OPEN_MANAGER_LOAD = 0xF9400A60
STREAM_OPEN_EPILOGUE_STOCK = bytes.fromhex(
    "e003142af44f54a9f65753a9f85f52a9fa6751a9fd7b4fa9"
    "fc8340f9ff430591c0035fd6"
)
STREAM_OPEN_HELPER_TEMPLATE = bytes.fromhex(
    "74000035600a40f900000094e003142a00000014"
)
OUTPUT_POOL_TAIL_STOCK = bytes.fromhex("281740f9")
OUTPUT_POOL_TAIL_STOCK_TEMPLATE = bytes.fromhex(
    "281740f9a9835ff81f0109eb010f0054f44f5ba9f6575aa9"
    "f85f59a9fa6758a9fc6f57a9fd7b56a9ff030791c0035fd6"
)

TEXT_RELEASE_FILES = (
    "LICENSE",
    "CONTRIBUTING.md",
    "module.prop",
    "customize.sh",
    "service.sh",
    "post-fs-data.sh",
    "wrapper.sh",
    "uninstall.sh",
    "bin/a2h_apply",
    "bin/a2h_audio_watch",
    "config/packages.txt",
    "config/package_states",
    "config/state",
    "config/game_auto_pause",
    "companion/app/src/main/AndroidManifest.xml",
    "webroot/index.html",
)


def decode_aarch64_bl(site: int, instruction: int) -> int | None:
    if instruction & 0xFC000000 != 0x94000000:
        return None
    immediate = instruction & 0x03FFFFFF
    if immediate & 0x02000000:
        immediate -= 0x04000000
    return site + immediate * 4


def encode_aarch64_bl(site: int, target: int) -> int | None:
    delta = target - site
    if delta & 3:
        return None
    immediate = delta // 4
    if immediate < -0x02000000 or immediate > 0x01FFFFFF:
        return None
    return 0x94000000 | (immediate & 0x03FFFFFF)


def encode_aarch64_b(site: int, target: int) -> int | None:
    delta = target - site
    if delta & 3:
        return None
    immediate = delta // 4
    if immediate < -0x02000000 or immediate > 0x01FFFFFF:
        return None
    return 0x14000000 | (immediate & 0x03FFFFFF)


def encode_aarch64_cbz_like(
    site: int, target: int, shape: int,
) -> int | None:
    if shape & 0x7E000000 != 0x34000000:
        return None
    delta = target - site
    if delta & 3:
        return None
    immediate = delta // 4
    if immediate < -0x40000 or immediate > 0x3FFFF:
        return None
    return (shape & 0xFF00001F) | ((immediate & 0x7FFFF) << 5)


def build_idle_clear_overlay(
    update: int, policy: int, helper: int,
) -> tuple[bytes, bytes, bytes] | None:
    count_to_zero_init = encode_aarch64_cbz_like(
        policy + 0x3C, helper + 0xC8, 0xB4000A68
    )
    to_helper = encode_aarch64_b(policy + 0x188, update + 0xF0)
    to_idle_helper = encode_aarch64_b(update + 0xF4, helper + 0xBC)
    if None in (count_to_zero_init, to_helper, to_idle_helper):
        return None
    update_patch = bytearray(UPDATE_FLAGS_GAME_HANDOFF_LEGACY)
    update_patch[16:32] = bytes.fromhex(
        "04000014f3031f2a000000141f2003d5"
    )
    struct.pack_into("<I", update_patch, 24, to_idle_helper)
    return (
        bytes(update_patch), struct.pack("<I", int(count_to_zero_init)),
        struct.pack("<I", int(to_helper)),
    )


def build_concurrent_overlays(
    helper: int, policy: int, stream: int, stream_open: int,
    stream_open_patch_off: int, recompute_target: int,
    update_a2h_target: int,
) -> tuple[bytes, bytes, tuple[bytes, bytes, bytes], bytes] | None:
    policy_to_helper = encode_aarch64_b(policy + 0x1C8, helper + 0x90)
    pending_decay = encode_aarch64_b(helper + 0x88, helper + 0x20)
    helper_entry = encode_aarch64_b(helper + 0x90, helper + 0x30)
    policy_return = encode_aarch64_b(helper + 0xAC, policy + 0x1CC)
    idle_return = encode_aarch64_b(helper + 0xC4, policy + 0x18C)
    zero_init_return = encode_aarch64_b(helper + 0xCC, policy + 0x188)
    event_return = encode_aarch64_b(stream + 0x1FAC, recompute_target)
    new_node_return = encode_aarch64_b(stream + 0x2188, recompute_target)
    recompute = encode_aarch64_b(stream + 0x22B4, recompute_target)
    stream_open_site = stream_open + stream_open_patch_off
    stream_open_call = encode_aarch64_bl(stream_open_site, helper)
    stream_open_update = encode_aarch64_bl(helper + 0x08, update_a2h_target)
    stream_open_return = encode_aarch64_b(
        helper + 0x10, stream_open_site + STREAM_OPEN_EVENT_BYTES
    )
    if None in (
        policy_to_helper, pending_decay, helper_entry, policy_return, idle_return,
        zero_init_return,
        event_return, new_node_return, recompute,
        stream_open_call, stream_open_update, stream_open_return,
    ):
        return None

    helper = bytearray(UPDATE_CONCURRENT_HELPER_BYTES)
    helper[:len(STREAM_OPEN_HELPER_TEMPLATE)] = STREAM_OPEN_HELPER_TEMPLATE
    for offset in range(len(STREAM_OPEN_HELPER_TEMPLATE), CONCURRENT_CORE_OFF, 4):
        struct.pack_into("<I", helper, offset, 0xD503201F)
    helper[CONCURRENT_CORE_OFF:CONCURRENT_CORE_OFF + UPDATE_CONCURRENT_PREVIOUS_BYTES] = UPDATE_CONCURRENT_HELPER_TEMPLATE
    struct.pack_into("<I", helper, CONCURRENT_OPEN_UPDATE_BL_OFF,
                     int(stream_open_update))
    struct.pack_into("<I", helper, CONCURRENT_OPEN_RETURN_B_OFF,
                     int(stream_open_return))
    struct.pack_into("<I", helper, CONCURRENT_PENDING_DECAY_B_OFF,
                     int(pending_decay))
    struct.pack_into("<I", helper, CONCURRENT_ENTRY_B_OFF, int(helper_entry))
    struct.pack_into("<I", helper, CONCURRENT_POLICY_RETURN_B_OFF,
                     int(policy_return))
    struct.pack_into("<I", helper, CONCURRENT_IDLE_RETURN_B_OFF,
                     int(idle_return))
    struct.pack_into("<I", helper, CONCURRENT_ZERO_INIT_RETURN_B_OFF,
                     int(zero_init_return))
    concurrent_policy = bytearray(GAME_POLICY_CONCURRENT_TEMPLATE)
    struct.pack_into("<I", concurrent_policy, 0, int(policy_to_helper))
    inline_event = bytearray(STREAM_EVENT_INLINE_TEMPLATE)
    struct.pack_into("<I", inline_event, 24, int(event_return))
    new_node_event = bytearray(STREAM_EVENT_INLINE_TEMPLATE)
    struct.pack_into("<I", new_node_event, 24, int(new_node_return))
    return (
        bytes(helper), bytes(concurrent_policy),
        (
            bytes(inline_event), bytes(new_node_event),
            struct.pack("<I", int(recompute)),
        ),
        struct.pack("<I", int(stream_open_call)),
    )


def derive_executable_tail_layout(
    program_headers: list[dict[str, object]], file_size: int,
) -> dict[str, int] | None:
    loads = [item for item in program_headers if int(item["type"]) == 1]
    executable = [item for item in loads if int(item["flags"]) & 1]
    if not executable:
        return None
    selected = max(executable, key=lambda item: int(item["vaddr"]) + int(item["memsz"]))
    vaddr = int(selected["vaddr"])
    offset = int(selected["offset"])
    filesz = int(selected["filesz"])
    memsz = int(selected["memsz"])
    if filesz > memsz or min(vaddr, offset, filesz, memsz) < 0:
        return None
    file_end = vaddr + filesz
    mem_end = vaddr + memsz
    tail_start = (max(file_end, mem_end) + 15) & ~15
    tail_end = (mem_end + 4095) & ~4095
    cache = (tail_end - 112) & ~15
    concurrent = (cache - UPDATE_CONCURRENT_HELPER_BYTES) & ~15
    claimed_end = cache + 112
    if (
        concurrent < tail_start
        or concurrent + UPDATE_CONCURRENT_HELPER_BYTES > cache
        or claimed_end > tail_end
    ):
        return None
    for item in loads:
        start = int(item["vaddr"])
        end = start + int(item["memsz"])
        if start < claimed_end and end > concurrent:
            return None
    file_offset = offset + concurrent - vaddr
    span = claimed_end - concurrent
    if file_offset < 0 or file_offset + span > file_size:
        return None
    return {
        "file_end": file_end,
        "mem_end": mem_end,
        "tail_start": tail_start,
        "tail_end": tail_end,
        "concurrent": concurrent,
        "cache": cache,
        "file_offset": file_offset,
        "span": span,
    }


def decode_aarch64_b_cond(site: int, instruction: int) -> int | None:
    if instruction & 0xFF000010 != 0x54000000:
        return None
    immediate = (instruction >> 5) & 0x7FFFF
    if immediate & 0x40000:
        immediate -= 0x80000
    return site + immediate * 4


def decode_aarch64_test_branch(site: int, instruction: int) -> int | None:
    if instruction & 0x7E000000 != 0x36000000:
        return None
    immediate = (instruction >> 5) & 0x3FFF
    if immediate & 0x2000:
        immediate -= 0x4000
    return site + immediate * 4


def stream_ref_stock_shape(data: bytes) -> bool:
    if len(data) != STREAM_REF_PATCH_BYTES:
        return False
    for offset in range(0, STREAM_REF_PATCH_BYTES, 4):
        current = struct.unpack_from("<I", data, offset)[0]
        expected = struct.unpack_from("<I", STREAM_REF_STOCK_TEMPLATE,
                                      offset)[0]
        mask = STREAM_REF_VARIABLE_WORD_MASKS.get(offset, 0xFFFFFFFF)
        if current & mask != expected & mask:
            return False
    return True


def exact_plt_entry(data: bytes, program_headers: list[dict],
                    target: int) -> bool:
    file_offset = None
    for segment in program_headers:
        start = int(segment["vaddr"])
        end = start + int(segment["filesz"])
        if segment["type"] == 1 and start <= target <= end - 16:
            file_offset = int(segment["offset"]) + target - start
            break
    if file_offset is None:
        return False
    adrp, ldr, add, branch = struct.unpack_from("<IIII", data, file_offset)
    return (
        adrp & 0x9F00001F == 0x90000010
        and ldr & 0xFFC003FF == 0xF9400211
        and add & 0xFFC003FF == 0x91000210
        and branch == 0xD61F0220
        and ((ldr >> 10) & 0xFFF) << 3 == (add >> 10) & 0xFFF
    )


@dataclass
class Result:
    status: str
    name: str
    detail: str


class Report:
    def __init__(self) -> None:
        self.results: list[Result] = []

    def add(self, status: str, name: str, detail: str) -> None:
        self.results.append(Result(status, name, detail))
        print(f"[{status}] {name}: {detail}")

    def check(self, condition: bool, name: str, ok: str, bad: str) -> None:
        self.add("PASS" if condition else "FAIL", name, ok if condition else bad)

    def gap(self, name: str, detail: str) -> None:
        self.add("GAP", name, detail)

    def exit_code(self, strict_gaps: bool) -> int:
        failed = any(item.status == "FAIL" for item in self.results)
        gaps = any(item.status == "GAP" for item in self.results)
        return 1 if failed or (strict_gaps and gaps) else 0


def parse_properties(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"invalid property line: {line!r}")
        key, value = line.split("=", 1)
        if key in values:
            raise ValueError(f"duplicate property: {key}")
        values[key] = value
    return values


def extract_function(source: str, marker: str, next_marker: str) -> str:
    start = source.find(marker)
    if start < 0:
        return ""
    end = source.find(next_marker, start + len(marker))
    return source[start:] if end < 0 else source[start:end]


def locate_ndk(explicit: Path | None) -> Path | None:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit)
    for name in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT"):
        if os.environ.get(name):
            candidates.append(Path(os.environ[name]))
    candidates.extend(
        [
            Path("D:/Download/android-ndk-r26d-windows/android-ndk-r26d"),
            Path.home() / "Android/Sdk/ndk/27.0.12077973",
        ]
    )
    for candidate in candidates:
        if (candidate / "build/cmake/android.toolchain.cmake").is_file():
            return candidate.resolve()
    return None


def locate_posix_shell() -> str | None:
    override = os.environ.get("A2H_TEST_POSIX_SHELL")
    git_shells = [
        r"C:\Program Files\Git\bin\sh.exe",
        r"C:\Program Files\Git\usr\bin\sh.exe",
    ]
    candidates = ([override, shutil.which("sh")] + git_shells
                  if os.name == "nt" else [override, shutil.which("sh")])
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(candidate)
    return None


def locate_bash_shell() -> str | None:
    """Locate a shell that supports the timeout form used by the host FIFO harness."""
    override = os.environ.get("A2H_TEST_BASH_SHELL")
    git_shells = [
        r"C:\Program Files\Git\bin\bash.exe",
        r"C:\Program Files\Git\usr\bin\bash.exe",
    ]
    candidates = ([override, shutil.which("bash")] + git_shells
                  if os.name == "nt" else [override, shutil.which("bash")])
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(candidate)
    return None


def posix_shell_command(shell: str, *arguments: str) -> list[str]:
    if os.name == "nt":
        return [
            shell, "-c",
            'PATH=/usr/bin:/bin:$PATH; export PATH; exec sh "$@"',
            "sh", *arguments,
        ]
    return [shell, *arguments]


def run_command(command: list[str], cwd: Path, input_bytes: bytes | None = None) -> tuple[int, str]:
    if (os.name == "nt" and len(command) == 2 and command[1] == "-s"
            and Path(command[0]).name.lower() in {"sh.exe", "bash.exe"}):
        shell_name = "bash" if Path(command[0]).name.lower() == "bash.exe" else "sh"
        command = [
            command[0], "-c",
            f"PATH=/usr/bin:/bin:$PATH; export PATH; exec /usr/bin/{shell_name} -s",
        ]
    completed = subprocess.run(
        command,
        cwd=cwd,
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    return completed.returncode, completed.stdout.decode("utf-8", errors="replace")


def apk_signing_ids(data: bytes) -> set[int]:
    eocd = data.rfind(b"PK\x05\x06", max(0, len(data) - 65557))
    if eocd < 0 or eocd + 22 > len(data):
        raise ValueError("APK EOCD is missing")
    central_offset = struct.unpack_from("<I", data, eocd + 16)[0]
    if central_offset < 32 or data[central_offset - 16:central_offset] != b"APK Sig Block 42":
        raise ValueError("APK signing block is missing")
    block_size = struct.unpack_from("<Q", data, central_offset - 24)[0]
    block_start = central_offset - block_size - 8
    if block_start < 0 or struct.unpack_from("<Q", data, block_start)[0] != block_size:
        raise ValueError("APK signing block size mismatch")
    ids: set[int] = set()
    position = block_start + 8
    pairs_end = central_offset - 24
    while position < pairs_end:
        if position + 12 > pairs_end:
            raise ValueError("truncated APK signing pair")
        pair_size = struct.unpack_from("<Q", data, position)[0]
        if pair_size < 4 or pair_size > pairs_end - position - 8:
            raise ValueError("invalid APK signing pair size")
        ids.add(struct.unpack_from("<I", data, position + 8)[0])
        position += 8 + pair_size
    if position != pairs_end:
        raise ValueError("APK signing pairs do not fill the block")
    return ids


def check_release_tree(root: Path, report: Report, use_adb: bool) -> None:
    try:
        props = parse_properties(root / "module.prop")
    except (OSError, UnicodeError, ValueError) as exc:
        report.add("FAIL", "module properties", str(exc))
        props = {}
    required = {"id", "name", "version", "versionCode", "author", "description", "webui", "webuiIcon"}
    report.check(required <= props.keys(), "module property keys", "all required keys present", f"missing {sorted(required - props.keys())}")
    version = props.get("version", "")
    report.check(
        props.get("id") == "a2h_hook" and re.fullmatch(r"v[0-9A-Za-z][0-9A-Za-z._-]*", version) is not None,
        "module identity",
        f"id=a2h_hook version={version}",
        f"invalid id/version: {props.get('id')!r} {version!r}",
    )
    report.check(
        (version, props.get("versionCode", "")) == EXPECTED_RELEASE,
        "release version pair",
        f"version={EXPECTED_RELEASE[0]} versionCode={EXPECTED_RELEASE[1]}",
        f"expected={EXPECTED_RELEASE!r} actual={(version, props.get('versionCode', ''))!r}",
    )
    license_text = (root / "LICENSE").read_text(encoding="utf-8")
    contributing = (root / "CONTRIBUTING.md").read_text(encoding="utf-8")
    readme = (root / "README.md").read_text(encoding="utf-8")
    license_contract = (
        "GNU GENERAL PUBLIC LICENSE" in license_text
        and "Version 3, 29 June 2007" in license_text
        and "GPL-3.0-or-later" in readme
        and "GPL-3.0-or-later" in contributing
        and "书面形式明确同意" in contributing
        and "不限制 GPL" in contributing
        and "不能被追溯撤销" in readme
    )
    report.check(
        license_contract,
        "GPL and upstream governance contract",
        "GPL-3.0-or-later migration, historical boundary, and written upstream approval are explicit",
        "license text, migration boundary, or contribution governance is incomplete",
    )

    bad_text: list[str] = []
    for relative in TEXT_RELEASE_FILES:
        path = root / relative
        try:
            data = path.read_bytes()
            data.decode("utf-8")
            if data.startswith(b"\xef\xbb\xbf") or b"\r" in data:
                bad_text.append(relative)
        except (OSError, UnicodeError):
            bad_text.append(relative)
    report.check(not bad_text, "release text encoding", "UTF-8, LF, no BOM", f"invalid files: {bad_text}")

    packages = (root / "config/packages.txt").read_text(encoding="utf-8").splitlines()
    states = (root / "config/package_states").read_text(encoding="utf-8").splitlines()
    report.check(
        len(packages) == 10 and tuple(packages[:6]) == OFFICIAL,
        "default package table",
        "10 slots with six official packages",
        f"lines={len(packages)} official={packages[:6]!r}",
    )
    report.check(
        states == ["1"] * 6 + ["0"] * 4,
        "default package states",
        "six enabled and four disabled",
        f"states={states!r}",
    )
    game_policy = (root / "config/game_auto_pause").read_text(encoding="utf-8")
    report.check(
        game_policy == "enabled\n",
        "default game auto-pause policy",
        "enabled (Xiaomi stock behavior)",
        f"unexpected value: {game_policy!r}",
    )

    patcher = (root / "src/patcher_v3.c").read_text(encoding="utf-8")
    version_match = re.search(r'#define\s+A2H_VERSION\s+"([^"]+)"', patcher)
    source_version = version_match.group(1) if version_match else ""
    report.check(
        version == f"v{source_version}",
        "patcher version metadata",
        f"module={version} patcher={source_version}",
        f"module={version!r} patcher={source_version!r}",
    )

    webui = (root / "webroot/index.html").read_text(encoding="utf-8")
    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    companion_styles = (root / "companion/app/src/main/res/values/styles.xml").read_text(encoding="utf-8")
    companion_manifest = (root / "companion/app/src/main/AndroidManifest.xml").read_text(encoding="utf-8")
    companion_activity = (root / "companion/app/src/main/java/io/github/bbbomb0/a2hhook/WebUiActivity.java").read_text(encoding="utf-8")
    performance_contract = (
        "apply-final-verified" in patcher
        and "elf_symbol_cache_lookup" in patcher
        and "elf_symbol_cache_store" in patcher
        and "setpriority(PRIO_PROCESS, 0, -10)" in patcher
        and "patcher_apply_final_verified()" in applier
        and "native final verification accepted" in applier
        and "queue_changed_fast()" in applier
        and "mark_dirty_pending()" in applier
        and "prepare_config_fast_unlocked()" in applier
        and "compute_config_and_table_fingerprints()" in applier
        and "CFG_NORMALIZED_FINGERPRINT" in applier
        and "config fast prepared" in applier
        and "queue worker found newer config" not in applier
        and 'nohup sh "$SELF" queue' not in applier
    )
    report.check(
        performance_contract,
        "fast apply capability contract",
        "verified native apply avoids a second ptrace check, controls share one stat snapshot per validation pass, and config changes start one boosted worker",
        "one of the verified-apply, symbol-cache, priority, or direct-worker safeguards is missing",
    )
    report.check(
        version in webui,
        "WebUI version metadata",
        f"WebUI contains {version}",
        f"WebUI does not contain {version}",
    )
    companion_theme_contract = (
        "Theme.Material.Light" not in companion_styles
        and "Theme.DeviceDefault.DayNight" in companion_styles
        and 'android:configChanges="uiMode"' in companion_manifest
        and "public void onConfigurationChanged(Configuration configuration)" in companion_activity
        and "settings.setAlgorithmicDarkeningAllowed(false)" in companion_activity
        and "window.__A2H_NATIVE_DARK__=" in companion_activity
        and "window.A2HNativeTheme&&window.A2HNativeTheme(" in companion_activity
    )
    report.check(
        companion_theme_contract,
        "companion system-theme ownership contract",
        "DayNight host and live uiMode bridge own companion WebUI system theme",
        "companion host still locks light mode or does not propagate live uiMode changes",
    )
    miuix_webui = (
        "--primary:#3482ff" in webui
        and 'class="group"' in webui
        and 'id="aboutPage"' in webui
        and 'id="donatePage"' in webui
        and 'id="qrPage"' in webui
        and 'id="strongHaptic"' in webui
        and 'id="donateImage" alt=""' in webui
        and "history.pushState" in webui
        and "window.A2HWebUIBack" in webui
        and "window.addEventListener('popstate'" in webui
        and "donate-wechat-pay.webp" in webui
        and "donate-wechat.webp" in webui
        and "donate-alipay.webp" in webui
        and "https://qm.qq.com/q/nOF82hSWwU" in webui
        and "function haptic(" in webui
        and "navigator.vibrate" in webui
        and "cmd vibrator_manager synced -f" in webui
        and "cmd vibrator vibrate -f" in webui
        and "游戏时启动后台音乐触感" in webui
        and "data-app-package=\"com.tencent.mobileqq\"" in webui
        and "data-no-browser-fallback" in webui
        and "mqqapi://card/show_pslcard?src_type=internal&amp;version=1&amp;uin=778505328" in webui
        and "navigateRoute('donate')" in webui
        and "about-row about-author" in webui
        and "data-theme-mode=\"system\"" in webui
        and "localStorage.removeItem('a2h_theme')" in webui
        and "viewport-fit=cover" in webui
        and "--a2h-safe-top:var(--a2h-native-safe-top,var(--safe-area-inset-top,env(safe-area-inset-top,0px)))" in webui
        and "--a2h-safe-bottom:var(--a2h-native-safe-bottom,var(--safe-area-inset-bottom,env(safe-area-inset-bottom,0px)))" in webui
        and "style.setProperty('--a2h-native-safe-top'" in webui
        and "style.setProperty('--a2h-native-safe-bottom'" in webui
        and "window.A2HSystemInsets=" in webui
        and "function enableImmersiveLayout(" in webui
        and "window.ksu.enableEdgeToEdge(true)" in webui
        and "window.ksu.enableInsets(true)" in webui
        and "function syncSystemBars(" in webui
        and "window.ksu.setSystemBarsDark(dark)" in webui
        and "fullScreen(" not in webui
        and "align-items:flex-end" in webui
        and "border-radius:28px 28px 0 0" in webui
        and "background-clip:padding-box" in webui
        and "box-shadow:none;isolation:isolate" in webui
        and "@media(max-height:900px)" in webui
        and ".slot{height:44px}" in webui
        and webui.count('class="sheet-handle-area"') == 3
        and "window.visualViewport" in webui
        and "--ime-inset" in webui
        and "function showCommand(" in webui
        and "function bindSheetDrag(" in webui
        and "function bindSnackbarSwipe(" in webui
        and "function isTextEntry(" in webui
        and "kind==='err'?10000:4000" in webui
        and "@keyframes sheet-in" in webui
        and webui.count('class="a2h-logo') == 2
        and 'class="a2h-logo project-logo"' in webui
        and '<div class="mark">A2</div>' not in webui
        and ".project-mark{width:96px;height:84px;color:var(--primary)" in webui
        and "visibilitychange" in webui
        and "motion-active" in webui
        and ".shell').classList.toggle('motion-active'" in webui
        and "function prioritizeInteraction(" in webui
        and "!applyBusy&&Date.now()>=motionPausedUntil" in webui
        and "requestAnimationFrame(()=>setTimeout(run,0))" in webui
        and "window.A2HNativeTheme=" in webui
        and "window.__A2H_NATIVE_DARK__" in webui
        and "const commit=execRoot(command,15000)" in webui
        and "flushRootHaptic();" in webui
        and "animation:a2h-core-loop 1.3s ease-in-out infinite" in webui
        and "a2h-core-intro" not in webui
        and "a2h-orbit-outer-intro" not in webui
        and "a2h-wave-left-intro" not in webui
        and "@keyframes a2h-orbit-outer-loop" in webui
        and "@keyframes a2h-wave-left-loop" in webui
        and webui.count('class="logo-wave') == 4
        and "M9 24l-6 8 6 8" not in webui
        and "setInterval(" not in webui
        and webui.count('<svg class="brand-icon') == 2
        and 'class="brand-icon qq-icon"' in webui
        and 'class="qq-belly"' not in webui
        and "qq-mascot" not in webui
        and "qq-cut" not in webui
        and "@keyframes qq-wave" not in webui
        and webui.count('class="support-icon brand-payment"') == 3
        and 'src="payment-wechat-pay.webp"' in webui
        and 'src="payment-wechat-reward.webp"' in webui
        and 'src="payment-alipay.webp"' in webui
        and 'event.target!==page' in webui
        and "document.querySelectorAll('.route-page')" in webui
        and "support-option-summary" not in webui
        and "微信扫码支付" not in webui
        and "支付宝扫码赞赏" not in webui
        and "选择赞赏方式" not in webui
        and "选择后才会显示对应二维码" not in webui
    )
    report.check(
        miuix_webui,
        "Miuix WebUI interaction contract",
        "compact grouped layout, animated sheets, outside-tap back, transparent music-haptic marks, solid QQ and optimized payment icons present",
        "Miuix layout, support assets or haptic fallback is incomplete",
    )
    request_apply = extract_function(
        webui, "function requestApply(reason,writePackages){", "async function drainApply(){"
    )
    apply_loop = extract_function(
        webui, "async function drainApply(){", "function parseDeviceSection("
    )
    raw_bridge = extract_function(
        webui, "function invokeRawBridge(", "function invokeGenericBridge("
    )
    state_contract = (
        "APPLY_DELAY" not in webui
        and "applyTimer" not in webui
        and "prioritizeInteraction(420);" in request_apply
        and "scheduleApplyDrain();" in request_apply
        and "interactionBusy=true;" in request_apply
        and "renderMode();" in request_apply
        and "window[callbackName]=" in raw_bridge
        and "command,'{}',callbackName" in raw_bridge
        and "verifyDeviceConfig(text,data,writePackages);" in apply_loop
        and "loadDeviceConfig({preserveNotice:true})" in apply_loop
        and "if(!applyPending) interactionBusy=false;" in apply_loop
        and "const interactionLocked=applyBusy||interactionBusy;" in webui
        and "input.disabled=!deviceConfigReady||applyBusy||interactionBusy;" in webui
        and "toggle.disabled=!deviceConfigReady||applyBusy||interactionBusy||!value;" in webui
        and "state!=='enabled'&&state!=='disabled'" in webui
        and "policy!=='enabled'&&policy!=='disabled'" in webui
        and "device-policy-mismatch" in webui
    )
    report.check(
        state_contract,
        "WebUI device-state persistence contract",
        "next-frame dispatch, string callback, strict readback, and failure restore present",
        "mode persistence safeguards are incomplete or a time-based configuration debounce returned",
    )
    commit_builder = extract_function(
        webui, "function buildCommitCommand(state,snapshot,writePackages){", "function buildCommand("
    )
    commit_contract = (
        'a2h_apply commit-queue' in commit_builder
        and 'data.generation' in commit_builder
        and 'values.map(shellQuote)' in commit_builder
        and 'states.map(shellQuote)' in commit_builder
        and 'queue-controls-fast' in commit_builder
        and 'execRoot(command,15000)' in webui
    )
    report.check(
        commit_contract,
        "WebUI grouped commit marker order",
        "one validated commit-queue command carries generation, 10 packages and 10 states",
        "WebUI commit command is incomplete",
    )
    report.check(
        "commit_config_and_queue()" in applier and
        "commit_config_abort()" in applier and
        "commit_restore_one()" in applier and
        "commit_config_signal()" in applier and
        "commit_config_cleanup()" in applier and
        "queue-controls-fast)" in applier,
        "WebUI grouped commit rollback contract",
        "native applier owns atomic five-file rollback and direct fast worker queue",
        "unified commit/rollback entry or fast queue path is incomplete",
    )

    service = (root / "service.sh").read_text(encoding="utf-8")
    watcher = extract_function(
        service,
        "config_inotify_pid=\nconfig_inotify_enabled=0\naudio_watcher_pid=\n",
        "done\n",
    )
    watcher_contract = (
        "watch_tick_seconds=2" in watcher
        and "watch_stable_ticks=3" in watcher
        and "watch_health_ticks=15" in watcher
        and "applier_busy" in watcher
        and "snapshot-state" in watcher
        and "changed across snapshot" in watcher
        and "CONFIG_EVENT_MARKER" in service
        and 'state|game_auto_pause|packages.txt|package_states|config_generation)' in service
        and "inotifyd" in service
        and "signature_needed" in watcher
        and "config_poll_grace_ticks" in watcher
        and "watch_stable_ticks + 1" in watcher
        and "sleep 25" not in watcher
        and "Never let the slower health path" in watcher
        and 'current_pid=$(find_hal_pid)' in watcher
        and 'read -r current_revision < "$CFG_REVISION"' in watcher
        and 'read -r applied_revision < "$APPLIED_REVISION"' in watcher
        and 'if [ "$current_pid" != "$last_pid" ]; then' in watcher
        and '[ "$current_snapshot" != "$applied_snapshot" ] ||' in watcher
        and '[ "$current_revision" != "$applied_revision" ]' in watcher
        and '"$APPLIER" check' not in watcher
        and '"$APPLIER" status' not in watcher
        and '"$APPLIER" show' not in watcher
        and "apply_reason=live-check" not in watcher
    )

    notification = extract_function(
        service, "notification_runtime_result() {", "record_notification_marker() {"
    )
    notification_contract = (
        bool(notification)
        and 'sh "$APPLIER" snapshot-state' in notification
        and 'notification_pid=$(find_hal_pid)' in notification
        and '"$notification_pid" = "$notification_last_pid"' in notification
        and '"$notification_snapshot" = "$notification_applied_snapshot"' in notification
        and '"$notification_revision" = "$notification_applied_revision"' in notification
        and '"$APPLIER" check' not in service
        and '"$APPLIER" status' not in service
        and '"$APPLIER" show' not in service
    )
    report.check(
        notification_contract,
        "non-intrusive service notification contract",
        "automatic notification and watcher paths use PID/config metadata without native inspection",
        "service notification metadata checks are incomplete or an automatic native inspection returned",
    )
    report.check(
        watcher_contract,
        "non-intrusive watcher health contract",
        "FIFO-blocked idle, 2-second debounce, and 30-second PID/config probes never launch a native inspection",
        "watcher debounce/probes are incomplete or a periodic native inspection returned",
    )

    shell = locate_posix_shell()
    if not shell:
        report.gap("watcher steady-state runtime regression", "POSIX sh is required")
    else:
        rc, output = run_command(
            posix_shell_command(shell, "tests/watcher_steady_state_harness.sh", "service.sh"), root
        )
        report.check(
            rc == 0 and "WATCHER_STEADY_PASS" in output,
            "watcher steady-state runtime regression",
            output.strip(),
            output.strip() or f"harness exited {rc}",
        )
        rc, output = run_command(
            posix_shell_command(shell, "tests/watcher_event_rearm_harness.sh", "service.sh"), root
        )
        report.check(
            rc == 0 and "WATCHER_REARM_PASS" in output,
            "watcher inotify rearm runtime regression",
            output.strip(),
            output.strip() or f"harness exited {rc}",
        )

    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    report.check(
        'queue worker retained pending request after failure' in applier
        and 'release_worker_lock\n      if [ -f "$PENDING_FILE" ] && claim_worker_lock; then' in applier,
        "queue worker failure handoff",
        "a request arriving during a failed apply is reclaimed or left for a new worker",
        "failed worker can release its lock while leaving an unconsumed pending request",
    )
    report.check(
        'touch "$ACTION_LOG" "$MAIN_LOG"' not in applier
        and '[ -e "$ACTION_LOG" ] || : > "$ACTION_LOG"' in applier
        and '[ -e "$MAIN_LOG" ] || : > "$MAIN_LOG"' in applier,
        "steady-state log metadata contract",
        "snapshot-state creates missing logs without touching existing files",
        "periodic metadata probes can still refresh existing log mtimes",
    )
    report.check(
        "queue_changed_fast()" in applier
        and "mark_dirty_pending()" in applier
        and "coalesce_pending_after_success()" in applier
        and 'mv "$PENDING_FILE" "$coalesce_claim"' in applier
        and 'raw_config_matches "$stable_before_raw"' in applier
        and "queue worker coalesced" in applier
        and "queue worker preserved newer config" in applier
        and 'renice -n -10 -p "$$"' in applier
        and "toggle-fast)" in applier
        and "toggle-game-auto-pause-fast|" in applier,
        "quick settings direct worker queue contract",
        "both tiles commit under the config lock, start one worker, and coalesce only signature-proven stale requests",
        "tile commands bypass the production worker or stale-request coalescing can lose newer configuration",
    )

    postfs = (root / "post-fs-data.sh").read_text(encoding="utf-8")
    postfs_contract = (
        'cleanup_stale_runtime "$POSTFS_LOCAL_TMP"' in postfs
        and 'getprop sys.boot_completed' in postfs
        and '!= "1"' in postfs
        and "a2h_apply.pending" in postfs
        and "a2h_apply.worker" in postfs
        and "a2h_apply.lock" in postfs
        and "a2h_config.lock" in postfs
        and "a2h_packages.txt" in postfs
        and "a2h_state" in postfs
        and "a2h_config.wake" in postfs
        and 'POSTFS_AUDIO_RUNTIME=${A2H_POSTFS_AUDIO_RUNTIME:-$POSTFS_LOCAL_TMP/a2h_hook_runtime}' in postfs
        and 'POSTFS_SERVICE_LOCK=${A2H_POSTFS_SERVICE_LOCK:-$POSTFS_LOCAL_TMP/a2h_hook_service.lock}' in postfs
        and "runtime_owner_alive" in postfs
        and "lock_epoch_signature" in postfs
        and "cleanup_previous_boot_runtime" in postfs
        and 'grep -a -F -q "$owned_process_name"' in postfs
    )
    report.check(
        postfs_contract,
        "previous-boot runtime cleanup contract",
        "post-fs-data removes only stable non-live module epochs and preserves temporary-root owners",
        "owner-aware early-boot cleanup or one of the module runtime artifacts is missing",
    )

    audio_watcher = (root / "bin/a2h_audio_watch").read_text(encoding="utf-8")
    trigger_source = (root / "src/trigger.c").read_text(encoding="utf-8")
    service = (root / "service.sh").read_text(encoding="utf-8")
    uninstall = (root / "uninstall.sh").read_text(encoding="utf-8")
    audio_watcher_contract = (
        "vendor.xiaomi.hardware.mediaeventgatherservice-service" in audio_watcher
        and 'event_name" = "audio_track_message"' in audio_watcher
        and 'event_scenario" = "playback"' in audio_watcher
        and "IFS='|' read -r event_name event_package event_scenario" in audio_watcher
        and "/data/system/packages.list" in audio_watcher
        and 'exec 3< "$CFG_STATES"' in audio_watcher
        and 'exec 4< "$CFG_PKGS"' in audio_watcher
        and '"$SU_BIN" "$lease_uid" -c "$lease_command"' in audio_watcher
        and '"$TRIGGER_RUNTIME --lease $lease_token $lease_session"' in audio_watcher
        and "AAudioStream_waitForStateChange" in trigger_source
        and "AAudioStreamBuilder_setDataCallback" in trigger_source
        and "wait_for_first_callback(stream, &trigger_state)" in trigger_source
        and "write_session_file(session_file, stream, 1)" in trigger_source
        and "AAudioStream_requestStop(stream)" in trigger_source
        and "FALLBACK_LEASE_MS = 70000" in trigger_source
        and 'cooldown_accept "$trigger_package"' in audio_watcher
        and "read -r cooldown_now cooldown_unused < /proc/uptime" in audio_watcher
        and 'printf \'%s\\n\' "$cooldown_now" > "$cooldown_file"' in audio_watcher
        and 'chmod 0711 "$RUNTIME_DIR"' in audio_watcher
        and 'chmod 0555 "$trigger_tmp"' in audio_watcher
        and 'CORE_UID_DIR="$RUNTIME_DIR/core_uid"' in audio_watcher
        and '[ ! -f "$CORE_UID_DIR/$trigger_package" ] || return 0' in audio_watcher
        and ': > "$CORE_UID_DIR/$trigger_package"' in audio_watcher
        and "mkfifo" in audio_watcher
        and "logcat_pid" in audio_watcher
        and "watch_restart_delay=2" in audio_watcher
        and 'watch_restart_delay" -le 30' in audio_watcher
        and 'command -v "$LOGCAT_BIN"' in audio_watcher
        and '"${#package_value}" -lt 256' in audio_watcher
        and '"$trigger_uid" -lt 10000' in audio_watcher
        and "APM_AudioPolicyManager:D" in audio_watcher
        and "AudioPolicyManager:D" in audio_watcher
        and "*'stopOutput()'*|*'stoptOutput()'*" in audio_watcher
        and 'policy_port_file="$PORT_DIR/$policy_port"' in audio_watcher
        and 'trigger_session_matches "$policy_package" "$policy_session"' in audio_watcher
        and 'lease_worker_start=$(process_starttime "$lease_worker")' in audio_watcher
        and 'RECONCILE_MARKER="$RUNTIME_DIR/reconcile"' in audio_watcher
        and 'reconcile_active_leases()' in audio_watcher
        and 'stop_lease_for_reconcile()' in audio_watcher
        and '"__A2H_RECONCILE__"' in audio_watcher
        and '"$RECONCILE_LOG_TAG:I"' in audio_watcher
        and 'SERVICE_LOCK_DIR=/data/local/tmp/a2h_hook_service.lock' in service
        and "acquire_service_lock" in service
        and "service_lock_root_owned" in service
        and "stale_service_lock_recheck" in service
        and "release_service_lock" in service
        and "service_lock_still_owned" in service
        and "service ownership lost" in service
        and 'WATCH_LOCK_DIR=${A2H_WATCH_LOCK_DIR:-$RUNTIME_DIR/watcher.lock}' in audio_watcher
        and "acquire_watcher_lock" in audio_watcher
        and "stale_owned_dir_recheck" in audio_watcher
        and "release_watcher_lock" in audio_watcher
        and "watcher_lock_still_owned" in audio_watcher
        and "require_watcher_lock" in audio_watcher
        and "ownership lost expected=" in audio_watcher
        and 'LEASE_CLAIM_DIR="$RUNTIME_DIR/lease_claims"' in audio_watcher
        and "acquire_lease_claim" in audio_watcher
        and "release_lease_claim" in audio_watcher
        and "start_lease_owned" in audio_watcher
        and "dumpsys" not in audio_watcher
        and "ptrace" not in audio_watcher.lower()
        and "com.tencent.tmgp.sgame" not in audio_watcher
        and "com.tencent.game.rhythmmaster" not in audio_watcher
        and 'AUDIO_WATCHER="$MODDIR/bin/a2h_audio_watch"' in service
        and "start_audio_watcher || true" in service
        and "stop_audio_watcher" in service
        and 'if [ "$health_due" = "1" ]; then' in service
        and 'CONFIG_WAKE_FIFO=/data/local/tmp/a2h_config.wake' in service
        and "prepare_config_wake_fifo()" in service
        and "wait_for_watch_tick()" in service
        and 'read -r -t "$((watch_tick_seconds * watch_health_ticks))"' in service
        and "/data/local/tmp/a2h_hook_runtime" in uninstall
    )
    report.check(
        audio_watcher_contract,
        "arbitrary audio UID watcher contract",
        "vendor playback and AudioPolicy ports use exact global/slot policy, real UID/session, singleton ownership, atomic per-package claims and PID/starttime cleanup",
        "audio watcher metadata, UID, lifecycle, or no-hardcoded-package contract is incomplete",
    )

    applier_reconcile_contract = (
        'AUDIO_RECONCILE_MARKER="$AUDIO_RUNTIME_DIR/reconcile"' in applier
        and "signal_audio_reconcile()" in applier
        and "record_applied" in applier
        and "signal_audio_reconcile" in applier
        and "__A2H_RECONCILE__" in applier
        and "A2H_SKIP_AUDIO_RECONCILE" in applier
    )
    report.check(
        applier_reconcile_contract,
        "mode-switch audio reconcile contract",
        "successful configuration transactions signal the single watcher to re-evaluate active media leases",
        "applier is missing the root-only reconcile marker/log wake path",
    )

    packager = (root / "package_module.py").read_text(encoding="utf-8")
    forbidden = ("tests/", "zygisk/", "a2h_hook.so", "a2h_inject")
    manifest_area = extract_function(packager, "FILES = (", "EXECUTABLE =")
    leaked = [item for item in forbidden if item in manifest_area]
    report.check(not leaked, "release manifest isolation", "tests and legacy injection excluded", f"forbidden manifest entries: {leaked}")
    report.check(
        "ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)" in packager
        and "ZipInfo(filename=relative, date_time=ZIP_TIMESTAMP)" in packager
        and "select_compression(data)" in packager
        and "info.compress_size > info.file_size" in packager
        and "ZipInfo.from_file" not in packager,
        "reproducible release timestamp contract",
        "all ZIP members use a fixed DOS timestamp and skip size-increasing compression",
        "release ZIP still inherits source mtimes or lacks adaptive compression",
    )
    text_manifest_area = extract_function(packager, "TEXT_FILES = {", "ZIP_TIMESTAMP")
    report.check(
        not any(suffix in text_manifest_area for suffix in (".apk", ".png", ".webp", ".so")),
        "release text/binary classification",
        "only textual members receive BOM and line-ending validation",
        "binary assets are still classified as text and can fail packaging on arbitrary bytes",
    )

    build_script = (root / "build.sh").read_text(encoding="utf-8")
    clean_block = extract_function(build_script, "        clean)", "        zip)")
    report.check(
        "a2h_hook_*.zip" not in clean_block
        and 'rm -f "$MODULE_DIR/a2h_hook_${MODULE_VERSION}.zip"' in clean_block
        and "*[!A-Za-z0-9._-]*" in clean_block,
        "release clean isolation",
        "clean validates the version and removes only its ZIP while preserving historical releases",
        "build.sh clean can delete historical release ZIPs or accepts an unsafe version path",
    )

    installer = (root / "customize.sh").read_text(encoding="utf-8")
    report.check(
        '[ -f "$MODDIR/config/.package_baseline" ] &&' in installer
        and 'wc -l < "$MODDIR/config/.package_baseline"' in installer,
        "clean-install baseline guard",
        "installer checks baseline existence before input redirection",
        "first install can emit a missing .package_baseline redirection error",
    )
    report.check(
        "for name in state game_auto_pause packages.txt package_states config_generation .package_baseline" in installer
        and re.search(r'repair_backslash_entry\s+"\$MODDIR/config\\\\game_auto_pause"', installer) is not None
        and 'pm install --user 0 "$companion_apk"' in installer
        and 'pm uninstall io.github.bbbomb0.a2hhook' in installer,
        "installer policy/companion migration",
        "game policy survives upgrades; installer clean-installs and service has a boot-time fallback",
        "installer does not preserve game policy or companion install command drifted",
    )

    scripts = [
        "customize.sh", "service.sh", "post-fs-data.sh", "wrapper.sh",
        "uninstall.sh", "bin/a2h_apply", "bin/a2h_audio_watch",
    ]
    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "Android shell syntax", "adb requested but not found")
        else:
            failed: list[str] = []
            for relative in scripts:
                rc, output = run_command([adb, "shell", "sh", "-n"], root, (root / relative).read_bytes())
                if rc != 0:
                    failed.append(f"{relative}: {output.strip()}")
            report.check(not failed, "Android shell syntax", "all release scripts pass /system/bin/sh -n", "; ".join(failed))
    else:
        report.gap("Android shell syntax", "rerun with --adb against an Android device")

    scripts_found = re.findall(r"<script(?:\s[^>]*)?>(.*?)</script>", webui, re.DOTALL | re.IGNORECASE)
    node = shutil.which("node")
    if not scripts_found or not node:
        report.gap("WebUI JavaScript syntax", "inline script or Node.js unavailable")
    else:
        script_bytes = "\n".join(scripts_found).encode("utf-8")
        rc, output = run_command([node, "--check", "-"], root, script_bytes)
        report.check(rc == 0, "WebUI JavaScript syntax", "node --check passed", output.strip())


def check_companion(root: Path, report: Report) -> None:
    manifest_path = root / "companion/app/src/main/AndroidManifest.xml"
    apk_path = root / "companion/a2h_companion.apk"
    try:
        manifest = ET.parse(manifest_path).getroot()
    except (OSError, ET.ParseError) as exc:
        report.add("FAIL", "companion manifest", str(exc))
        return

    android = "{http://schemas.android.com/apk/res/android}"
    application = manifest.find("application")
    permissions = {
        node.get(android + "name", "") for node in manifest.findall("uses-permission")
    }
    activity = manifest.find("application/activity")
    services = manifest.findall("application/service")
    activity_actions = {
        node.get(android + "name", "")
        for node in manifest.findall("application/activity/intent-filter/action")
    }
    service_actions = {
        node.get(android + "name", "")
        for service_node in services
        for node in service_node.findall("intent-filter/action")
    }
    service_names = {node.get(android + "name", "") for node in services}
    manifest_ok = (
        application is not None
        and application.get(android + "allowBackup") == "false"
        and application.get(android + "usesCleartextTraffic") == "false"
        and permissions == {"android.permission.VIBRATE"}
        and "android.permission.INTERNET" not in permissions
        and activity is not None
        and activity.get(android + "name") == ".WebUiActivity"
        and activity.get(android + "enableOnBackInvokedCallback") == "true"
        and activity.get(android + "exported") == "true"
        and "android.service.quicksettings.action.QS_TILE_PREFERENCES" in activity_actions
        and service_names == {".A2HTileService", ".A2HGameTileService"}
        and all(
            node.get(android + "permission") == "android.permission.BIND_QUICK_SETTINGS_TILE"
            and node.get(android + "exported") == "true"
            for node in services
        )
        and "android.service.quicksettings.action.QS_TILE" in service_actions
    )
    report.check(
        manifest_ok,
        "companion manifest security",
        "no network/accessibility/foreground permission; protected TileService and fixed preference activity",
        f"permissions={sorted(permissions)!r} activity_actions={sorted(activity_actions)!r} service_actions={sorted(service_actions)!r}",
    )

    build_script = (root / "companion/build.ps1").read_text(encoding="utf-8")
    report.check(
        "$VersionName = '1.5.8'" in build_script
        and "$VersionCode = '1580'" in build_script
        and "--min-sdk-version', '29'" in build_script
        and "--target-sdk-version', '35'" in build_script
        and "signing.properties" in build_script,
        "companion build metadata",
        "version 1.5.8/1580, API 29-35, external stable signing config",
        "companion build version/API/signing metadata mismatch",
    )

    web_activity = (root / "companion/app/src/main/java/io/github/bbbomb0/a2hhook/WebUiActivity.java").read_text(encoding="utf-8")
    bridge = (root / "companion/app/src/main/java/io/github/bbbomb0/a2hhook/A2HBridge.java").read_text(encoding="utf-8")
    root_shell = (root / "companion/app/src/main/java/io/github/bbbomb0/a2hhook/RootShell.java").read_text(encoding="utf-8")
    tile = (root / "companion/app/src/main/java/io/github/bbbomb0/a2hhook/A2HTileService.java").read_text(encoding="utf-8")
    tile_base = (root / "companion/app/src/main/java/io/github/bbbomb0/a2hhook/A2HToggleTileService.java").read_text(encoding="utf-8")
    game_tile = (root / "companion/app/src/main/java/io/github/bbbomb0/a2hhook/A2HGameTileService.java").read_text(encoding="utf-8")
    java_security = (
        "settings.setBlockNetworkLoads(true)" in web_activity
        and "settings.setAllowFileAccess(false)" in web_activity
        and "settings.setAllowContentAccess(false)" in web_activity
        and "WebView.setWebContentsDebuggingEnabled(false)" in web_activity
        and "webView.pauseTimers()" in web_activity
        and "webView.resumeTimers()" in web_activity
        and "webView.stopLoading()" in web_activity
        and "((ViewGroup) webView.getParent()).removeView(webView)" in web_activity
        and '"https".equals(uri.getScheme())' in web_activity
        and '"/coolapk.webp".equals(path)' in web_activity
        and '"/donate-wechat-pay.webp".equals(path)' in web_activity
        and '"/donate-wechat.webp".equals(path)' in web_activity
        and '"/donate-alipay.webp".equals(path)' in web_activity
        and '"/payment-wechat-pay.webp".equals(path)' in web_activity
        and '"/payment-wechat-reward.webp".equals(path)' in web_activity
        and '"/payment-alipay.webp".equals(path)' in web_activity
        and '"/".equals(uri.getPath())' in web_activity
        and "return null;" in web_activity
        and "Color.TRANSPARENT" in web_activity
        and "setOnApplyWindowInsetsListener" in web_activity
        and "setDecorFitsSystemWindows(false)" in web_activity
        and "SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN" in web_activity
        and "SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION" in web_activity
        and "setStatusBarContrastEnforced(false)" in web_activity
        and "setNavigationBarContrastEnforced(false)" in web_activity
        and "WindowInsets.Type.systemBars()" in web_activity
        and "WindowInsets.Type.displayCutout()" in web_activity
        and "getInsetsIgnoringVisibility" in web_activity
        and "getDisplayMetrics().density" in web_activity
        and "Math.round(physicalPixels / density)" in web_activity
        and "window.A2HSystemInsets" in web_activity
        and "webParams.topMargin" not in web_activity
        and "FrameLayout" in web_activity
        and "requestApplyInsets()" in web_activity
        and "addJavascriptInterface" in web_activity
        and "public void onBackPressed()" in web_activity
        and "OnBackInvokedCallback" in web_activity
        and "registerOnBackInvokedCallback" in web_activity
        and "unregisterOnBackInvokedCallback" in web_activity
        and "dispatchBackToWebUi" in web_activity
        and "finishAfterTransition()" in web_activity
        and "window.A2HWebUIBack" in web_activity
        and "evaluateJavascript" in web_activity
        and "CALLBACK_NAME.matcher(callbackName).matches()" in bridge
        and "HAPTIC_KIND" in bridge
        and "public void setSystemBarsDark(boolean dark)" in bridge
        and "miui.util.HapticFeedbackUtil" in bridge
        and "performHapticFeedback" in bridge
        and "VibrationEffect.createPredefined" in bridge
        and "process.getOutputStream().close()" in root_shell
        and root_shell.count("setDaemon(true)") == 2
        and "a2h_apply toggle" in tile
        and "A2H 全局音乐触感" in tile
        and "ic_a2h_tile_off" in tile
        and "onStartListening" in tile_base
        and "toggle-game-auto-pause" in game_tile
        and "游戏时启动后台音乐触感" in game_tile
        and 'return "disabled".equals(state);' in game_tile
        and 'return drawableId("ic_a2h_tile_game_off");' in game_tile
        and 'return drawableId("ic_a2h_tile_game_on");' in game_tile
        and "isActiveState(state)" in tile_base
        and "setIcon(Icon.createWithResource" in tile_base
        and 'RootShell.run("sh " + toggleCommand(), 10000)' in tile_base
        and "STATE_MARKER" not in tile_base
    )
    report.check(
        java_security,
        "companion local-root bridge boundary",
        "fixed local assets, blocked network/file/content access, strict callback, bounded HyperOS haptics and two fast-feedback tile bridges",
        "companion WebView or tile bridge security contract is incomplete",
    )

    android_attr = "{http://schemas.android.com/apk/res/android}pathData"
    game_icon_paths = {}
    for state in ("on", "off"):
        icon_root = ET.parse(
            root / f"companion/app/src/main/res/drawable/ic_a2h_tile_game_{state}.xml"
        ).getroot()
        game_icon_paths[state] = [
            node.attrib[android_attr]
            for node in icon_root.findall("path")
            if android_attr in node.attrib
        ]
    vibration_path = next(
        (path for path in game_icon_paths["on"] if "M0.4,13.7L2,15.3" in path),
        "",
    )
    vibration_segments = [
        tuple(float(value) for value in values)
        for values in re.findall(
            r"M([0-9.]+),([0-9.]+)L([0-9.]+),([0-9.]+)", vibration_path
        )
    ]
    vibration_geometry_ok = (
        len(vibration_segments) == 4
        and all(abs((x2 - x1) - (y2 - y1)) < 1e-6 for x1, y1, x2, y2 in vibration_segments)
        and sum((x1 + x2) / 2 < 4 for x1, _, x2, _ in vibration_segments) == 2
        and sum((x1 + x2) / 2 > 20 for x1, _, x2, _ in vibration_segments) == 2
    )
    controller_body = "M4,10C4.5,7.6 6.2,6.5 8,7"
    note = "M14.1,0.8v3"
    pause_slash = "M2.6,5.8L21.4,21.2"
    global_icon_paths = {}
    for state, resource in (("on", "ic_a2h_tile.xml"), ("off", "ic_a2h_tile_off.xml")):
        icon_root = ET.parse(
            root / f"companion/app/src/main/res/drawable/{resource}"
        ).getroot()
        global_icon_paths[state] = [
            node.attrib[android_attr]
            for node in icon_root.findall("path")
            if android_attr in node.attrib
        ]
    report.check(
        vibration_geometry_ok
        and any(controller_body in path for path in game_icon_paths["on"])
        and any(note in path for path in game_icon_paths["on"])
        and any(pause_slash in path for path in game_icon_paths["on"])
        and not any(pause_slash in path for path in game_icon_paths["off"]),
        "game tile reference icon geometry",
        "simple dual-grip controller, disjoint note/buttons/haptics, stock-policy slash and background-haptic no-slash state",
        f"on={game_icon_paths['on']!r} off={game_icon_paths['off']!r}",
    )
    report.check(
        not any(pause_slash in path for path in global_icon_paths["on"])
        and any(pause_slash in path for path in global_icon_paths["off"]),
        "global tile disabled icon geometry",
        "global mode uses the base icon; custom mode uses the same long pause slash",
        f"on={global_icon_paths['on']!r} off={global_icon_paths['off']!r}",
    )

    service_script = (root / "service.sh").read_text(encoding="utf-8")
    uninstall_script = (root / "uninstall.sh").read_text(encoding="utf-8")
    installer_script = (root / "customize.sh").read_text(encoding="utf-8")
    lifecycle_ok = (
        "COMPANION_VERSION_CODE=1580" in service_script
        and "ensure_companion_installed()" in service_script
        and "ensure_companion_installed &" in service_script
        and 'pm install --user 0 "$COMPANION_APK"' in service_script
        and 'pm uninstall "$COMPANION_PACKAGE"' in service_script
        and "companion_expected_hash" in service_script
        and "COMPANION_PACKAGE=io.github.bbbomb0.a2hhook" in uninstall_script
        and 'pm uninstall "$COMPANION_PACKAGE"' in uninstall_script
        and "COMPANION_CLEANUP=/data/adb/service.d/a2h_hook_companion_cleanup.sh" in uninstall_script
        and "schedule_companion_cleanup()" in uninstall_script
        and 'getprop sys.boot_completed' in uninstall_script
        and "[ \"$attempt\" -lt 90 ]" in uninstall_script
        and "[ -f /data/adb/modules_update/a2h_hook/module.prop ]" in uninstall_script
        and "/data/adb/service.d/a2h_hook_companion_cleanup.sh" in service_script
        and "/data/adb/service.d/a2h_hook_companion_cleanup.sh" in installer_script
        and "a2h_apply.pending" in uninstall_script
        and 'pm install --user 0 "$companion_apk"' in installer_script
        and 'pm uninstall io.github.bbbomb0.a2hhook' in installer_script
    )
    report.check(
        lifecycle_ok,
        "companion install/uninstall lifecycle",
        "version-aware boot fallback, early-boot deferred cleanup and complete fixed-package uninstall are present",
        "companion boot repair, version synchronization, or uninstall cleanup is incomplete",
    )

    try:
        apk_data = apk_path.read_bytes()
        ids = apk_signing_ids(apk_data)
        with zipfile.ZipFile(apk_path, "r") as apk:
            names = apk.namelist()
            required = {
                "AndroidManifest.xml", "classes.dex", "resources.arsc",
                "assets/index.html", "assets/coolapk.webp",
                "assets/donate-wechat-pay.webp", "assets/donate-wechat.webp",
                "assets/donate-alipay.webp", "assets/payment-wechat-pay.webp",
                "assets/payment-wechat-reward.webp", "assets/payment-alipay.webp",
            }
            apk_ok = (
                len(names) == len(set(names))
                and required <= set(names)
                and apk.testzip() is None
                and apk.read("assets/index.html") == (root / "webroot/index.html").read_bytes()
                and apk.read("assets/coolapk.webp") == (root / "webroot/coolapk.webp").read_bytes()
                and all(
                    apk.read(f"assets/{asset}") == (root / f"webroot/{asset}").read_bytes()
                    for asset in (
                        "donate-wechat-pay.webp", "donate-wechat.webp", "donate-alipay.webp",
                        "payment-wechat-pay.webp", "payment-wechat-reward.webp", "payment-alipay.webp",
                    )
                )
                and 0xF05368C0 in ids
            )
    except (OSError, ValueError, zipfile.BadZipFile, struct.error) as exc:
        report.add("FAIL", "companion signed APK", str(exc))
    else:
        report.check(
            apk_ok,
            "companion signed APK",
            "unique/CRC-valid APK, exact WebUI assets and APK Signature Scheme v3 block",
            f"entries={names!r} signing_ids={[hex(value) for value in sorted(ids)]!r}",
        )


def check_lock_protocol(root: Path, report: Report, use_adb: bool) -> None:
    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    webui = (root / "webroot/index.html").read_text(encoding="utf-8")
    lock_block = extract_function(applier, "process_starttime() {", "acquire_config_lock() {")
    required = (
        "process_starttime() {",
        "LOCK_OWNER_FORMAT=paired",
        "legacy_lock_owner_alive() {",
        'claim_token="$$ $claim_start"',
        'release_expected="$$ $release_start"',
    )
    report.check(
        bool(lock_block) and all(token in lock_block for token in required),
        "PID lock ownership contract",
        "pid+starttime ownership and legacy migration guards present",
        "dual-factor lock ownership contract is incomplete",
    )
    report.check(
        "commit-queue" in webui and "commit_config_and_queue()" in applier and
        'commit_config_abort()' in applier and 'commit_restore_one()' in applier,
        "WebUI config lock ownership contract",
        "embedded writer uses the same pid+starttime owner token",
        "WebUI config writer does not match the native lock protocol",
    )
    if not lock_block:
        return

    harness = lock_block + r'''
fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
base=__LOCK_TEST_BASE__/a2h_lock_regression_$$
paired="$base/paired"
legacy_live="$base/legacy_live"
legacy_reused="$base/legacy_reused"
creating="$base/creating"
cleanup() {
  kill "${legacy_child:-}" 2>/dev/null || true
  wait "${legacy_child:-}" 2>/dev/null || true
  rm -f "$paired/pid" "$legacy_live/pid" "$legacy_reused/pid" \
    "$creating/pid" "$creating/status" 2>/dev/null
  rmdir "$paired" "$legacy_live" "$legacy_reused" "$creating" "$base" 2>/dev/null || true
}
trap cleanup 0 1 2 15
mkdir "$base" || fail mkdir-base
claim_pid_lock "$paired" || fail claim-paired
self_start=$(process_starttime "$$") || fail self-start
[ "$(cat "$paired/pid")" = "$$ $self_start" ] || fail paired-format
lock_owner_alive "$paired" || fail paired-live
printf '%s %s\n' "$$" "$((self_start + 1))" > "$paired/pid"
lock_owner_alive "$paired" && fail reused-pid-accepted
release_pid_lock "$paired" && fail foreign-release-accepted
[ -f "$paired/pid" ] || fail foreign-release-deleted
printf '%s %s\n' "$$" "$self_start" > "$paired/pid"
release_pid_lock "$paired" || fail exact-release
sh -c 'sleep 8; :' a2h_apply &
legacy_child=$!
sleep 1
mkdir "$legacy_live" || fail mkdir-legacy-live
printf '%s\n' "$legacy_child" > "$legacy_live/pid"
lock_owner_alive "$legacy_live" || fail legacy-a2h-rejected
kill "$legacy_child" 2>/dev/null || true
wait "$legacy_child" 2>/dev/null || true
legacy_child=
stale_lock_recheck "$legacy_live" || fail legacy-dead-cleanup
mkdir "$legacy_reused" || fail mkdir-legacy-reused
printf '%s\n' "$$" > "$legacy_reused/pid"
lock_owner_alive "$legacy_reused" && fail legacy-unrelated-accepted
stale_lock_recheck "$legacy_reused" || fail legacy-reused-cleanup
mkdir "$creating" || fail mkdir-creating
printf '%s\n' starting > "$creating/status"
stale_lock_recheck "$creating" "$creating/status" || fail creating-cleanup
[ ! -d "$paired" ] && [ ! -d "$legacy_live" ] &&
  [ ! -d "$legacy_reused" ] && [ ! -d "$creating" ] || fail cleanup-left-lock
printf 'PASS pid-starttime lock regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "PID lock runtime regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = shutil.which("sh")
        if not shell or not Path("/proc/self/stat").is_file():
            report.gap("PID lock runtime regression", "rerun with --adb or on a Linux host")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__LOCK_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS pid-starttime lock regression" in output,
        "PID lock runtime regression",
        "paired/reused/legacy/creating/release cases passed",
        output.strip(),
    )


def check_service_singleton_lock(root: Path, report: Report, use_adb: bool) -> None:
    service = (root / "service.sh").read_text(encoding="utf-8")
    lock_block = extract_function(service, "process_starttime() {", "raw_config_signature() {")
    if not lock_block:
        report.add("FAIL", "service singleton lock regression", "service lock functions not found")
        return

    harness = lock_block + r'''
fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
log() { :; }
base=__SERVICE_LOCK_TEST_BASE__/a2h_service_lock_$$
SERVICE_LOCK_DIR="$base/lock"
SERVICE_LOCK_RECORD="$SERVICE_LOCK_DIR/owner"
service_lock_owner=
A2H_SERVICE_LOCK_SKIP_OWNER_CHECK=1
writer_pid=
cleanup() {
  [ -z "$writer_pid" ] || kill "$writer_pid" 2>/dev/null || true
  [ -z "$writer_pid" ] || wait "$writer_pid" 2>/dev/null || true
  rm -f "$SERVICE_LOCK_RECORD" 2>/dev/null || true
  rmdir "$SERVICE_LOCK_DIR" "$base" 2>/dev/null || true
}
trap cleanup 0 1 2 15
mkdir "$base" || fail mkdir-base

acquire_service_lock || fail initial-acquire
first_owner=$service_lock_owner
service_lock_still_owned || fail initial-ownership-check
printf '999999.1\n' > "$SERVICE_LOCK_RECORD"
service_lock_still_owned && fail foreign-owner-accepted
printf '%s\n' "$first_owner" > "$SERVICE_LOCK_RECORD"
service_lock_still_owned || fail restored-owner-rejected
acquire_service_lock
duplicate_rc=$?
[ "$duplicate_rc" -eq 2 ] || fail live-duplicate-accepted
service_lock_owner=$first_owner
release_service_lock || fail initial-release
[ ! -e "$SERVICE_LOCK_DIR" ] || fail initial-release-left-lock

mkdir "$SERVICE_LOCK_DIR" || fail mkdir-creating
self_start=$(process_starttime "$$") || fail self-start
(
  sleep 0.2
  printf '%s\n' "$$.$self_start" > "$SERVICE_LOCK_RECORD"
) &
writer_pid=$!
service_lock_owner=
acquire_service_lock
creating_rc=$?
wait "$writer_pid" 2>/dev/null || true
writer_pid=
[ "$creating_rc" -eq 2 ] || fail creating-owner-removed
[ -f "$SERVICE_LOCK_RECORD" ] || fail creating-record-missing
service_lock_owner="$$.$self_start"
release_service_lock || fail creating-release

mkdir "$SERVICE_LOCK_DIR" || fail mkdir-stale
printf '999999.1\n' > "$SERVICE_LOCK_RECORD"
service_lock_owner=
acquire_service_lock || fail stale-recovery
stale_owner=$service_lock_owner
[ "$stale_owner" = "$$.$self_start" ] || fail stale-owner-format
printf '%s\n' "$$.$((self_start + 1))" > "$SERVICE_LOCK_RECORD"
release_service_lock || true
[ -d "$SERVICE_LOCK_DIR" ] || fail foreign-release-deleted
printf '%s\n' "$stale_owner" > "$SERVICE_LOCK_RECORD"
service_lock_owner=$stale_owner
release_service_lock || fail stale-release
[ ! -e "$SERVICE_LOCK_DIR" ] || fail stale-release-left-lock
printf 'PASS service singleton lock regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "service singleton lock regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = locate_posix_shell()
        if not shell or not Path("/proc/self/stat").is_file():
            report.gap("service singleton lock regression", "rerun with --adb or on a Linux host")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__SERVICE_LOCK_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS service singleton lock regression" in output,
        "service singleton lock regression",
        "live duplicate, ownership fence, creating owner, stale owner, PID reuse and foreign release cases passed",
        output.strip() or f"harness exited {rc}",
    )


def check_config_hotupdate(root: Path, report: Report, use_adb: bool) -> None:
    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    config_block = extract_function(applier, "commit_tmp() {", "generate_runtime_config() {")
    if not config_block:
        report.add("FAIL", "configuration hot-update runtime regression", "normalizer block not found")
        return
    config_block = config_block.replace(
        'mkdir -p "$CFG_DIR" /data/local/tmp', 'mkdir -p "$CFG_DIR" "$base/runtime"'
    )

    harness = config_block + r'''
set -eu
base=__CONFIG_TEST_BASE__/a2h_config_hotupdate_$$
CFG_DIR="$base/config"
CFG_STATE="$CFG_DIR/state"
CFG_PKGS="$CFG_DIR/packages.txt"
CFG_STATES="$CFG_DIR/package_states"
CFG_GENERATION="$CFG_DIR/config_generation"
CFG_BASELINE="$CFG_DIR/.package_baseline"
TEST_LOG="$base/test.log"
CONFIG_RECOVERED=0
A2H_QUIET_PREPARE=1

fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
log() { printf '%s\n' "$*" >> "$TEST_LOG"; }
lock_owned_by_self() { return 0; }
cleanup() { rm -rf "$base"; }
trap cleanup 0 1 2 15
mkdir -p "$CFG_DIR"

write_packages() {
  package8=$1
  cat > "$CFG_PKGS" <<EOF
com.kugou.android
com.tencent.qqmusic
com.netease.cloudmusic
cn.kuwo.player
com.miui.player
com.luna.music

$package8


EOF
}

write_states() {
  state1=$1
  state8=$2
  printf '%s\n' "$state1" 1 1 1 1 1 0 "$state8" 0 0 > "$CFG_STATES"
}

reset_case() {
  rm -rf "$CFG_DIR"
  mkdir -p "$CFG_DIR"
  : > "$TEST_LOG"
  printf 'disabled\n' > "$CFG_STATE"
  write_packages com.example.old
  write_states 1 0
  printf '100\n' > "$CFG_GENERATION"
  normalize_package_config || fail baseline-normalize
  [ "$(sed -n '8p' "$CFG_STATES")" = 0 ] || fail baseline-state8
  [ -f "$CFG_BASELINE" ] || fail baseline-missing
}

reset_case
write_packages com.example.new
normalize_package_config || fail external-normalize
[ "$(sed -n '8p' "$CFG_STATES")" = 1 ] || fail external-state8-not-enabled
[ "$(sed -n '8p' "$CFG_BASELINE")" = com.example.new ] || fail external-baseline
first_states=$(cksum < "$CFG_STATES")
first_baseline=$(cksum < "$CFG_BASELINE")
normalize_package_config || fail external-idempotent-normalize
[ "$(cksum < "$CFG_STATES")" = "$first_states" ] || fail external-idempotent-states
[ "$(cksum < "$CFG_BASELINE")" = "$first_baseline" ] || fail external-idempotent-baseline

write_packages ''
normalize_package_config || fail clear-normalize
[ "$(sed -n '8p' "$CFG_STATES")" = 0 ] || fail clear-state8-not-disabled

reset_case
write_packages com.example.drift
write_states 0 0
normalize_package_config || fail drift-normalize
[ "$(sed -n '1p' "$CFG_STATES")" = 0 ] || fail drift-unmodified-state-not-preserved
[ "$(sed -n '8p' "$CFG_STATES")" = 1 ] || fail drift-state8-not-enabled
grep -q 'state_drift=1' "$TEST_LOG" || fail drift-diagnostic-missing

reset_case
write_packages com.example.grouped
write_states 1 0
printf '101\n' > "$CFG_GENERATION"
normalize_package_config || fail grouped-normalize
[ "$(sed -n '8p' "$CFG_STATES")" = 0 ] || fail grouped-explicit-off-overridden
[ "$(sed -n '12s/^generation=//p' "$CFG_BASELINE")" = 101 ] || fail grouped-generation

reset_case
rm -f "$CFG_BASELINE"
write_packages com.example.recovered
normalize_package_config || fail missing-baseline-normalize
[ "$(sed -n '8p' "$CFG_STATES")" = 0 ] || fail missing-baseline-guessed-state
grep -q 'package baseline rebuilt reason=missing' "$TEST_LOG" || fail missing-baseline-diagnostic

printf 'PASS configuration hot-update runtime regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "configuration hot-update runtime regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = shutil.which("sh")
        if not shell:
            report.gap("configuration hot-update runtime regression", "rerun with --adb or on a POSIX host")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__CONFIG_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS configuration hot-update runtime regression" in output,
        "configuration hot-update runtime regression",
        "slot 8 edit, state drift, grouped commit, clear, baseline recovery, and idempotence passed",
        output.strip(),
    )


def check_control_transaction(root: Path, report: Report, use_adb: bool) -> None:
    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    atomic_block = extract_function(applier, "commit_tmp() {", "write_default_packages() {")
    controls_block = extract_function(applier, "set_controls() {", "find_hal_pid() {")
    if not atomic_block or not controls_block:
        report.add("FAIL", "mode/policy transaction regression", "production control functions not found")
        return

    harness = atomic_block + controls_block + r'''
set -eu
base=__CONTROL_TEST_BASE__/a2h_controls_$$
CFG_DIR="$base/config"
CFG_STATE="$CFG_DIR/state"
CFG_GAME_POLICY="$CFG_DIR/game_auto_pause"
CFG_NORMALIZED_FINGERPRINT="$CFG_DIR/.normalized_fingerprint"
lock_calls=0
fail_policy_commit=0

fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
log() { :; }
acquire_config_lock() { lock_calls=$((lock_calls + 1)); return 0; }
release_config_lock() { return 0; }
normalized_fingerprint_matches() { return 1; }
compute_table_fingerprint() { return 1; }
record_normalized_fingerprint() { return 0; }
commit_tmp() {
  src=$1
  dest=$2
  if [ "$fail_policy_commit" = 1 ] && [ "$dest" = "$CFG_GAME_POLICY" ]; then
    fail_policy_commit=0
    rm -f "$src"
    return 71
  fi
  chmod "${3:-0644}" "$src" 2>/dev/null || true
  mv -f "$src" "$dest"
}
cleanup() { rm -rf "$base"; }
trap cleanup 0 1 2 15
mkdir -p "$CFG_DIR"
printf 'disabled\n' > "$CFG_STATE"
printf 'enabled\n' > "$CFG_GAME_POLICY"

if set_controls enabled invalid; then fail invalid-policy-accepted; fi
[ "$lock_calls" = 0 ] || fail invalid-policy-acquired-lock
[ "$(cat "$CFG_STATE")" = disabled ] || fail invalid-policy-changed-mode
[ "$(cat "$CFG_GAME_POLICY")" = enabled ] || fail invalid-policy-changed-policy

fail_policy_commit=1
if set_controls enabled disabled; then fail injected-policy-write-succeeded; fi
[ "$(cat "$CFG_STATE")" = disabled ] || fail mode-not-rolled-back
[ "$(cat "$CFG_GAME_POLICY")" = enabled ] || fail policy-not-rolled-back

set_controls enabled disabled || fail valid-controls-failed
[ "$(cat "$CFG_STATE")" = enabled ] || fail valid-mode-not-committed
[ "$(cat "$CFG_GAME_POLICY")" = disabled ] || fail valid-policy-not-committed

rm -f "$CFG_STATE" "$CFG_GAME_POLICY"
fail_policy_commit=1
if set_controls enabled disabled; then fail missing-file-fault-succeeded; fi
[ ! -e "$CFG_STATE" ] || fail absent-mode-not-restored
[ ! -e "$CFG_GAME_POLICY" ] || fail absent-policy-not-restored
printf 'PASS mode/policy transaction regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "mode/policy transaction regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = locate_posix_shell()
        if not shell:
            report.gap("mode/policy transaction regression", "POSIX sh is required")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__CONTROL_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS mode/policy transaction regression" in output,
        "mode/policy transaction regression",
        "validation-before-lock, two-file rollback, success, and absent-file rollback passed",
        output.strip() or f"harness exited {rc}",
    )


def check_policy_refresh_transaction(root: Path, report: Report, use_adb: bool) -> None:
    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    atomic_block = extract_function(applier, "commit_tmp() {", "write_default_packages() {")
    refresh_block = extract_function(applier, "refresh_policy_state() {", "mark_pending() {")
    if not atomic_block or not refresh_block:
        report.add("FAIL", "game policy immediate refresh regression", "production refresh functions not found")
        return

    harness = atomic_block + refresh_block + r'''
set -eu
base=__POLICY_REFRESH_TEST_BASE__/a2h_policy_refresh_$$
CFG_DIR="$base/config"
APPLIED_GAME_POLICY="$CFG_DIR/applied_game_auto_pause"
APPLIED_REVISION="$CFG_DIR/applied_revision"
APPLIED_SNAPSHOT="$CFG_DIR/applied_snapshot"
MAIN_LOG="$base/main.log"
ACTION_LOG="$base/action.log"
TRIGGER="$base/trigger"
TRIGGER_COUNT="$base/trigger.count"
TRIGGER_FAIL="$base/trigger.fail"
CURRENT_MODE=disabled
CURRENT_GAME_AUTO_PAUSE=enabled
CURRENT_SNAPSHOT=snapshot-1
CURRENT_RAW_SIGNATURE=raw-1
CURRENT_REVISION=1
ACTIVE_COUNT=6
A2H_APPLY_ATTEMPTS=2
apply_calls=0
release_calls=0

fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
log() { printf '%s\n' "$*" >> "$ACTION_LOG"; }
acquire_apply_lock() { return 0; }
release_apply_lock() { release_calls=$((release_calls + 1)); return 0; }
prepare_config() { return 0; }
raw_config_matches() { return 0; }
apply_current() { apply_calls=$((apply_calls + 1)); return 0; }
sleep() { :; }
cleanup() { rm -rf "$base"; }
trap cleanup 0 1 2 15
mkdir -p "$CFG_DIR"
: > "$MAIN_LOG"
: > "$ACTION_LOG"
printf '0\n' > "$TRIGGER_COUNT"
printf '#!%s\n' "$(command -v sh)" > "$TRIGGER"
cat >> "$TRIGGER" <<'EOF'
count=$(cat "$A2H_TEST_TRIGGER_COUNT")
printf '%s\n' "$((count + 1))" > "$A2H_TEST_TRIGGER_COUNT"
[ ! -e "$A2H_TEST_TRIGGER_FAIL" ]
EOF
chmod 0755 "$TRIGGER"
export A2H_TEST_TRIGGER_COUNT="$TRIGGER_COUNT"
export A2H_TEST_TRIGGER_FAIL="$TRIGGER_FAIL"

# First successful apply initializes metadata without a synthetic stream.
apply_stable initial >/dev/null || fail initial-apply
[ "$(cat "$TRIGGER_COUNT")" = 0 ] || fail initial-triggered
[ "$(cat "$APPLIED_GAME_POLICY")" = enabled ] || fail initial-policy-marker
[ "$(cat "$APPLIED_SNAPSHOT")" = snapshot-1 ] || fail initial-snapshot

# A package/mode-only snapshot update must not trigger policy recomputation.
CURRENT_SNAPSHOT=snapshot-2
CURRENT_REVISION=2
apply_stable same-policy >/dev/null || fail same-policy-apply
[ "$(cat "$TRIGGER_COUNT")" = 0 ] || fail same-policy-triggered

# Both policy directions trigger exactly once after a successful apply.
CURRENT_GAME_AUTO_PAUSE=disabled
CURRENT_SNAPSHOT=snapshot-3
CURRENT_REVISION=3
apply_stable disable-policy >/dev/null || fail disable-policy-apply
[ "$(cat "$TRIGGER_COUNT")" = 1 ] || fail disable-trigger-count
[ "$(cat "$APPLIED_GAME_POLICY")" = disabled ] || fail disable-policy-marker

CURRENT_GAME_AUTO_PAUSE=enabled
CURRENT_SNAPSHOT=snapshot-4
CURRENT_REVISION=4
apply_stable enable-policy >/dev/null || fail enable-policy-apply
[ "$(cat "$TRIGGER_COUNT")" = 2 ] || fail enable-trigger-count
[ "$(cat "$APPLIED_GAME_POLICY")" = enabled ] || fail enable-policy-marker

# A failed trigger retries but cannot commit applied metadata.
: > "$TRIGGER_FAIL"
CURRENT_GAME_AUTO_PAUSE=disabled
CURRENT_SNAPSHOT=snapshot-5
CURRENT_REVISION=5
if apply_stable failing-trigger >/dev/null; then fail failing-trigger-accepted; fi
[ "$(cat "$TRIGGER_COUNT")" = 4 ] || fail failing-trigger-retry-count
[ "$(cat "$APPLIED_GAME_POLICY")" = enabled ] || fail failing-trigger-policy-committed
[ "$(cat "$APPLIED_REVISION")" = 4 ] || fail failing-trigger-revision-committed
[ "$(cat "$APPLIED_SNAPSHOT")" = snapshot-4 ] || fail failing-trigger-snapshot-committed

rm -f "$TRIGGER_FAIL"
apply_stable recovered-trigger >/dev/null || fail recovered-trigger-apply
[ "$(cat "$TRIGGER_COUNT")" = 5 ] || fail recovered-trigger-count
[ "$(cat "$APPLIED_GAME_POLICY")" = disabled ] || fail recovered-policy-marker
[ "$(cat "$APPLIED_REVISION")" = 5 ] || fail recovered-revision
[ "$(cat "$APPLIED_SNAPSHOT")" = snapshot-5 ] || fail recovered-snapshot
[ "$release_calls" = 6 ] || fail apply-lock-release-count-$release_calls
printf 'PASS game policy immediate refresh regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "game policy immediate refresh regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = locate_posix_shell()
        if not shell:
            report.gap("game policy immediate refresh regression", "POSIX sh is required")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__POLICY_REFRESH_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS game policy immediate refresh regression" in output,
        "game policy immediate refresh regression",
        "initial/same-policy skip, bidirectional one-shot refresh, failure retry, and metadata commit ordering passed",
        output.strip() or f"harness exited {rc}",
    )


def check_revision_metadata_repair(root: Path, report: Report, use_adb: bool) -> None:
    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    atomic_block = extract_function(applier, "commit_tmp() {", "write_default_packages() {")
    revision_block = extract_function(applier, "record_revision() {", "prepare_config_unlocked() {")
    if not atomic_block or not revision_block:
        report.add("FAIL", "revision metadata repair regression", "production atomic/revision functions not found")
        return

    harness = atomic_block + revision_block + r'''
set -eu
base=__METADATA_TEST_BASE__/a2h_revision_metadata_$$
CFG_DIR="$base/config"
CFG_SNAPSHOT="$CFG_DIR/config_snapshot"
CFG_REVISION="$CFG_DIR/revision"
TEST_LOG="$base/test.log"
CURRENT_SNAPSHOT=314159:26
CURRENT_REVISION=0
ACTIVE_COUNT=6
CURRENT_MODE=disabled
CURRENT_GAME_AUTO_PAUSE=enabled
metadata_mv_calls=0
metadata_fail_snapshot=0
METADATA_MV_LOG="$base/mv.log"

fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
log() { printf '%s\n' "$*" >> "$TEST_LOG"; }
mv() {
  metadata_mv_calls=$((metadata_mv_calls + 1))
  case "${3:-}" in
    "$CFG_REVISION") printf 'revision\n' >> "$METADATA_MV_LOG" ;;
    "$CFG_SNAPSHOT")
      printf 'snapshot\n' >> "$METADATA_MV_LOG"
      [ "$metadata_fail_snapshot" != 1 ] || return 73
      ;;
  esac
  command mv "$@"
}
cleanup() { rm -rf "$base"; }
trap cleanup 0 1 2 15
mkdir -p "$CFG_DIR"
: > "$TEST_LOG"

run_revision_case() {
  case_label=$1
  case_input=$2
  case_expected=$3
  rm -rf "$CFG_DIR"
  mkdir -p "$CFG_DIR"
  printf '%s\n' "$CURRENT_SNAPSHOT" > "$CFG_SNAPSHOT"
  case "$case_input" in
    missing) rm -f "$CFG_REVISION" ;;
    invalid) printf 'not-a-number\n' > "$CFG_REVISION" ;;
    # A trailing CR without LF remains observable under MSYS command
    # substitution while exercising the same Android normalization path.
    cr) printf '7\r' > "$CFG_REVISION" ;;
    *) fail "$case_label-unknown-input" ;;
  esac

  snapshot_before=$(cksum < "$CFG_SNAPSHOT")
  metadata_mv_calls=0
  CURRENT_REVISION=999
  record_revision || fail "$case_label-first-repair"
  [ "$CURRENT_REVISION" = "$case_expected" ] || fail "$case_label-current-first"
  printf '%s\n' "$case_expected" > "$base/expected-revision"
  cmp -s "$base/expected-revision" "$CFG_REVISION" || fail "$case_label-canonical-file"
  [ "$metadata_mv_calls" = 1 ] || fail "$case_label-not-atomic-moves-$metadata_mv_calls"
  [ "$(cksum < "$CFG_SNAPSHOT")" = "$snapshot_before" ] || fail "$case_label-snapshot-rewritten"

  revision_after_first=$(cksum < "$CFG_REVISION")
  metadata_mv_calls=0
  CURRENT_REVISION=999
  record_revision || fail "$case_label-second-repair"
  [ "$CURRENT_REVISION" = "$case_expected" ] || fail "$case_label-current-second"
  [ "$(cksum < "$CFG_REVISION")" = "$revision_after_first" ] || fail "$case_label-second-content"
  [ "$metadata_mv_calls" = 0 ] || fail "$case_label-second-not-idempotent"
  [ "$(cksum < "$CFG_SNAPSHOT")" = "$snapshot_before" ] || fail "$case_label-second-snapshot"
}

run_revision_case missing missing 1
run_revision_case nonnumeric invalid 1
run_revision_case carriage-return cr 7

# A changed snapshot is committed last. If that second atomic move fails, the
# old snapshot remains visibly stale so a later prepare cannot accept an old
# legal revision as current metadata.
rm -rf "$CFG_DIR"
mkdir -p "$CFG_DIR"
CURRENT_SNAPSHOT=changed:200
printf 'old:100\n' > "$CFG_SNAPSHOT"
printf '7\n' > "$CFG_REVISION"
: > "$METADATA_MV_LOG"
metadata_mv_calls=0
metadata_fail_snapshot=1
if record_revision; then
  fail changed-snapshot-failure-was-swallowed
fi
metadata_fail_snapshot=0
[ "$metadata_mv_calls" = 2 ] || fail changed-first-move-count-$metadata_mv_calls
printf 'revision\nsnapshot\n' > "$base/expected-mv-order"
cmp -s "$base/expected-mv-order" "$METADATA_MV_LOG" || fail changed-unsafe-mv-order
[ "$(cat "$CFG_SNAPSHOT")" = old:100 ] || fail changed-failed-snapshot-visible-as-current
first_failed_revision=$(cat "$CFG_REVISION" 2>/dev/null || true)
case "$first_failed_revision" in
  ''|*[!0-9]*) fail changed-first-revision-invalid ;;
esac
[ "$first_failed_revision" -gt 7 ] || fail changed-first-revision-not-advanced
[ ! "$(cat "$CFG_SNAPSHOT")" = "$CURRENT_SNAPSHOT" ] ||
  [ ! "$first_failed_revision" = 7 ] || fail changed-old-revision-accepted

: > "$METADATA_MV_LOG"
metadata_mv_calls=0
record_revision || fail changed-retry
[ "$(cat "$CFG_SNAPSHOT")" = "$CURRENT_SNAPSHOT" ] || fail changed-retry-snapshot
recovered_revision=$(cat "$CFG_REVISION")
case "$recovered_revision" in
  ''|*[!0-9]*) fail changed-retry-revision-invalid ;;
esac
[ "$recovered_revision" -gt 7 ] || fail changed-retry-revision-not-advanced
[ "$CURRENT_REVISION" = "$recovered_revision" ] || fail changed-retry-current-revision

recovered_revision_sum=$(cksum < "$CFG_REVISION")
recovered_snapshot_sum=$(cksum < "$CFG_SNAPSHOT")
: > "$METADATA_MV_LOG"
metadata_mv_calls=0
record_revision || fail changed-idempotent
[ "$metadata_mv_calls" = 0 ] || fail changed-idempotent-moved-$metadata_mv_calls
[ "$(cksum < "$CFG_REVISION")" = "$recovered_revision_sum" ] || fail changed-idempotent-revision
[ "$(cksum < "$CFG_SNAPSHOT")" = "$recovered_snapshot_sum" ] || fail changed-idempotent-snapshot
printf 'PASS revision metadata repair regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "revision metadata repair regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = locate_posix_shell()
        if not shell:
            report.gap("revision metadata repair regression", "POSIX sh is required")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__METADATA_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS revision metadata repair regression" in output,
        "revision metadata repair regression",
        "matching snapshots canonically repair missing, invalid, and CR revisions exactly once",
        output.strip() or f"harness exited {rc}",
    )


def check_last_pid_write_failure(root: Path, report: Report, use_adb: bool) -> None:
    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    atomic_block = extract_function(applier, "commit_tmp() {", "write_default_packages() {")
    apply_block = extract_function(applier, "apply_current() {", "mark_pending() {")
    if not atomic_block or not apply_block:
        report.add("FAIL", "last-pid write failure regression", "production atomic/apply functions not found")
        return

    harness = atomic_block + apply_block + r'''
set -eu
base=__LAST_PID_TEST_BASE__/a2h_last_pid_failure_$$
CFG_DIR="$base/config"
LAST_PID_FILE="$CFG_DIR/last_pid"
PATCHER="$base/patcher"
PATCHER_CALL_LOG="$base/patcher.calls"
MAIN_LOG="$base/main.log"
TEST_LOG="$base/test.log"
TMP_PKGS="$base/packages.txt"
APPLIED_GAME_POLICY="$CFG_DIR/applied_game_auto_pause"
APPLIED_REVISION="$CFG_DIR/applied_revision"
APPLIED_SNAPSHOT="$CFG_DIR/applied_snapshot"
CURRENT_RAW_SIGNATURE=raw-1
CURRENT_MODE=enabled
CURRENT_GAME_AUTO_PAUSE=enabled
CURRENT_REVISION=17
CURRENT_SNAPSHOT=271828:18
ACTIVE_COUNT=10
HAL_PID=
HAL_BASE=
last_pid_failure_calls=0
prepare_calls=0
release_calls=0
A2H_APPLY_ATTEMPTS=4

fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
log() { printf '%s\n' "$*" >> "$TEST_LOG"; }
log_slots() { :; }
resolve_hal() { HAL_PID=4242; HAL_BASE=; return 0; }
patcher_supports_packages() { return 0; }
patcher_apply_final_verified() { return 1; }
acquire_apply_lock() { return 0; }
release_apply_lock() { release_calls=$((release_calls + 1)); return 0; }
prepare_config() { prepare_calls=$((prepare_calls + 1)); return 0; }
raw_config_matches() { return 0; }
mv() {
  if [ "$#" = 3 ] && [ "$1" = -f ] && [ "$3" = "$LAST_PID_FILE" ]; then
    last_pid_failure_calls=$((last_pid_failure_calls + 1))
    return 73
  fi
  command mv "$@"
}
cleanup() { rm -rf "$base"; }
trap cleanup 0 1 2 15
mkdir -p "$CFG_DIR"
: > "$PATCHER_CALL_LOG"
: > "$MAIN_LOG"
: > "$TEST_LOG"
: > "$TMP_PKGS"
printf 'old-pid\n' > "$LAST_PID_FILE"
export PATCHER_CALL_LOG
cat > "$PATCHER" <<'EOF'
#!/bin/sh
printf 'call\n' >> "$PATCHER_CALL_LOG"
exit 0
EOF
chmod 0755 "$PATCHER"

stable_result=0
if apply_stable metadata-failure; then
  fail last-pid-write-failure-was-swallowed
else
  stable_result=$?
fi
[ "$stable_result" = 74 ] || fail unexpected-stable-result-$stable_result
[ "$last_pid_failure_calls" = 1 ] || fail last-pid-failure-not-injected
[ "$(cat "$LAST_PID_FILE")" = old-pid ] || fail last-pid-destination-corrupted
[ "$(wc -l < "$PATCHER_CALL_LOG" | tr -d ' ')" = 2 ] || fail patcher-precondition
[ "$prepare_calls" = 1 ] || fail metadata-failure-retried-prepare-$prepare_calls
[ "$release_calls" = 1 ] || fail apply-lock-release-count-$release_calls
[ ! -e "$APPLIED_SNAPSHOT" ] && [ ! -e "$APPLIED_REVISION" ] || fail applied-metadata-recorded
! grep -q 'apply verified' "$TEST_LOG" || fail success-log-after-metadata-failure
printf 'PASS last-pid write failure regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "last-pid write failure regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = locate_posix_shell()
        if not shell:
            report.gap("last-pid write failure regression", "POSIX sh is required")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__LAST_PID_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS last-pid write failure regression" in output,
        "last-pid write failure regression",
        "last_pid failure returns 74 after one apply/check pair without retry",
        output.strip() or f"harness exited {rc}",
    )


def check_boot_last_pid_failure(root: Path, report: Report, use_adb: bool) -> None:
    service = (root / "service.sh").read_text(encoding="utf-8")
    boot_block = extract_function(
        service, "boot_ok=0\nboot_try=1\n", "\nconfig_inotify_pid="
    )
    if not boot_block:
        report.add("FAIL", "boot last-pid failure regression", "production boot retry block not found")
        return

    harness = r'''
set -eu
base=__BOOT_TEST_BASE__/a2h_boot_last_pid_$$
APPLY_CALL_LOG="$base/apply.calls"
SLEEP_CALL_LOG="$base/sleep.calls"
TEST_LOG="$base/test.log"
CFG_STATE="$base/state"
APPLIED_SNAPSHOT="$base/applied_snapshot"

fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
log() { printf '%s\n' "$*" >> "$TEST_LOG"; }
apply_once() { printf '%s\n' "$1" >> "$APPLY_CALL_LOG"; return 74; }
sleep() { printf '%s\n' "$1" >> "$SLEEP_CALL_LOG"; return 0; }
post_boot_notification() { :; }
cleanup() { rm -rf "$base"; }
trap cleanup 0 1 2 15
mkdir -p "$base"
: > "$APPLY_CALL_LOG"
: > "$SLEEP_CALL_LOG"
: > "$TEST_LOG"
'''+ boot_block + r'''
printf 'watcher-reached\n' > "$base/watcher.reached"
[ "$(wc -l < "$APPLY_CALL_LOG" | tr -d ' ')" = 1 ] || fail boot-metadata-failure-retried
[ ! -s "$SLEEP_CALL_LOG" ] || fail boot-metadata-failure-slept
[ "$boot_ok" = 0 ] || fail boot-metadata-failure-marked-success
[ "$runtime_status" = failure ] || fail boot-metadata-runtime-status
[ -f "$base/watcher.reached" ] || fail boot-did-not-reach-watcher
printf 'PASS boot last-pid failure regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "boot last-pid failure regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = locate_posix_shell()
        if not shell:
            report.gap("boot last-pid failure regression", "POSIX sh is required")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__BOOT_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS boot last-pid failure regression" in output,
        "boot last-pid failure regression",
        "boot treats rc=74 as terminal for the attempt and immediately reaches the watcher",
        output.strip() or f"harness exited {rc}",
    )


def check_postfs_runtime_cleanup(root: Path, report: Report, use_adb: bool) -> None:
    postfs = (root / "post-fs-data.sh").read_text(encoding="utf-8")
    harness = r'''
set -eu
base=__POSTFS_TEST_BASE__/a2h_postfs_cleanup_$$
runtime_test_dir="$base/runtime"
audio_runtime_test_dir="$base/audio-runtime"
service_lock_test_dir="$base/service-lock"
BOOT_STATE=0
live_pid=
A2H_POSTFS_LOCAL_TMP="$runtime_test_dir"
A2H_POSTFS_AUDIO_RUNTIME="$audio_runtime_test_dir"
A2H_POSTFS_SERVICE_LOCK="$service_lock_test_dir"
export A2H_POSTFS_LOCAL_TMP A2H_POSTFS_AUDIO_RUNTIME A2H_POSTFS_SERVICE_LOCK

fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
getprop() {
  case "${1:-}" in
    sys.boot_completed) printf '%s\n' "$BOOT_STATE" ;;
    *) printf '\n' ;;
  esac
}
resetprop() { :; }
setprop() { :; }
cleanup() {
  [ -z "$live_pid" ] || kill "$live_pid" 2>/dev/null || true
  [ -z "$live_pid" ] || wait "$live_pid" 2>/dev/null || true
  rm -rf "$base"
}
trap cleanup 0 1 2 15

seed_runtime() {
  rm -rf "$runtime_test_dir" "$audio_runtime_test_dir" "$service_lock_test_dir"
  mkdir -p \
    "$runtime_test_dir/a2h_apply.lock" \
    "$runtime_test_dir/a2h_apply.worker" \
    "$runtime_test_dir/a2h_config.lock"
  mkdir -p "$audio_runtime_test_dir/cooldown"
  mkdir -p "$service_lock_test_dir"
  printf 'trigger\n' > "$audio_runtime_test_dir/a2h_trigger"
  printf 'pending\n' > "$runtime_test_dir/a2h_apply.pending"
  printf 'pending-tmp\n' > "$runtime_test_dir/a2h_apply.pending.tmp.123"
  printf 'state\n' > "$runtime_test_dir/a2h_state"
  printf 'state-tmp\n' > "$runtime_test_dir/.a2h_state.123"
  printf 'packages\n' > "$runtime_test_dir/a2h_packages.txt"
  printf 'packages-tmp\n' > "$runtime_test_dir/.a2h_packages.123"
  printf 'event\n' > "$runtime_test_dir/a2h_config.changed"
  printf 'wake\n' > "$runtime_test_dir/a2h_config.wake"
  printf 'owner\n' > "$runtime_test_dir/a2h_apply.worker/pid"
  printf 'keep\n' > "$runtime_test_dir/unrelated.keep"
}

seed_runtime
''' + postfs + r'''
[ -f "$runtime_test_dir/unrelated.keep" ] || fail early-unrelated-removed
[ "$(find "$runtime_test_dir" -mindepth 1 ! -name unrelated.keep | wc -l | tr -d ' ')" = 0 ] ||
  fail early-target-remains
[ ! -e "$audio_runtime_test_dir" ] || fail early-audio-runtime-remains
[ ! -e "$service_lock_test_dir" ] || fail early-service-lock-remains

# Temporary-root activation can run post-fs-data twice in one boot. A live
# service owner must preserve its complete runtime epoch.
seed_runtime
cat > "$base/service.sh" <<'EOF'
#!/bin/sh
sleep 20
EOF
chmod 0755 "$base/service.sh"
sh "$base/service.sh" &
live_pid=$!
live_start=$(process_starttime "$live_pid") || fail live-starttime
printf '%s\n' "$live_pid.$live_start" > "$service_lock_test_dir/owner"
before=$(find "$runtime_test_dir" -mindepth 1 -print | sort | cksum)
before_audio=$(find "$audio_runtime_test_dir" -mindepth 1 -print | sort | cksum)
before_service=$(find "$service_lock_test_dir" -mindepth 1 -print | sort | cksum)
''' + postfs + r'''
after=$(find "$runtime_test_dir" -mindepth 1 -print | sort | cksum)
after_audio=$(find "$audio_runtime_test_dir" -mindepth 1 -print | sort | cksum)
after_service=$(find "$service_lock_test_dir" -mindepth 1 -print | sort | cksum)
[ "$after" = "$before" ] || fail live-owner-mutated-runtime
[ "$after_audio" = "$before_audio" ] || fail live-owner-mutated-audio-runtime
[ "$after_service" = "$before_service" ] || fail live-owner-mutated-service-lock
kill "$live_pid" 2>/dev/null || true
wait "$live_pid" 2>/dev/null || true
live_pid=

seed_runtime
before=$(find "$runtime_test_dir" -mindepth 1 -print | sort | cksum)
before_audio=$(find "$audio_runtime_test_dir" -mindepth 1 -print | sort | cksum)
before_service=$(find "$service_lock_test_dir" -mindepth 1 -print | sort | cksum)
BOOT_STATE=1
''' + postfs + r'''
after=$(find "$runtime_test_dir" -mindepth 1 -print | sort | cksum)
after_audio=$(find "$audio_runtime_test_dir" -mindepth 1 -print | sort | cksum)
after_service=$(find "$service_lock_test_dir" -mindepth 1 -print | sort | cksum)
[ "$after" = "$before" ] || fail completed-boot-mutated-runtime
[ "$after_audio" = "$before_audio" ] || fail completed-boot-mutated-audio-runtime
[ "$after_service" = "$before_service" ] || fail completed-boot-mutated-service-lock
printf 'PASS post-fs-data runtime cleanup regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "post-fs-data runtime cleanup regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = locate_posix_shell()
        if not shell:
            report.gap("post-fs-data runtime cleanup regression", "POSIX sh is required")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__POSTFS_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS post-fs-data runtime cleanup regression" in output,
        "post-fs-data runtime cleanup regression",
        "stable stale epochs are removed; live temporary-root and completed-boot epochs are preserved",
        output.strip() or f"harness exited {rc}",
    )


def check_config_wake_fifo(root: Path, report: Report, use_adb: bool) -> None:
    service = (root / "service.sh").read_text(encoding="utf-8")
    wake_functions = extract_function(
        service, "prepare_config_wake_fifo() {", "\nstart_audio_watcher() {",
    )
    if not wake_functions:
        report.add("FAIL", "config wake FIFO regression", "production wake functions not found")
        return

    harness = r'''
set -eu
base=__WAKE_TEST_BASE__/a2h_config_wake_$$
CONFIG_WAKE_FIFO="$base/config.wake"
config_inotify_enabled=1
config_wake_ready=0
pending_raw_signature=
config_poll_grace_ticks=0
watch_tick_seconds=1
watch_health_ticks=2

fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
mkdir -p "$base"
''' + wake_functions + r'''
cleanup() {
  stop_config_wake_fifo || true
  rm -rf "$base"
}
trap cleanup 0 1 2 15

prepare_config_wake_fifo || fail prepare
[ -p "$CONFIG_WAKE_FIFO" ] || fail not-fifo
[ "$config_wake_ready" = 1 ] || fail not-ready

( sleep 1; printf 'config\n' > "$CONFIG_WAKE_FIFO" ) &
writer=$!
wait_for_watch_tick
wait "$writer"
[ "$watch_elapsed_ticks" = 1 ] || fail event-elapsed
[ "$watch_wake_event" = config ] || fail event-payload

wait_for_watch_tick
[ "$watch_elapsed_ticks" = 2 ] || fail timeout-elapsed
stop_config_wake_fifo
[ ! -e "$CONFIG_WAKE_FIFO" ] || fail cleanup
printf 'PASS config wake FIFO regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "config wake FIFO regression", "adb requested but not found")
            return
        command = [adb, "shell", "su", "-c", "sh -s"]
        test_base = "/data/local/tmp"
    else:
        # The production helper uses `read -t`; dash accepts `read` but not this
        # timeout option, so the host harness must run under bash explicitly.
        shell = locate_bash_shell()
        if not shell:
            report.gap("config wake FIFO regression", "Bash with read -t is required")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__WAKE_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS config wake FIFO regression" in output,
        "config wake FIFO regression",
        "event wake, health timeout, FIFO type and cleanup passed",
        output.strip() or f"harness exited {rc}",
    )


def check_audio_uid_watcher(root: Path, report: Report, use_adb: bool) -> None:
    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "audio UID watcher semantics", "adb requested but not found")
            return
        harness = (root / "tests/audio_uid_watcher_harness.sh").resolve()
        watcher = (root / "bin/a2h_audio_watch").resolve()
        remote_harness = "/data/local/tmp/a2h_uid_watcher_harness.sh"
        remote_watcher = "/data/local/tmp/a2h_uid_watcher_target.sh"
        outputs: list[str] = []
        rc = 1
        try:
            for local, remote in ((harness, remote_harness), (watcher, remote_watcher)):
                pushed = subprocess.run(
                    [adb, "push", str(local), remote], cwd=root,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
                )
                outputs.append(pushed.stdout.decode("utf-8", errors="replace"))
                if pushed.returncode != 0:
                    raise OSError(f"adb push failed for {local.name}")
            completed = subprocess.run(
                [adb, "shell", "su", "-c",
                 f"A2H_TEST_BASE=/data/local/tmp sh {remote_harness} {remote_watcher}"],
                cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
            )
            rc = completed.returncode
            outputs.append(completed.stdout.decode("utf-8", errors="replace"))
        except OSError as exc:
            outputs.append(str(exc))
        finally:
            subprocess.run(
                [adb, "shell", "su", "-c", f"rm -f {remote_harness} {remote_watcher}"],
                cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
            )
        output = "".join(outputs)
        report.check(
            rc == 0 and "PASS audio UID watcher semantics" in output,
            "audio UID watcher semantics",
            "global arbitrary/long app package, cached core-system UID rejection, spaced JSON fallback, enabled exact slot, UID mapping, cooldown and failure rc all verified",
            output.strip() or f"harness exited {rc}",
        )
        return

    shell = locate_posix_shell()
    if not shell:
        report.gap("audio UID watcher semantics", "POSIX sh is required")
        return

    watcher = (root / "bin/a2h_audio_watch").resolve()

    def shell_path(path: Path) -> str:
        resolved = path.resolve()
        if os.name == "nt":
            try:
                return resolved.relative_to(root.resolve()).as_posix()
            except ValueError:
                value = resolved.as_posix()
                if re.match(r"^[A-Za-z]:/", value):
                    return f"/{value[0].lower()}{value[2:]}"
        return resolved.as_posix()

    def event(package: str, name: str = "audio_track_message",
              scenario: str = "playback") -> str:
        return (
            'TransferEvent event = {"name":"' + name
            + '","audio_event":{"app_name":"' + package
            + '", "scenario":"' + scenario + '"}}\n'
        )

    def spaced_event(package: str) -> str:
        return (
            'TransferEvent event = {"name" : "audio_track_message",'
            '"audio_event" : {"app_name" : "' + package
            + '", "scenario" : "playback"}}\n'
        )

    try:
        temporary_parent = root if os.name == "nt" else None
        with tempfile.TemporaryDirectory(
                prefix=".a2h-audio-watch-", dir=temporary_parent) as temporary:
            base = Path(temporary)
            module = base / "module"
            config = module / "config"
            bin_dir = module / "bin"
            config.mkdir(parents=True)
            bin_dir.mkdir(parents=True)
            trigger = bin_dir / "a2h_trigger"
            trigger.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8", newline="\n")
            trigger.chmod(0o755)
            packages_list = base / "packages.list"
            long_package = "com.example." + "x" * 70
            packages_list.write_text(
                "com.example.globalgame 10420 0 /data/user/0/com.example.globalgame\n"
                "org.sample.rhythm 10421 0 /data/user/0/org.sample.rhythm\n"
                "com.android.systemui 1000 0 /data/user/0/com.android.systemui\n"
                + f"{long_package} 10422 0 /data/user/0/{long_package}\n",
                encoding="utf-8", newline="\n",
            )
            su_log = base / "su.calls"
            watcher_log = base / "watch.log"
            fake_su = base / "fake-su"
            fake_su.write_text(
                "#!/bin/sh\n"
                "printf '%s\\n' \"$*\" >> \"$A2H_TEST_SU_LOG\"\n"
                "set -- $3\n"
                "if [ \"${A2H_TEST_SU_RC:-0}\" -eq 0 ]; then\n"
                "  printf '4242 ready\\n' > \"$4\"\n"
                "  sleep 0.1\n"
                "  exit 0\n"
                "fi\n"
                "sleep 0.1\n"
                "exit \"$A2H_TEST_SU_RC\"\n",
                encoding="utf-8", newline="\n",
            )
            fake_su.chmod(0o755)

            def run_case(name: str, mode: str, lines: str,
                         enabled_custom: bool = False,
                         su_rc: int = 0,
                         cooldown_seconds: int = 30) -> tuple[int, str, str]:
                packages = [""] * 10
                states = ["0"] * 10
                packages[6] = "org.sample.rhythm"
                if enabled_custom:
                    states[6] = "1"
                (config / "state").write_text(mode + "\n", encoding="utf-8", newline="\n")
                (config / "packages.txt").write_text(
                    "\n".join(packages) + "\n", encoding="utf-8", newline="\n",
                )
                (config / "package_states").write_text(
                    "\n".join(states) + "\n", encoding="utf-8", newline="\n",
                )
                event_file = base / f"{name}.events"
                event_file.write_text(lines, encoding="utf-8", newline="\n")
                su_log.write_text("", encoding="utf-8", newline="\n")
                watcher_log.write_text("", encoding="utf-8", newline="\n")
                runtime = base / f"runtime-{name}"
                environment = os.environ.copy()
                environment.update({
                    "A2H_MODDIR": shell_path(module),
                    "A2H_CONFIG_DIR": shell_path(config),
                    "A2H_PACKAGES_LIST": shell_path(packages_list),
                    "A2H_TRIGGER_SOURCE": shell_path(trigger),
                    "A2H_RUNTIME_DIR": shell_path(runtime),
                    "A2H_LOG_FILE": shell_path(watcher_log),
                    "A2H_SU_BIN": shell_path(fake_su),
                    "A2H_SKIP_CHOWN": "1",
                    "A2H_COOLDOWN_SECONDS": str(cooldown_seconds),
                    "A2H_TEST_SU_LOG": shell_path(su_log),
                    "A2H_TEST_SU_RC": str(su_rc),
                })
                completed = subprocess.run(
                    posix_shell_command(
                        shell, shell_path(watcher), "--file", shell_path(event_file),
                    ),
                    cwd=root,
                    env=environment,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
                return (
                    completed.returncode,
                    su_log.read_text(encoding="utf-8"),
                    watcher_log.read_text(encoding="utf-8"),
                )

            global_rc, global_calls, _ = run_case(
                "global", "enabled", event("com.example.globalgame") * 2,
            )
            custom_rc, custom_calls, _ = run_case(
                "custom-enabled", "disabled", event("org.sample.rhythm"), True,
            )
            disabled_rc, disabled_calls, _ = run_case(
                "custom-disabled", "disabled", event("org.sample.rhythm"), False,
            )
            rejected_rc, rejected_calls, rejected_log = run_case(
                "rejected", "enabled",
                event("bad/package")
                + event("")
                + event("com.example.globalgame", scenario="capture")
                + event("com.example.globalgame", name="music_playback")
                + event("com.android.systemui")
                + event("com.android.systemui")
                + event("net.missing.game"),
                cooldown_seconds=0,
            )
            failed_rc, failed_calls, failed_log = run_case(
                "trigger-failure", "enabled", event("com.example.globalgame"),
                su_rc=17,
            )
            spaced_rc, spaced_calls, _ = run_case(
                "spaced-json", "enabled", spaced_event("com.example.globalgame"),
            )
            long_rc, long_calls, _ = run_case(
                "long-package", "enabled", event(long_package),
            )

            global_lines = global_calls.splitlines()
            custom_lines = custom_calls.splitlines()
            failed_lines = failed_calls.splitlines()
            spaced_lines = spaced_calls.splitlines()
            long_lines = long_calls.splitlines()
            ok = (
                global_rc == custom_rc == disabled_rc == rejected_rc == failed_rc
                == spaced_rc == long_rc == 0
                and len(global_lines) == 1
                and global_lines[0].startswith("10420 -c ")
                and " --lease " in global_lines[0]
                and "/a2h_trigger --lease " in global_lines[0]
                and len(custom_lines) == 1
                and custom_lines[0].startswith("10421 -c ")
                and not disabled_calls
                and not rejected_calls
                and rejected_log.count("reason=core-system-uid") == 1
                and len(failed_lines) == 1
                and "rc=17" in failed_log
                and len(spaced_lines) == 1
                and spaced_lines[0].startswith("10420 -c ")
                and len(long_lines) == 1
                and long_lines[0].startswith("10422 -c ")
            )
            detail = (
                f"rcs={global_rc}/{custom_rc}/{disabled_rc}/{rejected_rc}/{failed_rc}/"
                f"{spaced_rc}/{long_rc} "
                f"global={global_lines!r} custom={custom_lines!r} "
                f"disabled={disabled_calls!r} rejected={rejected_calls!r} "
                f"rejected_log={rejected_log.strip()!r} "
                f"failed={failed_lines!r} spaced={spaced_lines!r} long={long_lines!r} "
                f"log={failed_log.strip()!r}"
            )
            report.check(
                ok,
                "audio UID watcher semantics",
                "global arbitrary/long app package, cached core-system UID rejection, spaced JSON fallback, enabled exact slot, UID mapping, cooldown and failure rc all verified",
                detail,
            )
    except (OSError, UnicodeError) as exc:
        report.add("FAIL", "audio UID watcher semantics", str(exc))


def check_audio_policy_lease(root: Path, report: Report, use_adb: bool) -> None:
    watcher_source = (root / "bin/a2h_audio_watch").read_text(encoding="utf-8")
    cleanup_block = extract_function(watcher_source, "cleanup() {", "watch_events() {")
    cleanup_contract = (
        bool(cleanup_block)
        and "while [ \"$cleanup_grace\" -lt 25 ]" in cleanup_block
        and "sleep 0.02" in cleanup_block
        and cleanup_block.index('rm -f "$lease_token"')
        < cleanup_block.index('while [ "$cleanup_grace" -lt 25 ]')
        < cleanup_block.index('kill "$lease_read_pid"')
    )
    report.check(
        cleanup_contract,
        "audio lease graceful shutdown contract",
        "token-first shared grace window precedes PID/starttime escalation",
        "watcher cleanup can still terminate a lease before its normal stream close",
    )
    if not use_adb:
        report.gap("audio policy lease lifecycle", "Android /proc and FIFO semantics require --adb")
        return
    adb = shutil.which("adb")
    if not adb:
        report.add("FAIL", "audio policy lease lifecycle", "adb requested but not found")
        return
    harness = (root / "tests/audio_policy_lease_harness.sh").resolve()
    watcher = (root / "bin/a2h_audio_watch").resolve()
    remote_harness = "/data/local/tmp/a2h_stage24_test_harness.sh"
    remote_watcher = "/data/local/tmp/a2h_stage24_test_watcher.sh"
    remote_base = "/data/local/tmp"
    outputs: list[str] = []
    rc = 1
    try:
        for local, remote in ((harness, remote_harness), (watcher, remote_watcher)):
            pushed = subprocess.run(
                [adb, "push", str(local), remote], cwd=root,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
            )
            outputs.append(pushed.stdout.decode("utf-8", errors="replace"))
            if pushed.returncode != 0:
                raise OSError(f"adb push failed for {local.name}")
        command = (
            f"A2H_TEST_BASE={remote_base} A2H_TEST_SHELL=/system/bin/sh "
            f"sh {remote_harness} {remote_watcher}"
        )
        completed = subprocess.run(
            [adb, "shell", "su", "-c", command], cwd=root,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
        )
        rc = completed.returncode
        outputs.append(completed.stdout.decode("utf-8", errors="replace"))
    except OSError as exc:
        outputs.append(str(exc))
    finally:
        subprocess.run(
            [adb, "shell", "su", "-c",
             f"rm -f {remote_harness} {remote_watcher}"],
            cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            check=False,
        )
    output = "".join(outputs)
    report.check(
        rc == 0 and "PASS audio policy lease lifecycle and singleton storm guard" in output,
        "audio policy lease lifecycle",
        "duplicate watcher rejection, same-package event storm coalescing, fallback promotion, self-session suppression, multi-port release and cleanup passed",
        output.strip() or f"harness exited {rc}",
    )


def check_webui_writer_transaction(root: Path, report: Report, use_adb: bool) -> None:
    node = shutil.which("node")
    if not node:
        report.gap("WebUI writer transaction regression", "node is required to generate the production writer command")
        return

    webui = (root / "webroot/index.html").read_text(encoding="utf-8")
    normalizer = extract_function(webui, "function normalizeSlot", "function cloneSlots")
    quoter = extract_function(webui, "function shellQuote", "function snapshotConfig")
    writer = extract_function(
        webui, "function buildCommitCommand(state,snapshot,writePackages){", "function buildCommand("
    )
    builder = extract_function(webui, "function buildCommand(", "function findExecBridge")
    if not normalizer or not quoter or not writer or not builder:
        report.add("FAIL", "WebUI writer transaction regression", "production writer functions could not be extracted")
        return

    snapshot = {
        "packages": [*OFFICIAL, "com.example.slot7", "com.example.slot8", "", ""],
        "enabled": [True, True, True, True, True, True, True, True, False, False],
        "gameAutoPause": "enabled",
        "generation": "4242",
    }
    javascript = "\n".join(
        (
            "'use strict';",
            normalizer,
            quoter,
             writer,
            f"const snapshot={json.dumps(snapshot, separators=(',', ':'))};",
             "process.stdout.write(buildCommitCommand('disabled',snapshot,true));",
        )
    )
    node_rc, writer_command = run_command([node, "-e", javascript], root)
    if node_rc != 0 or not writer_command.strip():
        report.add("FAIL", "WebUI writer transaction regression", writer_command.strip() or f"node rc={node_rc}")
        return

    combined_javascript = "\n".join(
        (
            "'use strict';",
            normalizer,
            quoter,
             writer,
            builder,
            f"const snapshot={json.dumps(snapshot, separators=(',', ':'))};",
            "process.stdout.write(buildCommand('disabled',snapshot,true));",
        )
    )
    node_rc, combined_command = run_command([node, "-e", combined_javascript], root)
    if node_rc != 0 or not combined_command.strip():
        report.add("FAIL", "WebUI writer/apply transaction regression", combined_command.strip() or f"node rc={node_rc}")
        return

    expected_args = [
        "disabled",
        "enabled",
        "4242",
        *snapshot["packages"],
        *("1" if enabled else "0" for enabled in snapshot["enabled"]),
    ]
    try:
        writer_tokens = shlex.split(writer_command, posix=True)
        combined_lines = [line for line in combined_command.splitlines() if not line.startswith("#")]
        combined_tokens = shlex.split("\n".join(combined_lines), posix=True)
    except ValueError as exc:
        report.add("FAIL", "WebUI writer/apply transaction regression", f"invalid shell quoting: {exc}")
        return
    expected_prefix = ["sh", "/data/adb/modules/a2h_hook/bin/a2h_apply", "commit-queue"]
    if (
        len(expected_args) != 23
        or writer_tokens[:3] != expected_prefix
        or combined_tokens[:3] != expected_prefix
        or writer_tokens[3:] != expected_args
        or combined_tokens[3:] != expected_args
    ):
        report.add(
            "FAIL",
            "WebUI writer/apply transaction regression",
            f"writer={writer_tokens!r} combined={combined_tokens!r} expected_args={expected_args!r}",
        )
        return

    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    validator = extract_function(applier, "valid_package() {", "restore_default_config_unlocked() {")
    transaction = extract_function(applier, "commit_config_abort() {", "set_controls() {")
    if not validator or not transaction:
        report.add("FAIL", "WebUI writer/apply transaction regression", "production transaction functions not found")
        return

    production_call = writer_command.replace(
        "sh /data/adb/modules/a2h_hook/bin/a2h_apply commit-queue", "commit_config_and_queue", 1
    )
    combined_call = combined_command.replace(
        "sh /data/adb/modules/a2h_hook/bin/a2h_apply commit-queue", "commit_config_and_queue", 1
    )
    harness = validator + transaction + r'''
set -u
base=__WRITER_TEST_BASE__/a2h_webui_writer_$$
CFG_DIR="$base/config"
CFG_STATE="$CFG_DIR/state"
CFG_GAME_POLICY="$CFG_DIR/game_auto_pause"
CFG_PKGS="$CFG_DIR/packages.txt"
CFG_STATES="$CFG_DIR/package_states"
CFG_GENERATION="$CFG_DIR/config_generation"
CFG_NORMALIZED_FINGERPRINT="$CFG_DIR/.normalized_fingerprint"
CONFIG_LOCK="$base/config.lock"
QUEUE_CALLED="$base/queue_called"
LOCK_ATTEMPTS="$base/lock_attempts"
fail_mv=0
mv_count=0

fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
cleanup() { rm -rf "$base"; }
trap cleanup 0 1 2 15

acquire_config_lock() {
  printf '1\n' >> "$LOCK_ATTEMPTS"
  mkdir "$CONFIG_LOCK" 2>/dev/null
}
release_config_lock() { rmdir "$CONFIG_LOCK" 2>/dev/null; }
queue_changed_fast() { printf 'queued\n' >> "$QUEUE_CALLED"; }
compute_config_value_signature() {
  CURRENT_CONFIG_VALUE_SIGNATURE=fixture-signature
}
record_normalized_fingerprint() {
  printf 'fixture\n' > "$CFG_NORMALIZED_FINGERPRINT"
}
mv() {
  mv_count=$((mv_count + 1))
  [ "$fail_mv" -eq 0 ] || [ "$mv_count" -ne "$fail_mv" ] || return 70
  command mv "$@"
}

seed_old() {
  rm -rf "$CFG_DIR" "$CONFIG_LOCK"
  rm -f "$QUEUE_CALLED" "$LOCK_ATTEMPTS"
  mkdir -p "$CFG_DIR"
  printf 'old-mode\n' > "$CFG_STATE"
  printf 'old-policy\n' > "$CFG_GAME_POLICY"
  printf 'old packages\n' > "$CFG_PKGS"
  printf 'old states\n' > "$CFG_STATES"
  printf '41\n' > "$CFG_GENERATION"
}

assert_old() {
  label=$1
  [ "$(cat "$CFG_STATE")" = old-mode ] || fail "$label-mode-rollback"
  [ "$(cat "$CFG_GAME_POLICY")" = old-policy ] || fail "$label-policy-rollback"
  [ "$(cat "$CFG_PKGS")" = 'old packages' ] || fail "$label-packages-rollback"
  [ "$(cat "$CFG_STATES")" = 'old states' ] || fail "$label-states-rollback"
  [ "$(cat "$CFG_GENERATION")" = 41 ] || fail "$label-generation-rollback"
  [ ! -e "$CONFIG_LOCK" ] || fail "$label-lock-leaked"
  [ -z "$(find "$CFG_DIR" -type f -name '.*.[0-9]*' -print -quit 2>/dev/null)" ] || fail "$label-temp-leaked"
  [ ! -e "$QUEUE_CALLED" ] || fail "$label-queue-ran"
}

seed_old
if commit_config_and_queue invalid disabled 4242 a b c d e f g h i j 1 1 1 1 1 1 1 1 1 1; then
  fail invalid-mode-accepted
fi
[ ! -e "$LOCK_ATTEMPTS" ] || fail invalid-mode-acquired-lock
assert_old invalid-mode

seed_old
if commit_config_and_queue disabled enabled 4242 a b c d e f g h i j 1 1 1 1 1 1 1 1 1; then
  fail invalid-count-accepted
fi
[ ! -e "$LOCK_ATTEMPTS" ] || fail invalid-count-acquired-lock
assert_old invalid-count

seed_old
if commit_config_and_queue disabled enabled 4242 a b c d e f g h i j 1 1 1 1 1 broken 1 1 1 1; then
  fail invalid-state-accepted
fi
[ ! -e "$LOCK_ATTEMPTS" ] || fail invalid-state-acquired-lock
assert_old invalid-state

fault=1
while [ "$fault" -le 5 ]; do
  seed_old
  if ( fail_mv=$fault; mv_count=0; __PRODUCTION_CALL__ ); then
    fail "commit-$fault-failure-returned-success"
  fi
  assert_old "commit-$fault"
  fault=$((fault + 1))
done

seed_old
( fail_mv=0; mv_count=0; __COMBINED_CALL__ ) || fail success-command
[ "$(cat "$CFG_STATE")" = disabled ] || fail success-mode
[ "$(cat "$CFG_GAME_POLICY")" = enabled ] || fail success-policy
[ "$(wc -l < "$CFG_PKGS" | tr -d ' ')" = 10 ] || fail success-package-count
[ "$(sed -n '8p' "$CFG_PKGS")" = com.example.slot8 ] || fail success-slot8-package
[ "$(wc -l < "$CFG_STATES" | tr -d ' ')" = 10 ] || fail success-state-count
[ "$(sed -n '8p' "$CFG_STATES")" = 1 ] || fail success-slot8-state
[ "$(cat "$CFG_GENERATION")" = 4242 ] || fail success-generation
[ ! -e "$CONFIG_LOCK" ] || fail success-lock-leaked
[ -z "$(find "$CFG_DIR" -type f -name '.*.[0-9]*' -print -quit 2>/dev/null)" ] || fail success-temp-leaked
[ "$(wc -l < "$QUEUE_CALLED" | tr -d ' ')" = 1 ] || fail success-queue-count
printf 'PASS WebUI writer/apply transaction regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "WebUI writer transaction regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = locate_posix_shell()
        if not shell:
            report.gap("WebUI writer transaction regression", "rerun with --adb or on a POSIX host")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = (
        harness.replace("__WRITER_TEST_BASE__", test_base)
        .replace("__PRODUCTION_CALL__", production_call)
        .replace("__COMBINED_CALL__", combined_call)
        .encode("utf-8")
    )
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS WebUI writer/apply transaction regression" in output,
        "WebUI writer/apply transaction regression",
        "23 ordered arguments, validation-before-lock, five-file rollback/cleanup, and success-only queue passed",
        output.strip(),
    )


def check_apply_capability_runtime(root: Path, report: Report, use_adb: bool) -> None:
    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    capability_functions = extract_function(
        applier, "probe_patcher_capabilities() {", "apply_current() {"
    )
    apply_current = extract_function(
        applier, "apply_current() {", "refresh_policy_state() {"
    )
    if not capability_functions or not apply_current:
        report.add(
            "FAIL",
            "native apply capability runtime regression",
            "production capability or apply function could not be extracted",
        )
        return

    harness = capability_functions + apply_current + r'''
set -eu
base=__CAPABILITY_TEST_BASE__/a2h_capability_$$
CFG_DIR="$base/config"
PATCHER_SUPPORT_MARKER="$CFG_DIR/.patcher_packages_support"
CALLS="$base/calls"
MODE_FILE="$base/mode"
TMP_PKGS="$base/packages"
LAST_PID_FILE="$base/last_pid"
MAIN_LOG="$base/main.log"
HAL_PID=123
HAL_BASE=
CURRENT_GAME_AUTO_PAUSE=enabled
CURRENT_REVISION=7
CURRENT_SNAPSHOT=fixture
ACTIVE_COUNT=6
PATCHER=patcher_stub

fail() { printf 'FAIL: %s\n' "$1"; exit 1; }
cleanup() { rm -rf "$base"; }
trap cleanup 0 1 2 15
mkdir -p "$CFG_DIR"
printf 'com.kugou.android\n' > "$TMP_PKGS"
: > "$MAIN_LOG"

log() { :; }
log_slots() { :; }
resolve_hal() { HAL_PID=123; HAL_BASE=; return 0; }
write_value_atomic() {
  atomic_path=$1
  atomic_value=$2
  printf '%s\n' "$atomic_value" > "$atomic_path"
}
patcher_stub() {
  if [ "${1:-}" = --help ]; then
    if [ "$(cat "$MODE_FILE")" = modern ]; then
      printf '%s\n' 'Usage: a2h_patch --packages FILE capabilities: apply-final-verified'
    else
      printf '%s\n' 'Usage: a2h_patch legacy'
    fi
    return 0
  fi
  printf '%s\n' "$*" >> "$CALLS"
  return 0
}
reset_case() {
  printf '%s\n' "$1" > "$MODE_FILE"
  : > "$CALLS"
  rm -f "$PATCHER_SUPPORT_MARKER" "$LAST_PID_FILE"
  PATCHER_SUPPORTS_PACKAGES=
  PATCHER_APPLY_FINAL_VERIFIED=
}

reset_case modern
CURRENT_MODE=enabled
apply_current modern-global >/dev/null || fail modern-global-apply
[ "$(wc -l < "$CALLS" | tr -d ' ')" = 1 ] || fail modern-global-call-count
grep -q '^123 --game-auto-pause enabled$' "$CALLS" || fail modern-global-command
grep -q -- '--check' "$CALLS" && fail modern-global-extra-check

reset_case modern
CURRENT_MODE=disabled
apply_current modern-whitelist >/dev/null || fail modern-whitelist-apply
[ "$(wc -l < "$CALLS" | tr -d ' ')" = 1 ] || fail modern-whitelist-call-count
grep -q '^--disable 123 ' "$CALLS" || fail modern-whitelist-command
grep -q -- '--check' "$CALLS" && fail modern-whitelist-extra-check

reset_case legacy
CURRENT_MODE=enabled
apply_current legacy-global >/dev/null || fail legacy-global-apply
[ "$(wc -l < "$CALLS" | tr -d ' ')" = 2 ] || fail legacy-global-call-count
sed -n '2p' "$CALLS" | grep -q '^--check global 123 ' || fail legacy-global-check
printf 'PASS native apply capability runtime regression\n'
'''

    if use_adb:
        adb = shutil.which("adb")
        if not adb:
            report.add("FAIL", "native apply capability runtime regression", "adb requested but not found")
            return
        command = [adb, "shell", "sh", "-s"]
        test_base = "/data/local/tmp"
    else:
        shell = locate_posix_shell()
        if not shell:
            report.gap(
                "native apply capability runtime regression",
                "rerun with --adb or on a POSIX host",
            )
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__CAPABILITY_TEST_BASE__", test_base).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS native apply capability runtime regression" in output,
        "native apply capability runtime regression",
        "modern patchers apply once; legacy patchers retain apply plus external check",
        output.strip() or f"harness exited {rc}",
    )


def check_source_contracts(root: Path, report: Report) -> None:
    source = (root / "src/patcher_v3.c").read_text(encoding="utf-8")
    report.check(
        "#define WHITELIST_STUB_WORDS 19" in source and
        "#define WHITELIST_STUB_BYTES (WHITELIST_STUB_WORDS * sizeof(uint32_t))" in source,
        "whitelist stub size",
        "19 words / 76 bytes",
        "stub size constants are missing or changed",
    )

    capacity_guard = bool(
        re.search(
            r"size_t\s+required_capacity\s*=\s*"
            r"mode\s*==\s*1\s*\?\s*WHITELIST_STUB_BYTES\s*:\s*"
            r"\(\s*mode\s*==\s*0\s*\?\s*sizeof\(GLOBAL_PATCH\)\s*:\s*0\s*\)",
            source,
            re.DOTALL,
        )
        and re.search(
            r"if\s*\(\s*required_capacity\s*&&\s*"
            r"g_func_capacity\s*<\s*required_capacity\s*\)",
            source,
        )
    )
    report.check(
        capacity_guard,
        "mode-specific write capacity guard",
        "whitelist/global requirements are compared with proven function capacity",
        "required_capacity selection or g_func_capacity guard is missing",
    )

    locate = extract_function(source, "static int locate_targets", "static int setup_custom_cave")
    elf_index = locate.find("resolve_func_by_elf_symbol")
    profile_fast_index = locate.find('"profile-fast"')
    report.check(
        elf_index >= 0 and (profile_fast_index < 0 or elf_index < profile_fast_index),
        "profile identity ordering",
        "mapped-ELF resolver precedes profile fast acceptance",
        f"ELF index={elf_index} profile-fast index={profile_fast_index}",
    )

    main_body = extract_function(source, "int main(", "\n}")
    report.check(
        main_body.find("capture_hal_map_identity") >= 0 and main_body.find("capture_hal_map_identity") < main_body.find("locate_targets"),
        "mapped HAL identity capture",
        "mapped path/device/inode captured before target resolution",
        "identity capture is absent or ordered after target resolution",
    )

    tx_begin = main_body.find("whitelist_transaction_begin")
    apply_strings = main_body.find("apply_strings", tx_begin)
    install_stub = main_body.find("install_whitelist_stub", apply_strings)
    tx_restore = main_body.find("whitelist_transaction_restore", install_stub)
    auxiliary_begin = main_body.find("auxiliary_transaction_begin")
    auxiliary_restore = main_body.find("auxiliary_transaction_restore", install_stub)
    outer_rollback = (
        tx_begin >= 0
        and auxiliary_begin >= 0
        and tx_begin < auxiliary_begin < apply_strings < install_stub < tx_restore < auxiliary_restore
        and re.search(
            r"if\s*\(\s*!final_ok\s*\).*?"
            r"whitelist_transaction_restore\s*\(.*?"
            r"auxiliary_transaction_restore\s*\(",
            main_body,
            re.DOTALL,
        )
        is not None
    )
    report.check(
        outer_rollback,
        "two-pass transaction rollback",
        "main snapshots before strings/stub and restores on any final failure",
        "main does not wrap both whitelist passes in the outer rollback transaction",
    )

    fault_seam = (
        "#ifdef A2H_TEST_FAULT_INJECTION" in source
        and "g_test_icache_override" in source
        and "g_test_icache_fail_call" in source
        and "g_test_icache_calls" in source
    )
    report.check(
        fault_seam,
        "native fault injection seam",
        "A2H_TEST_FAULT_INJECTION controls remote cache outcomes in tests",
        "test-only A2H_TEST_FAULT_INJECTION cache controls are missing",
    )

    exact_suffix = extract_function(
        source,
        "static int exact_whitelist_stub_suffix",
        "static int is_stub_head",
    )
    stale_overlay = extract_function(
        source,
        "static int global_stub_suffix_overlay",
        "static int owned_global_stub_suffix_ok",
    )
    scanner = extract_function(
        source,
        "static int scan_func_by_sig",
        "static int score_string_rel",
    )
    full_suffix_contract = (
        "WHITELIST_STUB_FIXED_TAIL[WHITELIST_STUB_WORDS - 2]" in source
        and re.search(
            r"for\s*\([^;]*;\s*word\s*<\s*WHITELIST_STUB_WORDS\s*;",
            exact_suffix,
        )
        is not None
        and "WHITELIST_STUB_FIXED_TAIL[word - 2]" in exact_suffix
        and "exact_whitelist_stub_suffix(head, n, 2)" in stale_overlay
        and "exact_whitelist_stub_suffix(head, n, 4)" in stale_overlay
        and re.search(
            r"patched_global_candidate_ok\s*\(\s*pid\s*,\s*base\s*,\s*h\s*,\s*len\s*-\s*i\s*\)",
            scanner,
        )
        is not None
    )
    report.check(
        full_suffix_contract,
        "stale-stub full suffix contract",
        "8/16-byte overlays require every remaining matcher word",
        "stale overlay matcher does not prove the complete suffix/span",
    )

    marked_cave = extract_function(
        source,
        "static int marked_cave_ok",
        "static int exact_stub_targets_cave",
    )
    stale_owner = extract_function(
        source,
        "static int owned_global_stub_suffix_ok",
        "static int patched_global_candidate_ok",
    )
    owned_cave_contract = (
        "global_stub_suffix_overlay(head, n)" in stale_owner
        and re.search(
            r"load_cave_hint\s*\(\s*&cave\s*\)\s*&&\s*"
            r"marked_cave_ok\s*\(\s*pid\s*,\s*base\s*,\s*cave\s*\)",
            stale_owner,
        )
        is not None
        and "memcmp(marker_value, WHITELIST_MARKER, sizeof(marker_value))" in marked_cave
        and "uint64_t ptrs[MAX_SLOTS]" in marked_cave
        and re.search(r"ptrs\[i\]\s*!=\s*expected", marked_cave) is not None
    )
    report.check(
        owned_cave_contract,
        "stale-stub cave ownership contract",
        "exact suffix also requires the owned marker and pointer table",
        "stale suffix acceptance is not tied to the owned cave payload",
    )

    elf_resolver = extract_function(
        source,
        "static int resolve_func_by_elf_symbol",
        "static int scan_func_by_sig",
    )
    elf_stock_tail_contract = re.search(
        r"int\s+stale_owned\s*=\s*stale_overlay\s*\?\s*"
        r"owned_global_stub_suffix_ok\s*\(\s*pid\s*,\s*base\s*,\s*live\s*,\s*sym\.size\s*\)\s*:\s*0\s*;"
        r".*?else\s+if\s*\(\s*stale_owned\s*&&\s*"
        r"memcmp\s*\(\s*live\s*\+\s*WHITELIST_STUB_BYTES\s*,\s*"
        r"disk\s*\+\s*WHITELIST_STUB_BYTES\s*,\s*"
        r"sym\.size\s*-\s*WHITELIST_STUB_BYTES\s*\)\s*==\s*0",
        elf_resolver,
        re.DOTALL,
    ) is not None
    report.check(
        elf_stock_tail_contract,
        "stale-stub ELF stock-tail contract",
        "owned stale stub also matches disk bytes after byte 76",
        "ELF stale-stub recovery lacks the stock tail comparison after byte 76",
    )


def check_ndk(root: Path, report: Report, ndk: Path | None) -> None:
    resolved = locate_ndk(ndk)
    if not resolved:
        report.gap("strict NDK compile", "Android NDK not found; pass --ndk")
        return
    suffix = ".cmd" if os.name == "nt" else ""
    host = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    clang = resolved / f"toolchains/llvm/prebuilt/{host}/bin/aarch64-linux-android31-clang{suffix}"
    if not clang.is_file():
        report.add("FAIL", "strict NDK compile", f"compiler missing: {clang}")
        return
    command = [
        str(clang), "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
        str(root / "src/patcher_v3.c"),
    ]
    rc, output = run_command(command, root)
    report.check(rc == 0, "strict NDK compile", f"NDK {resolved.name} -Werror passed", output.strip())


def check_hal_fixtures(root: Path, report: Report, archive: Path) -> None:
    try:
        sys.path.insert(0, str(root / "tools"))
        from hal_fingerprint import parse_elf  # type: ignore
    except (ImportError, OSError) as exc:
        report.add("FAIL", "HAL fixture loader", str(exc))
        return

    for system, expected in HAL_CASES.items():
        path = archive / str(expected["relative"])
        if not path.is_file():
            report.gap(f"HAL fixture {system}", f"missing ignored local archive file: {path}")
            continue
        try:
            actual = parse_elf(path)
        except (OSError, ValueError) as exc:
            report.add("FAIL", f"HAL fixture {system}", str(exc))
            continue
        symbol = actual["a2h"]["is_A2H_app_symbol"] or {}
        lifecycle = actual["a2h"].get("lifecycle_symbols", {})
        lifecycle_expected = {
            "updateA2HMode": (int(expected["update"]), 568),
            "stream_setParameters": (int(expected["stream"]), 12040),
            "isA2HAllowed": (int(expected["policy"]), 700),
            "updateOutputPoolActive": (int(expected["output_pool"]), 1128),
            "streamAttributeDevices": (int(expected["device_layout"]), 616),
            "stream_open": (int(expected["stream_open"]), 0xD1C),
        }
        lifecycle_matched = all(
            key in lifecycle
            and int(lifecycle[key].get("value", "0"), 0) == value
            and lifecycle[key].get("size") == size
            and lifecycle[key].get("file_offset") is not None
            for key, (value, size) in lifecycle_expected.items()
        )
        matched = (
            actual["file"]["sha256"] == expected["sha256"]
            and actual["file"]["size"] == expected["size"]
            and actual["elf"]["gnu_build_id"] == expected["build_id"]
            and int(symbol.get("value", "0"), 0) == expected["symbol"]
            and symbol.get("size") == expected["symbol_size"]
            and symbol.get("state") == "stock"
            and lifecycle_matched
        )
        report.check(
            matched,
            f"HAL fixture {system}",
            f"sha/build-id/is_A2H_app=0x{expected['symbol']:x}/160 and lifecycle symbols verified",
            json.dumps({"file": actual["file"], "build_id": actual["elf"]["gnu_build_id"], "symbol": symbol, "lifecycle": lifecycle}, ensure_ascii=False),
        )
        if not lifecycle_matched:
            continue

        data = path.read_bytes()
        tail_layout = derive_executable_tail_layout(
            actual["elf"]["program_headers"], len(data)
        )
        helper_vaddr = int(tail_layout["concurrent"]) if tail_layout else 0
        tail_zero_ok = bool(
            tail_layout
            and data[
                int(tail_layout["file_offset"]) :
                int(tail_layout["file_offset"]) + int(tail_layout["span"])
            ] == bytes(int(tail_layout["span"]))
        )
        update_file = int(lifecycle["updateA2HMode"]["file_offset"], 0)
        stream_file = int(lifecycle["stream_setParameters"]["file_offset"], 0)
        policy_file = int(lifecycle["isA2HAllowed"]["file_offset"], 0)
        stream_open_file = int(lifecycle["stream_open"]["file_offset"], 0)
        stream_open_vaddr = int(expected["stream_open"])
        stream_open_tail = data[
            stream_open_file + STREAM_OPEN_EPILOGUE_EXPECTED_OFF :
            stream_open_file + STREAM_OPEN_EPILOGUE_EXPECTED_OFF +
            STREAM_OPEN_EPILOGUE_BYTES
        ]
        stream_open_manager_hits = sum(
            1
            for offset in range(0, int(lifecycle["stream_open"]["size"]), 4)
            if data[stream_open_file + offset : stream_open_file + offset + 4]
            == struct.pack("<I", STREAM_OPEN_MANAGER_LOAD)
        )
        stream_open_shape = (
            stream_open_tail == STREAM_OPEN_EPILOGUE_STOCK
            and data[stream_open_file + 0x30 : stream_open_file + 0x34]
            == struct.pack("<I", STREAM_OPEN_THIS_MOV)
            and stream_open_manager_hits > 0
        )
        app_disk = data[update_file + 0x130 : update_file + 0x178]
        stock_shape = (
            len(app_disk) == 72
            and app_disk[:0x24] == UPDATE_APP_DISK_TEMPLATE[:0x24]
            and app_disk[0x28:] == UPDATE_APP_DISK_TEMPLATE[0x28:]
        )
        stock_call = struct.unpack_from("<I", app_disk, 0x24)[0] if stock_shape else 0
        stock_site = int(expected["update"]) + 0x154
        immediate = stock_call & 0x03FFFFFF
        if immediate & 0x02000000:
            immediate -= 0x04000000
        call_target = stock_site + immediate * 4
        call_ok = (
            (stock_call & 0xFC000000) == 0x94000000
            and call_target == int(expected["app_call"])
        )

        plt_ok = exact_plt_entry(
            data, actual["elf"]["program_headers"], call_target
        )
        stream_ref = data[
            stream_file + STREAM_REF_PATCH_OFF :
            stream_file + STREAM_REF_PATCH_OFF + STREAM_REF_PATCH_BYTES
        ]
        stream_ref_shape = stream_ref_stock_shape(stream_ref)
        delete_instruction = (
            struct.unpack_from("<I", stream_ref,
                               STREAM_REF_DELETE_BL_OFF)[0]
            if stream_ref_shape else 0
        )
        delete_target = decode_aarch64_bl(
            int(expected["stream"]) + STREAM_REF_PATCH_OFF +
            STREAM_REF_DELETE_BL_OFF,
            delete_instruction,
        )
        delete_ok = (
            delete_target == int(expected["delete_call"])
            and delete_target is not None
            and exact_plt_entry(
                data, actual["elf"]["program_headers"], delete_target
            )
        )

        allowed_instruction = struct.unpack_from(
            "<I", data, update_file + 0x1D4
        )[0]
        allowed_target = decode_aarch64_bl(
            int(expected["update"]) + 0x1D4, allowed_instruction
        )
        allowed_ok = (
            allowed_target is not None
            and exact_plt_entry(
                data, actual["elf"]["program_headers"], allowed_target
            )
        )
        stream_update_instruction = struct.unpack_from(
            "<I", data, stream_file + 0x23FC
        )[0]
        stream_update_target = decode_aarch64_bl(
            int(expected["stream"]) + 0x23FC, stream_update_instruction
        )
        stream_update_ok = (
            stream_update_target is not None
            and exact_plt_entry(
                data, actual["elf"]["program_headers"], stream_update_target
            )
        )
        stream_update_context = data[
            stream_file + 0x23F8 : stream_file + 0x2404
        ]
        stream_update_shape = (
            len(stream_update_context) == len(STREAM_UPDATE_CALL_TEMPLATE)
            and stream_update_context[:4] == STREAM_UPDATE_CALL_TEMPLATE[:4]
            and stream_update_context[8:] == STREAM_UPDATE_CALL_TEMPLATE[8:]
        )
        stream_events = tuple(
            data[
                stream_file + offset :
                stream_file + offset + size
            ]
            for offset, size in zip(STREAM_EVENT_OFFSETS, STREAM_EVENT_SIZES)
        )
        new_node_words = (
            struct.unpack("<7I", stream_events[1])
            if len(stream_events[1]) == STREAM_EVENT_SIZES[1] else ()
        )
        stream_event_stock_shape = (
            len(stream_events[0]) == STREAM_EVENT_SIZES[0]
            and stream_events[0][:16] == STREAM_EVENT_STOCK_TEMPLATE[0][:16]
            and stream_events[0][20:] == STREAM_EVENT_STOCK_TEMPLATE[0][20:]
            and len(new_node_words) == 7
            and new_node_words[0] & 0x9F00001F == 0x90000018
            and stream_events[1][4:20] == STREAM_EVENT_STOCK_TEMPLATE[1][4:20]
            and new_node_words[5] & 0x9F00001F == 0x90000003
            and new_node_words[6] & 0xFFC003FF == 0x91000063
            and stream_events[2] == STREAM_EVENT_STOCK_TEMPLATE[2]
        )
        strlen_instruction = (
            struct.unpack_from("<I", stream_events[0], 16)[0]
            if stream_event_stock_shape else 0
        )
        strlen_target = decode_aarch64_bl(
            int(expected["stream"]) + STREAM_EVENT_OFFSETS[0] + 16,
            strlen_instruction,
        )
        strlen_ok = (
            strlen_target is not None
            and strlen_target == int(expected["strlen_call"])
            and exact_plt_entry(
                data, actual["elf"]["program_headers"], strlen_target
            )
        )
        output_file = int(lifecycle["updateOutputPoolActive"]["file_offset"], 0)
        device_layout_file = int(
            lifecycle["streamAttributeDevices"]["file_offset"], 0
        )
        output_tail = data[
            output_file + 0x278 : output_file + 0x278 +
            len(OUTPUT_POOL_TAIL_STOCK_TEMPLATE)
        ]
        output_ok = (
            len(output_tail) == len(OUTPUT_POOL_TAIL_STOCK_TEMPLATE)
            and output_tail == OUTPUT_POOL_TAIL_STOCK_TEMPLATE
        )
        manager_registers_ok = (
            data[stream_file + 0x1F58 : stream_file + 0x1F5C]
            == STREAM_APP_MAP_MANAGER_ADD
            and data[stream_file + 0x2144 : stream_file + 0x2148]
            == STREAM_APP_MAP_MANAGER_ADD
            and data[output_file + 0x38 : output_file + 0x3C]
            == OUTPUT_POOL_MANAGER_ARG_MOV
        )
        policy_stream_layout_ok = tuple(
            struct.unpack_from("<I", data, policy_file + offset)[0]
            for offset in (0x40, 0x50, 0x64, 0x6C, 0x78, 0x7C, 0x84, 0x8C)
        ) == (
            0xAA0003F4, 0x5280011B, 0xF9413E88, 0x9100437B,
            0xF9413A88, 0xF87B6908, 0xF9412517, 0xF9412919,
        )
        idle_overlay = build_idle_clear_overlay(
            int(expected["update"]), int(expected["policy"]), helper_vaddr
        )
        generated_update = idle_overlay[0] if idle_overlay else b""
        generated_idle_count_branch = idle_overlay[1] if idle_overlay else b""
        generated_idle_branch = idle_overlay[2] if idle_overlay else b""
        concurrent_overlay = build_concurrent_overlays(
            helper_vaddr, int(expected["policy"]),
            int(expected["stream"]),
            stream_open_vaddr, STREAM_OPEN_EPILOGUE_EXPECTED_OFF,
            int(expected["stream"]) + 0x23F8,
            stream_update_target or 0,
        )
        generated_concurrent_helper = (
            concurrent_overlay[0] if concurrent_overlay else b""
        )
        generated_concurrent_policy = (
            concurrent_overlay[1] if concurrent_overlay else b""
        )
        generated_stream_events = (
            concurrent_overlay[2]
            if concurrent_overlay else (b"", b"", b"")
        )
        generated_stream_open = (
            concurrent_overlay[3] if concurrent_overlay else b""
        )
        eligible_words = (
            struct.unpack("<8I", generated_update)
            if len(generated_update) == 32 else ()
        )
        eligible_overlay_ok = (
            len(generated_update) == len(UPDATE_FLAGS_STOCK) == 32
            and generated_update != UPDATE_FLAGS_GAME_HANDOFF_LEGACY
            and generated_update != UPDATE_FLAGS_GAME_ACTIVE_FAILED
            and eligible_words == (
                0xB9400C08, 0x721E091F, 0x54000181, 0x37900168,
                0x14000004, 0x2A1F03F3,
                encode_aarch64_b(
                    int(expected["update"]) + 0xF4,
                    helper_vaddr + 0xBC,
                ), 0xD503201F,
            )
            and generated_idle_branch == struct.pack(
                "<I", encode_aarch64_b(
                    int(expected["policy"]) + 0x188,
                    int(expected["update"]) + 0xF0,
                ) or 0,
            )
            and generated_idle_count_branch == struct.pack(
                "<I", encode_aarch64_cbz_like(
                    int(expected["policy"]) + 0x3C,
                    helper_vaddr + 0xC8,
                    0xB4000A68,
                ) or 0,
            )
        )
        concurrent_overlay_ok = (
            len(generated_concurrent_helper) == UPDATE_CONCURRENT_HELPER_BYTES
            and len(generated_concurrent_policy) == 40
            and len(generated_stream_events[0]) == 28
            and len(generated_stream_events[1]) == 28
            and len(generated_stream_events[2]) == 4
            and len(generated_stream_open) == STREAM_OPEN_EVENT_BYTES
            and generated_concurrent_helper[:0x08]
                == STREAM_OPEN_HELPER_TEMPLATE[:0x08]
            and generated_concurrent_helper[0x0C:0x10]
                == STREAM_OPEN_HELPER_TEMPLATE[0x0C:0x10]
            and generated_concurrent_helper[0x14:0x20]
                == b"\x1f\x20\x03\xd5" * 3
            and generated_concurrent_helper[0x20:0x88]
                == UPDATE_CONCURRENT_HELPER_TEMPLATE[:0x68]
            and generated_concurrent_helper[0x8C:0x90]
                == UPDATE_CONCURRENT_HELPER_TEMPLATE[0x6C:0x70]
            and generated_concurrent_helper[0x94:0xAC]
                == UPDATE_CONCURRENT_HELPER_TEMPLATE[0x74:0x8C]
            and generated_concurrent_helper[0xB0:0xC4]
                == UPDATE_CONCURRENT_HELPER_TEMPLATE[0x90:0xA4]
            and generated_concurrent_helper[0xC8:0xCC]
                == bytes.fromhex("f40300aa")
            and generated_stream_open == struct.pack(
                "<I", encode_aarch64_bl(
                    stream_open_vaddr + STREAM_OPEN_EPILOGUE_EXPECTED_OFF,
                    helper_vaddr,
                ) or 0,
            )
            and generated_concurrent_helper[0x08:0x0C] == struct.pack(
                "<I", encode_aarch64_bl(
                    helper_vaddr + 0x08, stream_update_target or 0,
                ) or 0,
            )
            and generated_concurrent_helper[0x10:0x14] == struct.pack(
                "<I", encode_aarch64_b(
                    helper_vaddr + 0x10,
                    stream_open_vaddr + STREAM_OPEN_EPILOGUE_EXPECTED_OFF +
                    STREAM_OPEN_EVENT_BYTES,
                ) or 0,
            )
            and generated_concurrent_helper[0x88:0x8C] == struct.pack(
                "<I", encode_aarch64_b(
                    helper_vaddr + 0x88, helper_vaddr + 0x20,
                ) or 0,
            )
            and generated_concurrent_helper[0x90:0x94] == struct.pack(
                "<I", encode_aarch64_b(
                    helper_vaddr + 0x90, helper_vaddr + 0x30,
                ) or 0,
            )
            and generated_concurrent_helper[0xAC:0xB0] == struct.pack(
                "<I", encode_aarch64_b(
                    helper_vaddr + 0xAC,
                    int(expected["policy"]) + 0x1CC,
                ) or 0,
            )
            and generated_concurrent_helper[0xC4:0xC8] == struct.pack(
                "<I", encode_aarch64_b(
                    helper_vaddr + 0xC4,
                    int(expected["policy"]) + 0x18C,
                ) or 0,
            )
            and generated_concurrent_helper[0xCC:0xD0] == struct.pack(
                "<I", encode_aarch64_b(
                    helper_vaddr + 0xCC,
                    int(expected["policy"]) + 0x188,
                ) or 0,
            )
            and generated_concurrent_policy[:4] == struct.pack(
                "<I", encode_aarch64_b(
                    int(expected["policy"]) + 0x1C8,
                    helper_vaddr + 0x90,
                ) or 0,
            )
            and generated_concurrent_policy[4:20]
                == GAME_POLICY_CONCURRENT_TEMPLATE[4:20]
            and generated_concurrent_policy[20:] == GAME_POLICY_STOCK[20:]
            and GAME_POLICY_RELAXED[20:] == GAME_POLICY_STOCK[20:]
            and len(GAME_POLICY_PUBLIC_RELAXED) == len(GAME_POLICY_STOCK)
            and generated_concurrent_policy != GAME_POLICY_PUBLIC_RELAXED
            and generated_stream_events[0][:24]
                == STREAM_EVENT_INLINE_TEMPLATE[:24]
            and generated_stream_events[0][24:] == struct.pack(
                "<I", encode_aarch64_b(
                    int(expected["stream"]) + 0x1FAC,
                    int(expected["stream"]) + 0x23F8,
                ) or 0,
            )
            and generated_stream_events[1][:24]
                == STREAM_EVENT_INLINE_TEMPLATE[:24]
            and generated_stream_events[1][24:] == struct.pack(
                "<I", encode_aarch64_b(
                    int(expected["stream"]) + 0x2188,
                    int(expected["stream"]) + 0x23F8,
                ) or 0,
            )
            and generated_stream_events[2] == struct.pack(
                "<I", encode_aarch64_b(
                    int(expected["stream"]) + 0x22B4,
                    int(expected["stream"]) + 0x23F8,
                ) or 0,
            )
            and generated_stream_events[2]
                == bytes.fromhex("51000014")
        )
        idle_control_flow_ok = (
            data[policy_file + 0x30 : policy_file + 0x34]
            == bytes.fromhex("083c41f9")
            and data[policy_file + 0x3C : policy_file + 0x40]
            == bytes.fromhex("680a00b4")
            and data[
                policy_file + 0x188 :
                policy_file + 0x188 + len(IDLE_CLEAR_DISK_CONTEXT)
            ] == IDLE_CLEAR_DISK_CONTEXT
        )
        stream_this_ok = (
            data[stream_file + 0x24 : stream_file + 0x28]
            == bytes.fromhex("f30300aa")
        )
        flags_member_hits = [
            offset
            for offset in range(len(data))
            if data.startswith(OUTPUT_FLAGS_MEMBER_SEQUENCE, offset)
        ]
        flags_member_ok = len(flags_member_hits) == 1

        def padding_refs(blob: bytes) -> list[tuple[int, int]]:
            refs: list[tuple[int, int]] = []
            for segment in actual["elf"]["program_headers"]:
                if int(segment["type"]) != 1 or not (int(segment["flags"]) & 1):
                    continue
                start = int(segment["offset"])
                end = min(start + int(segment["filesz"]), len(blob))
                for file_offset in range(start, end - 3, 4):
                    instruction = struct.unpack_from("<I", blob, file_offset)[0]
                    if instruction & 0x3B000000 == 0x39000000:
                        scale = (instruction >> 30) & 0x3
                        displacement = ((instruction >> 10) & 0xFFF) << scale
                        if 0x519 <= displacement <= 0x51F:
                            refs.append((file_offset, displacement))
            return refs

        padding_hits = padding_refs(data)

        regions_ok = (
            data[update_file + 8 : update_file + 12] == bytes.fromhex("f71300f9")
            and data[update_file + 0xDC : update_file + 0xFC] == UPDATE_FLAGS_STOCK
            and stock_shape and call_ok and plt_ok and allowed_ok
            and stream_update_ok and stream_update_shape
            and output_ok and manager_registers_ok
            and policy_stream_layout_ok and not padding_hits
            and eligible_overlay_ok and idle_control_flow_ok
            and concurrent_overlay_ok and tail_zero_ok
            and stream_this_ok and flags_member_ok
            and stream_event_stock_shape and strlen_ok
            and stream_ref_shape and delete_ok
            and stream_open_shape
            and data[
                policy_file + 0x1C8 :
                policy_file + 0x1C8 + len(GAME_POLICY_STOCK)
            ] == GAME_POLICY_STOCK
        )
        report.check(
            regions_ok,
            f"HAL lifecycle regions {system}",
            f"eligible-output+idle helpers, RX-tail concurrent helper=0x{helper_vaddr:x}, +0x51a latch, full policy tail, existing/new-node 28-byte app events, open post-commit and BL targets 0x{call_target:x}/{delete_target!r}/{strlen_target!r} verified",
            f"stock_shape={stock_shape} call_ok={call_ok} plt_ok={plt_ok} allowed_ok={allowed_ok} stream_update_ok={stream_update_ok} stream_update_shape={stream_update_shape} stream_event_stock_shape={stream_event_stock_shape} strlen_ok={strlen_ok} output_ok={output_ok} manager_registers_ok={manager_registers_ok} policy_stream_layout_ok={policy_stream_layout_ok} eligible_overlay_ok={eligible_overlay_ok} concurrent_overlay_ok={concurrent_overlay_ok} tail_layout={tail_layout} tail_zero_ok={tail_zero_ok} stream_open_shape={stream_open_shape} open_manager_hits={stream_open_manager_hits} idle_control_flow_ok={idle_control_flow_ok} stream_this_ok={stream_this_ok} flags_member_hits={flags_member_hits} padding_hits={padding_hits}",
        )

    source = (root / "src/patcher_v3.c").read_text(encoding="utf-8")
    source_upper = source.upper()
    missing_offsets = [
        system
        for system, item in HAL_CASES.items()
        if f"0X{int(item['symbol']):X}" not in source_upper
    ]
    report.check(not missing_offsets, "profile fixture offsets", "all archived offsets appear in patcher profiles", f"missing offsets for {missing_offsets}")
    lifecycle_contracts = (
        "#define UPDATE_APP_POLICY_BYTES 72u" in source
        and "build_update_app_policy_overlay" in source
        and "exact_aarch64_plt_entry" in source
        and "g_auxiliary.app_policy_stock" in source
        and "g_auxiliary.app_policy_relaxed" in source
        and "#define STREAM_REF_PATCH_BYTES 148u" in source
        and "build_stream_ref_overlay" in source
        and "g_auxiliary.stream_ref_patched" in source
        and "HANDOFF_FLAG_OFF 0x519u" in source
        and "OUTPUT_POOL_TAIL_PATCH_OFF 0x278u" in source
        and "STREAM_APP_MAP_MANAGER_OFF 0x1F58u" in source
        and "OUTPUT_POOL_MANAGER_ARG_OFF 0x38u" in source
        and "STREAM_UPDATE_CALL_BYTES 12u" in source
        and "g_auxiliary.output_pool_tail_patched" in source
        and "output_pool_tail_patched" in source
        and "STREAM_EVENT_PATCH_COUNT 3u" in source
        and "STREAM_EVENT_PATCH_MAX_BYTES 28u" in source
        and "STREAM_EVENT_PATCH_SIZES" in source
        and "0x1F94u,0x2170u,0x22B4u" in source
        and "coherent_concurrent_generation" in source
        and "int relocated_64 =" in source
        and "int relocated_56 =" in source
        and "CONCURRENT_APP_FLAG_OFF 0x51Au" in source
        and "UPDATE_CONCURRENT_HELPER_BYTES 208u" in source
        and "UPDATE_CONCURRENT_PREVIOUS_BYTES 176u" in source
        and "UPDATE_CONCURRENT_PREVIOUS2_BYTES 160u" in source
        and "concurrent_helper_previous" in source
        and "previous-160" in source
        and "CONCURRENT_ENTRY_B_OFF 0x90u" in source
        and "CONCURRENT_ZERO_INIT_OFF 0xC8u" in source
        and "CONCURRENT_ZERO_INIT_RETURN_B_OFF 0xCCu" in source
        and "POLICY_SLOT_TABLE_LOAD_OFF 0x78u" in source
        and "POLICY_DEVICE_BEGIN_LOAD_OFF 0x84u" in source
        and "POLICY_DEVICE_END_LOAD_OFF 0x8Cu" in source
        and "POLICY_IDLE_COUNT_CBZ_OFF 0x3Cu" in source
        and "g_auxiliary.idle_count_branch" in source
        and "legacy-64" in source
        and "legacy-56" in source
        and "UPDATE_CONCURRENT_HELPER_OFF" not in source
        and "derive_executable_tail_layout" in source
        and "prepare_executable_concurrent_helper" in source
        and "locate_update_layout" in source
        and "locate_policy_layout" in source
        and "locate_stream_layout" in source
        and "locate_output_layout" in source
        and "semantic_hits=%u" in source
        and "g_auxiliary.concurrent_helper_off" in source
        and "rx-tail.concurrent-helper" in source
        and "GAME_POLICY_PATCH_BYTES 40u" in source
        and "stream_event_strlen_target" in source
        and "exact_aarch64_plt_entry(pid, base," in source
        and "g_auxiliary.concurrent_helper_patched" in source
        and "g_auxiliary.policy_concurrent" in source
        and "g_auxiliary.stream_event_recompute_legacy" in source
        and "g_auxiliary.stream_event_handoff_legacy" in source
        and "STREAM_OPEN_EPILOGUE_EXPECTED_OFF 0x9ECu" in source
        and "STREAM_OPEN_HELPER_TEMPLATE" in source
        and "stream-open-post-commit" in source
        and "stream_open_patched" in source
        and "UPDATE_FLAGS_GAME_HANDOFF_LEGACY" in source
        and "build_idle_clear_overlay" in source
        and "IDLE_CLEAR_BRANCH_PATCH_OFF 0x188u" in source
        and "UPDATE_IDLE_GUARD_OFF 0xECu" in source
        and "UPDATE_IDLE_HELPER_OFF 0xF0u" in source
        and "g_auxiliary.update_flags_patched" in source
        and "g_auxiliary.idle_clear_branch" in source
        and "STREAM_REF_FLAGS_LOAD_OFF 0x44u" in source
        and "STREAM_REF_STORE_OFF 0x90u" in source
        and "output_pool_tail_persistent_legacy" in source
        and "#define ELF_SYMBOL_MAX_BYTES 16384u" in source
    )
    report.check(
        lifecycle_contracts,
        "cross-ROM game lifecycle ownership",
        "72-byte app policy, guarded idle clear, unique runtime semantic layouts, dynamic RX-tail 208-byte open-plus-pending/committed real-stream helper with exact previous-176/160 and 64/56-byte migration, full 40-byte policy, two 28-byte app events, one recompute event and open post-commit event present",
        "required lifecycle ownership markers are missing",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--ndk", type=Path)
    parser.add_argument("--adb", action="store_true", help="run Android /system/bin/sh syntax checks")
    parser.add_argument("--strict-gaps", action="store_true")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    root = args.repo.resolve()
    archive = (args.archive or (root / "compatibility_archive")).resolve()
    report = Report()
    check_release_tree(root, report, args.adb)
    check_companion(root, report)
    check_lock_protocol(root, report, args.adb)
    check_service_singleton_lock(root, report, args.adb)
    check_config_hotupdate(root, report, args.adb)
    check_control_transaction(root, report, args.adb)
    check_policy_refresh_transaction(root, report, args.adb)
    check_revision_metadata_repair(root, report, args.adb)
    check_last_pid_write_failure(root, report, args.adb)
    check_boot_last_pid_failure(root, report, args.adb)
    check_config_wake_fifo(root, report, args.adb)
    check_postfs_runtime_cleanup(root, report, args.adb)
    check_audio_uid_watcher(root, report, args.adb)
    check_audio_policy_lease(root, report, args.adb)
    check_webui_writer_transaction(root, report, args.adb)
    check_apply_capability_runtime(root, report, args.adb)
    check_source_contracts(root, report)
    check_ndk(root, report, args.ndk)
    check_hal_fixtures(root, report, archive)

    counts = {status: sum(item.status == status for item in report.results) for status in ("PASS", "FAIL", "GAP")}
    print(f"SUMMARY pass={counts['PASS']} fail={counts['FAIL']} gap={counts['GAP']}")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps({"counts": counts, "results": [asdict(item) for item in report.results]}, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return report.exit_code(args.strict_gaps)


if __name__ == "__main__":
    raise SystemExit(main())
