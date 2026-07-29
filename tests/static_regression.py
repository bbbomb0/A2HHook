#!/usr/bin/env python3
"""Run non-destructive source, fixture, and release-tree regression checks."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
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

EXPECTED_RELEASE = ("v1.5.5-fix2", "1552")

HAL_CASES = {
    "OS2.0.218.0.VONCNXM": {
        "relative": "devices/25060RK16C__dali/OS2.0.218.0.VONCNXM/originals/hal/audio.primary.mediatek(os2.0.218).so",
        "sha256": "ba543c1fd331d20ca149ae38c80600acc331d7a3d09bad6582ca485163cf9d13",
        "size": 4454128,
        "build_id": "b83078b3502a148fb846652aa3bf1f7d",
        "symbol": 0x3E4280,
        "symbol_size": 160,
    },
    "OS3.0.302.0.WONCNXM": {
        "relative": "devices/25060RK16C__dali/OS3.0.302.0.WONCNXM/originals/hal/audio.primary.mediatek.so",
        "sha256": "fe48c8d2070a318ef4ab1f6d05a37c213e0b2abc5a95d910885ab1decc468924",
        "size": 4454112,
        "build_id": "fb4737aed1aed36ca755604fad655461",
        "symbol": 0x3E3FC0,
        "symbol_size": 160,
    },
    "OS3.0.305.0.WONCNXM": {
        "relative": "devices/25060RK16C__dali/OS3.0.305.0.WONCNXM/originals/hal/audio.primary.mediatek.so",
        "sha256": "da028039d1a823189ed0c3df3ab80bfd5d52a9849038f32ef5ac5e229b2af6fa",
        "size": 4454072,
        "build_id": "ad3569054ba54d7178354e85fbad3ce6",
        "symbol": 0x3E4020,
        "symbol_size": 160,
    },
}

TEXT_RELEASE_FILES = (
    "module.prop",
    "customize.sh",
    "service.sh",
    "post-fs-data.sh",
    "wrapper.sh",
    "bin/a2h_apply",
    "config/packages.txt",
    "config/package_states",
    "config/state",
    "webroot/index.html",
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


def run_command(command: list[str], cwd: Path, input_bytes: bytes | None = None) -> tuple[int, str]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    return completed.returncode, completed.stdout.decode("utf-8", errors="replace")


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
    report.check(
        version in webui,
        "WebUI version metadata",
        f"WebUI contains {version}",
        f"WebUI does not contain {version}",
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
        and "setTimeout" not in request_apply
        and "drainApply();" in request_apply
        and "window[callbackName]=" in raw_bridge
        and "command,'{}',callbackName" in raw_bridge
        and "verifyDeviceConfig(text,data,writePackages);" in apply_loop
        and "loadDeviceConfig({preserveNotice:true})" in apply_loop
        and "state!=='enabled'&&state!=='disabled'" in webui
    )
    report.check(
        state_contract,
        "WebUI device-state persistence contract",
        "immediate dispatch, string callback, strict readback, and failure restore present",
        "mode persistence safeguards are incomplete or page debounce returned",
    )
    writer = extract_function(
        webui, "function buildWritePackagesCmd(snapshot,readable){", "function buildCommand("
    )
    packages_commit = writer.find('mv -f "$pkg_tmp" "$cfg/packages.txt"')
    states_commit = writer.find('mv -f "$state_tmp" "$cfg/package_states"')
    generation_commit = writer.find('mv -f "$gen_tmp" "$cfg/config_generation"')
    rollback_contract = (
        'cfg_backed_up=0' in writer
        and 'cfg_committed=0' in writer
        and 'cfg_restore()' in writer
        and 'cfg_signal_abort()' in writer
        and 'cfg_backed_up=1' in writer
        and 'cfg_committed=1' in writer
        and 'trap - 0 1 2 15; cfg_abort; exit 130' in writer
        and 'cfg_wait=$((cfg_wait + 1)); [ "$cfg_wait" -le 8 ]' in writer
        and 'execRoot(command,15000)' in webui
    )
    report.check(
        0 <= packages_commit < states_commit < generation_commit,
        "WebUI grouped commit marker order",
        "packages and states commit before generation",
        f"commit indexes packages={packages_commit} states={states_commit} generation={generation_commit}",
    )
    report.check(
        rollback_contract,
        "WebUI grouped commit rollback contract",
        "old files are restored on failure/signal and lock waiting is bounded below the bridge timeout",
        "backup, rollback, signal exit, bounded wait, or timeout headroom is incomplete",
    )

    service = (root / "service.sh").read_text(encoding="utf-8")
    watcher = extract_function(service, "last_pid=$(cat", "done\n")
    watcher_contract = (
        "watch_tick_seconds=2" in watcher
        and "watch_stable_ticks=2" in watcher
        and "watch_health_ticks=15" in watcher
        and "applier_busy" in watcher
        and "snapshot-state" in watcher
        and "changed across snapshot" in watcher
        and "CONFIG_EVENT_MARKER" in service
        and 'state|packages.txt|package_states|config_generation)' in service
        and "inotifyd" in service
        and "signature_needed" in watcher
        and "sleep 25" not in watcher
        and "Never let the slower health path" in watcher
    )
    report.check(
        watcher_contract,
        "file-manager watcher latency contract",
        "2-second debounce is separated from 30-second HAL health checks",
        "fast config debounce or slow health isolation is incomplete",
    )

    applier = (root / "bin/a2h_apply").read_text(encoding="utf-8")
    report.check(
        'queue worker retained pending request after failure' in applier
        and 'release_worker_lock\n      if [ -f "$PENDING_FILE" ] && claim_worker_lock; then' in applier,
        "queue worker failure handoff",
        "a request arriving during a failed apply is reclaimed or left for a new worker",
        "failed worker can release its lock while leaving an unconsumed pending request",
    )

    packager = (root / "package_module.py").read_text(encoding="utf-8")
    forbidden = ("tests/", "zygisk/", "a2h_hook.so", "a2h_inject")
    manifest_area = extract_function(packager, "FILES = (", "EXECUTABLE =")
    leaked = [item for item in forbidden if item in manifest_area]
    report.check(not leaked, "release manifest isolation", "tests and legacy injection excluded", f"forbidden manifest entries: {leaked}")

    installer = (root / "customize.sh").read_text(encoding="utf-8")
    report.check(
        '[ -f "$MODDIR/config/.package_baseline" ] &&' in installer
        and 'wc -l < "$MODDIR/config/.package_baseline"' in installer,
        "clean-install baseline guard",
        "installer checks baseline existence before input redirection",
        "first install can emit a missing .package_baseline redirection error",
    )

    scripts = ["customize.sh", "service.sh", "post-fs-data.sh", "wrapper.sh", "bin/a2h_apply"]
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
        with tempfile.NamedTemporaryFile("w", suffix=".js", encoding="utf-8", newline="\n", delete=False) as stream:
            stream.write("\n".join(scripts_found))
            temporary = Path(stream.name)
        try:
            rc, output = run_command([node, "--check", str(temporary)], root)
            report.check(rc == 0, "WebUI JavaScript syntax", "node --check passed", output.strip())
        finally:
            temporary.unlink(missing_ok=True)


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
        "cfg_proc_start()" in webui and 'cfg_token="$$ $cfg_start"' in webui and
        '[ "$cfg_owner" = "$cfg_token" ]' in webui,
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


def check_webui_writer_transaction(root: Path, report: Report, use_adb: bool) -> None:
    node = shutil.which("node")
    if not node:
        report.gap("WebUI writer transaction regression", "node is required to generate the production writer command")
        return

    webui = (root / "webroot/index.html").read_text(encoding="utf-8")
    normalizer = extract_function(webui, "function normalizeSlot", "function cloneSlots")
    quoter = extract_function(webui, "function shellQuote", "function snapshotConfig")
    writer = extract_function(
        webui, "function buildWritePackagesCmd(snapshot,readable){", "function buildCommand("
    )
    builder = extract_function(webui, "function buildCommand(", "function findExecBridge")
    if not normalizer or not quoter or not writer or not builder:
        report.add("FAIL", "WebUI writer transaction regression", "production writer functions could not be extracted")
        return

    snapshot = {
        "packages": [*OFFICIAL, "com.example.slot7", "com.example.slot8", "", ""],
        "enabled": [True, True, True, True, True, True, True, True, False, False],
        "generation": "4242",
    }
    javascript = "\n".join(
        (
            "'use strict';",
            normalizer,
            quoter,
            writer,
            f"const snapshot={json.dumps(snapshot, separators=(',', ':'))};",
            "process.stdout.write(buildWritePackagesCmd(snapshot,true));",
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

    writer_command = writer_command.replace(
        "cfg=/data/adb/modules/a2h_hook/config", 'cfg="$base/config"'
    ).replace("cfg_lock=/data/local/tmp/a2h_config.lock", 'cfg_lock="$base/lock"')
    combined_command = combined_command.replace(
        "cfg=/data/adb/modules/a2h_hook/config", 'cfg="$base/config"'
    ).replace(
        "cfg_lock=/data/local/tmp/a2h_config.lock", 'cfg_lock="$base/lock"'
    ).replace(
        "sh /data/adb/modules/a2h_hook/bin/a2h_apply queue", 'sh "$base/a2h_apply" queue'
    )
    harness = r'''
set -u
base=__WRITER_TEST_BASE__/a2h_webui_writer_$$
cleanup() { rm -rf "$base"; }
trap cleanup 0 1 2 15
mkdir -p "$base/config"
printf 'old packages\n' > "$base/config/packages.txt"
printf 'old states\n' > "$base/config/package_states"
printf '41\n' > "$base/config/config_generation"
old_packages=$(cksum < "$base/config/packages.txt")
old_states=$(cksum < "$base/config/package_states")
old_generation=$(cksum < "$base/config/config_generation")

writer_mv_count=0
A2H_FAIL_MV=2
mv() {
  writer_mv_count=$((writer_mv_count + 1))
  [ "$writer_mv_count" != "$A2H_FAIL_MV" ] || return 70
  command mv "$@"
}
__WRITER_COMMAND__
writer_rc=$?
[ "$writer_rc" -ne 0 ] || { printf 'FAIL: injected writer failure returned success\n'; exit 1; }
[ "$(cksum < "$base/config/packages.txt")" = "$old_packages" ] || { printf 'FAIL: packages rollback\n'; exit 1; }
[ "$(cksum < "$base/config/package_states")" = "$old_states" ] || { printf 'FAIL: states rollback\n'; exit 1; }
[ "$(cksum < "$base/config/config_generation")" = "$old_generation" ] || { printf 'FAIL: generation rollback\n'; exit 1; }
[ ! -e "$base/lock" ] || { printf 'FAIL: writer lock leaked after rollback\n'; exit 1; }

printf '%s\n' '#!/system/bin/sh' 'printf "%s\n" "$*" > "$A2H_TEST_BASE/queue_called"' > "$base/a2h_apply"
export A2H_TEST_BASE="$base"
rm -f "$base/queue_called"
writer_mv_count=0
A2H_FAIL_MV=2
__COMBINED_COMMAND__
combined_rc=$?
[ "$combined_rc" -ne 0 ] || { printf 'FAIL: combined writer failure returned success\n'; exit 1; }
[ ! -e "$base/queue_called" ] || { printf 'FAIL: queue ran after writer rollback\n'; exit 1; }
[ "$(cksum < "$base/config/packages.txt")" = "$old_packages" ] || { printf 'FAIL: combined packages rollback\n'; exit 1; }
[ "$(cksum < "$base/config/package_states")" = "$old_states" ] || { printf 'FAIL: combined states rollback\n'; exit 1; }
[ "$(cksum < "$base/config/config_generation")" = "$old_generation" ] || { printf 'FAIL: combined generation rollback\n'; exit 1; }
[ ! -e "$base/lock" ] || { printf 'FAIL: combined writer lock leaked after rollback\n'; exit 1; }

writer_mv_count=0
A2H_FAIL_MV=0
__COMBINED_COMMAND__
[ "$?" -eq 0 ] || { printf 'FAIL: combined writer success path\n'; exit 1; }
[ "$(cat "$base/queue_called")" = "queue disabled" ] || { printf 'FAIL: queue missing after writer commit\n'; exit 1; }
[ "$(wc -l < "$base/config/packages.txt" | tr -d ' ')" = 10 ] || { printf 'FAIL: package line count\n'; exit 1; }
[ "$(sed -n '8p' "$base/config/packages.txt")" = com.example.slot8 ] || { printf 'FAIL: slot 8 package\n'; exit 1; }
[ "$(sed -n '8p' "$base/config/package_states")" = 1 ] || { printf 'FAIL: slot 8 state\n'; exit 1; }
[ "$(cat "$base/config/config_generation")" = 4242 ] || { printf 'FAIL: generation commit\n'; exit 1; }
[ ! -e "$base/lock" ] || { printf 'FAIL: writer lock leaked after commit\n'; exit 1; }
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
        shell = shutil.which("sh")
        if not shell:
            report.gap("WebUI writer transaction regression", "rerun with --adb or on a POSIX host")
            return
        command = [shell, "-s"]
        test_base = "${TMPDIR:-/tmp}"

    payload = harness.replace("__WRITER_TEST_BASE__", test_base).replace(
        "__WRITER_COMMAND__", writer_command
    ).replace("__COMBINED_COMMAND__", combined_command).encode("utf-8")
    rc, output = run_command(command, root, payload)
    report.check(
        rc == 0 and "PASS WebUI writer/apply transaction regression" in output,
        "WebUI writer/apply transaction regression",
        "production writer rolls back partial commits, gates queue on success, and commits all three files",
        output.strip(),
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
    outer_rollback = (
        tx_begin >= 0
        and tx_begin < apply_strings < install_stub < tx_restore
        and re.search(
            r"if\s*\(\s*!final_ok\s*\)\s*"
            r"rollback_ok\s*=\s*whitelist_transaction_restore\s*\(",
            main_body,
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
        matched = (
            actual["file"]["sha256"] == expected["sha256"]
            and actual["file"]["size"] == expected["size"]
            and actual["elf"]["gnu_build_id"] == expected["build_id"]
            and int(symbol.get("value", "0"), 0) == expected["symbol"]
            and symbol.get("size") == expected["symbol_size"]
            and symbol.get("state") == "stock"
        )
        report.check(
            matched,
            f"HAL fixture {system}",
            f"sha/build-id/symbol=0x{expected['symbol']:x}/160 verified",
            json.dumps({"file": actual["file"], "build_id": actual["elf"]["gnu_build_id"], "symbol": symbol}, ensure_ascii=False),
        )

    source = (root / "src/patcher_v3.c").read_text(encoding="utf-8")
    source_upper = source.upper()
    missing_offsets = [
        system
        for system, item in HAL_CASES.items()
        if f"0X{int(item['symbol']):X}" not in source_upper
    ]
    report.check(not missing_offsets, "profile fixture offsets", "all archived offsets appear in patcher profiles", f"missing offsets for {missing_offsets}")


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
    check_lock_protocol(root, report, args.adb)
    check_config_hotupdate(root, report, args.adb)
    check_webui_writer_transaction(root, report, args.adb)
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
