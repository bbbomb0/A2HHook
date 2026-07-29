#!/usr/bin/env python3
"""Create a compatibility archive case without overwriting existing evidence."""

from __future__ import annotations

import argparse
import json
import re
from datetime import date
from pathlib import Path


EVIDENCE_LEVELS = ("runtime-verified", "static-verified", "log-only", "unassigned")
SAFE_COMPONENT = re.compile(r"^[A-Za-z0-9._-]+$")


def component(value: str) -> str:
    if not SAFE_COMPONENT.fullmatch(value):
        raise argparse.ArgumentTypeError(
            "path components may contain only letters, digits, dot, underscore, and hyphen"
        )
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive_root", type=Path)
    parser.add_argument("--model", required=True, type=component)
    parser.add_argument("--device", required=True, type=component)
    parser.add_argument("--rom", required=True, type=component)
    parser.add_argument("--evidence-level", required=True, choices=EVIDENCE_LEVELS)
    args = parser.parse_args()

    root = args.archive_root.resolve()
    case = root / "devices" / f"{args.model}__{args.device}" / args.rom
    if case.exists():
        parser.error(f"case already exists: {case}")

    for relative in (
        "originals/hal",
        "originals/logs",
        "originals/runtime",
        "derived",
    ):
        (case / relative).mkdir(parents=True, exist_ok=False)

    manifest = {
        "schema_version": 1,
        "record_id": f"{args.model}__{args.device}__{args.rom}",
        "kind": "device-rom",
        "evidence_level": args.evidence_level,
        "device": {"model": args.model, "device": args.device},
        "rom": {"full_version": args.rom},
        "created_on": date.today().isoformat(),
        "source_aliases": [],
        "known_gaps": [],
    }
    (case / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    (case / "evidence.md").write_text(
        "# Evidence\n\n"
        f"- Device: `{args.model}` / `{args.device}`\n"
        f"- ROM: `{args.rom}`\n"
        f"- Evidence level: `{args.evidence_level}`\n\n"
        "## Provenance\n\n"
        "Record who supplied each file and whether it came from the mapped runtime path.\n\n"
        "## Verification\n\n"
        "Keep static analysis, log results, and observed device behavior as separate facts.\n\n"
        "## Known Gaps\n\n"
        "List every missing artifact or unverified behavior.\n",
        encoding="utf-8",
        newline="\n",
    )
    print(case)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
