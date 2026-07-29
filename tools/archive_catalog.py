#!/usr/bin/env python3
"""Build a searchable catalog from compatibility archive record manifests."""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


CATALOG_NAME = "catalog.json"
IGNORED_ROOT_FILES = {"CHECKSUMS.sha256", CATALOG_NAME, "README.md"}
EVIDENCE_LEVELS = {
    "runtime-verified",
    "static-verified",
    "log-only",
    "unassigned",
    "artifact-verified",
}


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"manifest is not an object: {path}")
    required = ("schema_version", "record_id", "kind", "evidence_level")
    missing = [key for key in required if key not in value]
    if missing:
        raise ValueError(f"manifest missing {', '.join(missing)}: {path}")
    if value["schema_version"] != 1:
        raise ValueError(f"unsupported schema_version in {path}")
    if value["evidence_level"] not in EVIDENCE_LEVELS:
        raise ValueError(f"invalid evidence_level in {path}")
    return value


def record_files(record_dir: Path) -> list[dict[str, Any]]:
    files = []
    for path in sorted(
        (item for item in record_dir.rglob("*") if item.is_file()),
        key=lambda item: item.relative_to(record_dir).as_posix(),
    ):
        files.append(
            {
                "path": path.relative_to(record_dir).as_posix(),
                "size": path.stat().st_size,
                "sha256": digest(path),
            }
        )
    return files


def discover_manifests(root: Path) -> list[Path]:
    manifests = []
    for branch in ("devices", "unassigned", "module_releases"):
        branch_path = root / branch
        if branch_path.is_dir():
            manifests.extend(branch_path.rglob("manifest.json"))
    return sorted(manifests, key=lambda path: path.relative_to(root).as_posix())


def build_catalog(root: Path) -> dict[str, Any]:
    manifests = discover_manifests(root)
    if not manifests:
        raise ValueError("no record manifests found")
    records = []
    ids: set[str] = set()
    covered: set[Path] = set()
    for path in manifests:
        manifest = load_manifest(path)
        record_id = str(manifest["record_id"])
        if record_id in ids:
            raise ValueError(f"duplicate record_id: {record_id}")
        ids.add(record_id)
        record_dir = path.parent
        files = record_files(record_dir)
        covered.update(item.resolve() for item in record_dir.rglob("*") if item.is_file())
        records.append(
            {
                "record_path": record_dir.relative_to(root).as_posix(),
                "record_id": record_id,
                "kind": manifest["kind"],
                "evidence_level": manifest["evidence_level"],
                "summary": manifest.get("summary", ""),
                "files": files,
            }
        )

    allowed_root = {root / name for name in IGNORED_ROOT_FILES}
    unowned = [
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file() and path.resolve() not in covered and path not in allowed_root
    ]
    if unowned:
        raise ValueError("files outside a record: " + ", ".join(sorted(unowned)))

    return {
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "record_count": len(records),
        "file_count": sum(len(record["files"]) for record in records),
        "total_bytes": sum(
            item["size"] for record in records for item in record["files"]
        ),
        "records": records,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive_root", type=Path)
    args = parser.parse_args()
    root = args.archive_root.resolve(strict=True)
    if not root.is_dir():
        parser.error("archive_root must be a directory")
    try:
        catalog = build_catalog(root)
    except ValueError as exc:
        parser.error(str(exc))
    output = root / CATALOG_NAME
    output.write_text(
        json.dumps(catalog, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(
        f"wrote {CATALOG_NAME}: {catalog['record_count']} records, "
        f"{catalog['file_count']} files"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
