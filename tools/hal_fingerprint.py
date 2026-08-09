#!/usr/bin/env python3
"""Extract stable A2H-relevant fingerprints from an AArch64 ELF HAL."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIQQQQQQ")
SECTION_HEADER = struct.Struct("<IIQQQQIIQQ")
SYMBOL = struct.Struct("<IBBHQQ")

PT_LOAD = 1
PT_NOTE = 4
PF_X = 1
PF_W = 2
SHT_SYMTAB = 2
SHT_DYNSYM = 11

STOCK_SIG8 = bytes.fromhex("e00400b4fd7bbea9")
GLOBAL_PATCH8 = bytes.fromhex("20008052c0035fd6")
WHITELIST_TAIL_WORDS = (
    0xB40001E0,
    0x52800142,
    0x340001A2,
    0xF8408423,
    0xB4000123,
    0xAA0003E4,
    0x38401485,
    0x38401466,
    0x6B0600BF,
    0x54000081,
    0x35FFFF85,
    0x52800020,
    0xD65F03C0,
    0x51000442,
    0x17FFFFF4,
    0x52800000,
    0xD65F03C0,
)

OFFICIAL_PACKAGES = (
    "com.kugou.android",
    "com.tencent.qqmusic",
    "com.netease.cloudmusic",
    "cn.kuwo.player",
    "com.miui.player",
    "com.luna.music",
)

LIFECYCLE_SYMBOLS = {
    "is_A2H_app": "is_A2H_app",
    "updateA2HMode": "_ZN7android22AudioALSAStreamManager13updateA2HModeEv",
    "stream_setParameters": "_ZN7android18AudioALSAStreamOut13setParametersERKNS_7String8E",
    "isA2HAllowed": "_ZN7android22AudioALSAStreamManager12isA2HAllowedEv",
    "updateOutputPoolActive": "_ZN7android22AudioALSAStreamManager22updateOutputPoolActiveE20audio_output_flags_tb",
    "streamAttributeDevices": "_ZN7android24isEarphoneWithDualDeviceEPK18stream_attribute_t",
    "stream_open": "_ZN7android18AudioALSAStreamOut4openEv",
}


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def all_offsets(data: bytes, needle: bytes, step: int = 1) -> list[int]:
    hits: list[int] = []
    start = 0
    while True:
        found = data.find(needle, start)
        if found < 0:
            return hits
        if found % step == 0:
            hits.append(found)
        start = found + 1


def c_string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        return ""
    end = data.find(b"\0", offset)
    if end < 0:
        end = len(data)
    return data[offset:end].decode("utf-8", errors="replace")


def parse_notes(blob: bytes) -> list[dict[str, Any]]:
    notes: list[dict[str, Any]] = []
    offset = 0
    while offset + 12 <= len(blob):
        namesz, descsz, note_type = struct.unpack_from("<III", blob, offset)
        offset += 12
        name_end = offset + namesz
        if name_end > len(blob):
            break
        name = blob[offset:name_end].rstrip(b"\0")
        offset = align_up(name_end, 4)
        desc_end = offset + descsz
        if desc_end > len(blob):
            break
        desc = blob[offset:desc_end]
        offset = align_up(desc_end, 4)
        notes.append(
            {
                "name": name.decode("ascii", errors="replace"),
                "type": note_type,
                "description_hex": desc.hex(),
            }
        )
    return notes


def vaddr_to_offset(loads: list[dict[str, int]], address: int) -> int | None:
    for segment in loads:
        start = segment["vaddr"]
        end = start + segment["filesz"]
        if start <= address < end:
            return segment["offset"] + address - start
    return None


def stub_head(data: bytes, offset: int) -> bool:
    if offset < 0 or offset + 12 > len(data):
        return False
    word0, word1, word2 = struct.unpack_from("<III", data, offset)
    if (word0 & 0x9F00001F) != 0x90000001:
        return False
    if (word1 & 0xFFC003FF) != 0x91000021:
        return False
    if word2 == 0x52800142:
        return True
    if offset + 16 <= len(data):
        word3 = struct.unpack_from("<I", data, offset + 12)[0]
        return (word2 & 0xFF00001F) == 0xB4000000 and word3 == 0x52800142
    return False


def exact_stub(data: bytes, offset: int) -> bool:
    size = 19 * 4
    if offset < 0 or offset + size > len(data) or not stub_head(data, offset):
        return False
    words = struct.unpack_from("<19I", data, offset)
    return tuple(words[2:]) == WHITELIST_TAIL_WORDS


def scan_executable_signatures(
    data: bytes, loads: list[dict[str, int]]
) -> dict[str, list[str]]:
    stock: list[str] = []
    global_patch: list[str] = []
    stub: list[str] = []
    exact: list[str] = []
    for segment in loads:
        if not segment["flags"] & PF_X:
            continue
        file_start = segment["offset"]
        file_end = min(file_start + segment["filesz"], len(data))
        for file_offset in range(file_start, max(file_start, file_end - 7), 4):
            relative = segment["vaddr"] + file_offset - file_start
            if data[file_offset : file_offset + 8] == STOCK_SIG8:
                stock.append(f"0x{relative:x}")
            if data[file_offset : file_offset + 8] == GLOBAL_PATCH8:
                global_patch.append(f"0x{relative:x}")
            if stub_head(data, file_offset):
                stub.append(f"0x{relative:x}")
                if exact_stub(data, file_offset):
                    exact.append(f"0x{relative:x}")
    return {
        "stock_sig8": stock,
        "global_patch8": global_patch,
        "whitelist_stub_head": stub,
        "whitelist_exact_stub_76b": exact,
    }


def parse_elf(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < ELF_HEADER.size:
        raise ValueError("file is smaller than an ELF64 header")
    header = ELF_HEADER.unpack_from(data)
    ident = header[0]
    if ident[:4] != b"\x7fELF" or ident[4] != 2 or ident[5] != 1:
        raise ValueError("expected little-endian ELF64")

    machine = header[2]
    phoff, shoff = header[5], header[6]
    phentsize, phnum = header[9], header[10]
    shentsize, shnum, shstrndx = header[11], header[12], header[13]
    if phentsize != PROGRAM_HEADER.size:
        raise ValueError(f"unexpected program header size: {phentsize}")

    program_headers: list[dict[str, int]] = []
    loads: list[dict[str, int]] = []
    notes: list[dict[str, Any]] = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        if offset + PROGRAM_HEADER.size > len(data):
            raise ValueError("program headers extend past EOF")
        values = PROGRAM_HEADER.unpack_from(data, offset)
        item = {
            "index": index,
            "type": values[0],
            "flags": values[1],
            "offset": values[2],
            "vaddr": values[3],
            "filesz": values[5],
            "memsz": values[6],
            "align": values[7],
        }
        program_headers.append(item)
        if item["type"] == PT_LOAD:
            loads.append(item)
        if item["type"] == PT_NOTE:
            start = item["offset"]
            end = min(start + item["filesz"], len(data))
            notes.extend(parse_notes(data[start:end]))

    sections: list[dict[str, int | str]] = []
    if shoff and shnum and shentsize == SECTION_HEADER.size:
        raw_sections = []
        for index in range(shnum):
            offset = shoff + index * shentsize
            if offset + SECTION_HEADER.size > len(data):
                break
            raw_sections.append(SECTION_HEADER.unpack_from(data, offset))
        shstr = b""
        if 0 <= shstrndx < len(raw_sections):
            entry = raw_sections[shstrndx]
            shstr = data[entry[4] : entry[4] + entry[5]]
        for index, entry in enumerate(raw_sections):
            sections.append(
                {
                    "index": index,
                    "name": c_string(shstr, entry[0]),
                    "type": entry[1],
                    "flags": entry[2],
                    "address": entry[3],
                    "offset": entry[4],
                    "size": entry[5],
                    "link": entry[6],
                    "entry_size": entry[9],
                }
            )

    wanted_symbols = {name: key for key, name in LIFECYCLE_SYMBOLS.items()}
    symbol_values: dict[str, tuple[int, int]] = {}
    for section in sections:
        if section["type"] not in (SHT_SYMTAB, SHT_DYNSYM):
            continue
        link = int(section["link"])
        if not 0 <= link < len(sections):
            continue
        string_section = sections[link]
        strings = data[
            int(string_section["offset"]) : int(string_section["offset"])
            + int(string_section["size"])
        ]
        entry_size = int(section["entry_size"]) or SYMBOL.size
        if entry_size < SYMBOL.size:
            continue
        start = int(section["offset"])
        end = min(start + int(section["size"]), len(data))
        for offset in range(start, end - SYMBOL.size + 1, entry_size):
            symbol = SYMBOL.unpack_from(data, offset)
            key = wanted_symbols.get(c_string(strings, symbol[0]))
            if key is not None and key not in symbol_values:
                symbol_values[key] = (symbol[4], symbol[5])
        if len(symbol_values) == len(LIFECYCLE_SYMBOLS):
            break

    writable_loads = [item for item in loads if item["flags"] & PF_W and item["memsz"]]
    tail: dict[str, Any] | None = None
    if writable_loads:
        final_writable = max(writable_loads, key=lambda item: item["vaddr"] + item["memsz"])
        declared_end = final_writable["vaddr"] + final_writable["memsz"]
        tail_start = align_up(declared_end, 16)
        tail_end = align_up(declared_end, 4096)
        available = max(0, tail_end - tail_start)
        need = 0x300
        candidate = (tail_end - need) & ~0xF if available >= need else None
        tail = {
            "writable_program_header_index": final_writable["index"],
            "segment_vaddr": f"0x{final_writable['vaddr']:x}",
            "segment_filesz": f"0x{final_writable['filesz']:x}",
            "segment_memsz": f"0x{final_writable['memsz']:x}",
            "declared_end": f"0x{declared_end:x}",
            "safe_tail_start_align16": f"0x{tail_start:x}",
            "safe_tail_end_page4096": f"0x{tail_end:x}",
            "safe_tail_bytes": available,
            "a2h_layout_bytes": need,
            "candidate": f"0x{candidate:x}" if candidate is not None else None,
        }

    build_id = None
    for note in notes:
        if note["name"] == "GNU" and note["type"] == 3:
            build_id = note["description_hex"]
            break

    package_offsets = {
        package: [f"0x{offset:x}" for offset in all_offsets(data, package.encode() + b"\0")]
        for package in OFFICIAL_PACKAGES
    }
    signatures = scan_executable_signatures(data, loads)

    symbol: dict[str, Any] | None = None
    symbol_value, symbol_size = symbol_values.get("is_A2H_app", (None, None))
    if symbol_value is not None:
        file_offset = vaddr_to_offset(loads, symbol_value)
        head = ""
        state = "unknown"
        if file_offset is not None:
            head = data[file_offset : file_offset + 32].hex()
            if data[file_offset : file_offset + 8] == STOCK_SIG8:
                state = "stock"
            elif data[file_offset : file_offset + 8] == GLOBAL_PATCH8:
                state = "global-patched"
            elif exact_stub(data, file_offset):
                state = "whitelist-exact-stub"
            elif stub_head(data, file_offset):
                state = "whitelist-stub-head"
        symbol = {
            "value": f"0x{symbol_value:x}",
            "size": symbol_size,
            "file_offset": f"0x{file_offset:x}" if file_offset is not None else None,
            "head_32_bytes_hex": head,
            "state": state,
        }

    lifecycle_symbols: dict[str, Any] = {}
    for key, (value, size) in symbol_values.items():
        file_offset = vaddr_to_offset(loads, value)
        lifecycle_symbols[key] = {
            "value": f"0x{value:x}",
            "size": size,
            "file_offset": f"0x{file_offset:x}" if file_offset is not None else None,
            "head_32_bytes_hex": (
                data[file_offset : file_offset + 32].hex()
                if file_offset is not None else ""
            ),
        }

    return {
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "file": {
            "name": path.name,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        },
        "elf": {
            "class": "ELF64",
            "endianness": "little",
            "machine": machine,
            "machine_name": "AArch64" if machine == 183 else f"unknown-{machine}",
            "gnu_build_id": build_id,
            "program_headers": program_headers,
            "a2h_safe_tail": tail,
        },
        "a2h": {
            "is_A2H_app_symbol": symbol,
            "lifecycle_symbols": lifecycle_symbols,
            "executable_signature_hits": signatures,
            "official_package_string_offsets": package_offsets,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = parse_elf(args.elf.resolve(strict=True))
    except (OSError, ValueError, struct.error) as exc:
        parser.error(str(exc))
    payload = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload, encoding="utf-8", newline="\n")
    else:
        print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
