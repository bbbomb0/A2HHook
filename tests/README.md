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
cache-fault seam, and strict stale-stub ownership contracts. With `--adb`, it
also runs the PID/starttime lock harness against Android `/proc`, including PID
reuse, legacy PID-only owners, strict release ownership, and an interrupted
lock-creation state.

## Isolated native transaction faults

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_transaction_harness.ps1 `
  -Ndk "D:\Download\android-ndk-r26d-windows\android-ndk-r26d"
```

The harness includes the current `src/patcher_v3.c`, replaces remote memory I/O
with a private byte array, injects every primary write failure in the string and
single-pass whitelist-stub transactions, and checks byte-for-byte rollback. It
also verifies strict 8-byte and 16-byte stale-overlay ownership, rejects damaged
suffix/marker evidence, recovers an owned safe-tail cave without external hint
files, and executes a small instruction model against all ten matcher slots.
The model covers slots 7 and 10, disabled/null entries, and exact string
boundaries. The harness also injects a second-pass cache-maintenance failure and
proves the outer function-and-cave transaction restores the original bytes. It
does not attach to or modify the phone audio process.

## ZIP verification

```powershell
python tests/verify_module_zip.py .\a2h_hook_v1.5.5-fix2.zip `
  --expected-version v1.5.5-fix2 --expected-code 1552
```

This independently checks the exact member list, duplicates, path portability,
Unix file modes, CRC, BOM/CRLF, configuration shape, module metadata, AArch64
ELF identity, static patcher linkage, and embedded patcher version.

## OS3.0.302 device matrix

Preflight only:

```powershell
powershell -ExecutionPolicy Bypass -File tests/os302_device_regression.ps1 `
  -ExpectedVersion v1.5.5-fix2
```

Full two-round test after the candidate module is installed and the phone has
rebooted:

```powershell
powershell -ExecutionPolicy Bypass -File tests/os302_device_regression.ps1 `
  -ExpectedVersion v1.5.5-fix2 -Execute -AllowAudioRestart
```

The device case waits for the apply queue to become idle, backs up persistent
and derived configuration, and requires a verified native restore before it
exits. It tests global/whitelist round trips, official and custom slot counts,
per-slot off/on, package-table preservation, two audio-HAL PID restarts,
watcher re-application, and atomic plus multi-stage slot-8 file-manager edits.
A final manual close/reopen of the ReSukiSU WebUI remains required,
because that manager exports only its main activity and has no stable direct
intent for its internal module-WebUI route.

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
