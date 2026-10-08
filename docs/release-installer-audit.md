# Installer readiness — 2026-10-08

This audit is release preparation, not a release approval. No installer was run
against the user's installed host. Umbra retains Apollo's service and tray model:
closing the settings browser leaves the host available; tray Quit requests the
service wrapper to stop. No custom browser-close behavior was added.

## Applied corrections

- Service registration uses Apollo master's quoted executable-path correction,
  including installations under `Program Files`.
- Windows uninstall removes its PATH entries before the optional installation
  directory deletion, whether the user keeps settings or removes them.
- ViGEm setup waits for its prerequisite installer, avoids a spontaneous reboot,
  and reports its result to NSIS. NSIS stops on failure and marks exit 3010 as a
  reboot requirement. Compatible existing shared drivers are retained.
- Installer PowerShell calls use no user profile and no interactive prompts.

The service-path correction is directly from
[Apollo's install-service.bat](https://github.com/ClassicOldSong/Apollo/blob/master/src_assets/windows/misc/service/install-service.bat).
The remaining changes repair packaging without changing the upstream application
lifecycle. Their unavoidable upstream-file touch points are recorded in
`scripts/umbra-delta.txt`.

## Exercised verification

`tests/windows/test-install-gamepad.ps1` ran on Windows PowerShell 5.1 using a
harmless temporary executable fixture, never a driver. It verified that actual
`Start-Process` waits for the prerequisite and forwards exit 0, 3010 and 1603;
that quiet/no-restart arguments reach the executable; and that an existing
compatible driver skips installation. The fixture and its temporary directory
are removed in `finally`.

`node scripts/umbra_check.mjs` passed. `git diff --check` passed.

`scripts/check-windows-payload.py` checks an unpacked staging tree for executable,
web, encoder, driver and notice/source-manifest presence. Running it against the
installed Apollo directory failed for missing `drivers/sudovda/SudoVDA.dll`,
`drivers/sudovda/nefconc.exe`, and a completed `SOURCE-MANIFEST.md`. This is
expected evidence of an incomplete reusable installer payload, not an assertion
that the already installed driver is missing from Windows' Driver Store.

## Remaining release gates

1. Supply the complete SudoVDA and nefconc installer payload with exact immutable
   source correspondence, licence notices, hashes and signature verification.
   Both binaries are missing from the tracked packaging directory. SudoVDA's
   current upstream README grants MIT/CC0 for its author's changes; that alone
   does not establish the source correspondence of the newer packaged driver.
2. Build and stage the intended GPU HEVC DLLs and kernels from their exact source.
   The existing Windows CMake configuration leaves `UMBRA_GPU_HEVC_DIR` empty;
   source packaging currently warns and permits omission.
3. Complete the release-matched source archive, manifest and dependency inventory;
   the current template explicitly remains incomplete.
4. Build the actual NSIS installer and verify clean install, upgrade, tray Quit,
   service stop, uninstall, restart requirement and rollback in an isolated
   Windows test environment, including an installation path containing spaces.
5. Linux DEB/RPM source packaging currently supplies a postinstall script but no
   removal hook to stop an active user service or standalone host. Implement and
   exercise that lifecycle on the supported distro versions before release.
6. Audit the optional legacy `aurora-link session-start` children: they launch
   detached and rely on `session-end` PID-file cleanup. Source currently checks
   only the executable leaf name, does not await termination, and its optional
   `dispwake` call can outlive its 15-second wait. A clean default config does not
   invoke those legacy session hooks automatically; upgraded rigs can still do so.

Normal service/tray shutdown source already contains job-object cleanup for the
host and a special Quit exit status preventing respawn. This is source evidence;
it does not replace a packaged runtime lifecycle test. User-configured detached
games and other products' shared drivers must not be indiscriminately killed or
removed as part of cleanup.
