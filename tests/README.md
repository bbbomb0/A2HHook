# A2HHook regression tests

These tests are development-only. `package_module.py` uses an explicit release
manifest, so nothing under `tests/` is included in the KernelSU module ZIP.

## Fast static checks

```powershell
python tests/static_regression.py --ndk "D:\Download\android-ndk-r26d-windows\android-ndk-r26d" --adb
```

The command verifies release metadata, strict C syntax/warnings, Android shell
syntax, inline WebUI JavaScript syntax, WebUI device-state persistence guards,
archived HAL fingerprints, the 76-byte whitelist/global capacity guard,
profile/ELF resolver ordering, the outer whitelist transaction, the test-only
cache-fault seam, strict stale-stub ownership, 32-bit game/spatial flags,
72-byte stock/relaxed handoff policy, 148-byte stream handoff/helper plus
the dynamic RX-tail 208-byte open-plus-pending/committed real-stream concurrent helper,
exact previous-176/160 and 64/56-byte helper migration, output-pool tail branch,
committed-`+appname` handoff clearing, ROM-specific BL/PLT generation, three
event-driven appname/recompute overlays, and unique semantic layout derivation
across the four archived HALs. It also executes the production policy-refresh
transaction and requires first/same-policy applications to skip the silent
trigger, both policy directions to trigger exactly once, and trigger failure to
leave applied metadata uncommitted. The audio UID watcher harness runs the production
shell script with synthetic vendor playback events and verifies arbitrary global
packages, enabled/disabled exact custom slots, invalid and missing UID rejection,
cached core-system UID rejection, per-package recursion cooldown, compact and
whitespace-formatted events, long valid
   package names, actual `su UID -c trigger --lease` arguments, bounded logcat restart backoff,
   and trigger failure reporting without any hardcoded game package. With `--adb`, the
   AudioPolicy lease harness additionally covers vendor-event fallback, callback-ready
   session suppression, `session -> portId` and `portId -> session` field order,
   `stopOutput()` / `stoptOutput()`, stop records without app names, multiple real ports,
   stale stop rejection, last-port release, PID/starttime ownership and full runtime cleanup.
   Cleanup also requires token-first shutdown, a single bounded grace window shared by
   all lease workers, and escalation only for owners still alive after that window.
   The native trigger must reach `STARTED`, execute its silent AAudio callback, publish a
   real allocated session as `ready`, and later reach `STOPPED`; a 70-second bounded
   fallback is used only when no real AudioPolicy lifecycle is observed. The service wake
harness executes the production FIFO functions (under root on Android) and verifies
immediate config wakeup, 30-second-equivalent health timeout bookkeeping, FIFO type
and cleanup. Companion checks cover both protected
`TileService` declarations, the positive game-background-haptic toggle, immediate
visual feedback contract, and clean APK install/uninstall lifecycle. The native
harness also covers controlled
`+0x40` layout drift and duplicate-anchor rejection. With `--adb`, it
also runs the PID/starttime lock harness against Android `/proc`, including PID
reuse, legacy PID-only owners, strict release ownership, and an interrupted
lock-creation state.

## Isolated native transaction faults

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_transaction_harness.ps1 `
  -Ndk "D:\Download\android-ndk-r26d-windows\android-ndk-r26d"
```

The harness includes the current `src/patcher_v3.c`, replaces remote memory I/O
with a private byte array, injects every primary and auxiliary write/cache
failure in the coordinated transaction, and checks byte-for-byte rollback. It
also verifies strict 8-byte and 16-byte stale-overlay ownership, rejects damaged
suffix/marker evidence, recovers an owned safe-tail cave without external hint
files, and executes a small instruction model against all ten matcher slots.
The model covers slots 7 and 10, disabled/null entries, exact string boundaries,
four ROM-specific app-policy/handoff BL generations, eleven handoff and
map-precedence boundary cases, exact previous-176/160/64/56-byte helper ownership,
legacy public-v1.5.6 and first-generation handoff migration, plus stock/relaxed
application policy transitions. It also
proves the outer function, cave and all
auxiliary regions restore their original bytes on any failure. It
does not attach to or modify the phone audio process.

## ZIP verification

```powershell
python tests/verify_module_zip.py .\a2h_hook_v1.5.8.zip `
  --expected-version v1.5.8 --expected-code 1580
```

This independently checks the exact member list, duplicates, path portability,
fixed reproducible member timestamps,
Unix file modes, CRC, BOM/CRLF, configuration shape, module metadata, AArch64
ELF identity, static patcher linkage, and embedded patcher version.

## OS3.0.302 device matrix

Preflight only:

```powershell
powershell -ExecutionPolicy Bypass -File tests/os302_device_regression.ps1 `
  -ExpectedVersion v1.5.8
```

Full two-round test after the candidate module is installed and the phone has
rebooted:

```powershell
powershell -ExecutionPolicy Bypass -File tests/os302_device_regression.ps1 `
  -ExpectedVersion v1.5.8 -Execute -AllowAudioRestart
```

The device case waits for the apply queue to become idle, backs up persistent
and derived configuration, and requires a verified native restore before it
exits. It tests global/whitelist round trips, official and custom slot counts,
per-slot off/on, package-table preservation, two audio-HAL PID restarts,
watcher re-application with one final native check per restart, and atomic plus
multi-stage slot-8 file-manager edits. The multi-stage case requires the
intermediate table to remain below the three-sample quiet window and produce no
separate revision. Restore removes the derived applied-policy marker and requires
the production applier to regenerate it with the restored configuration. The fast suite also runs 40 production
watcher cycles and rejects any steady-state native inspection or apply. A
second watcher harness simulates an atomic save during inotify rearming and
requires the short signature-poll grace window to detect and apply it before
the 30-second health probe.
Control-center tile and companion-WebUI rendering remain device UI checks; the
native matrix does not synthesize SystemUI touch input.

## OS3.0.305 runtime evidence

When a 3.0.305 device reports `51 00 00 58` at `is_A2H_app`, copy
`tests/os305_runtime_capture.sh` to the device and run it from a root shell:

```sh
su
sh /sdcard/Download/os305_runtime_capture.sh
```

The generated `A2H_OS305_runtime_*.txt` is read-only evidence. It captures the
full live function, decoded absolute-jump target and owner map, full process
maps, 10-slot configuration, retired injection files, and recent logs. Run it
immediately after enabling a real custom slot. The script is not packaged in
the module ZIP.
