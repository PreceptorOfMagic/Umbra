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

## Additional corrections and isolated tests

- Windows packaging now fails if required driver files or the GPU payload are
  absent. Both GPU DLLs were rebuilt from the audited v12b source, with pinned
  x265/OpenCL sources and installed GPL/provenance files.
- Complete SudoVDA payload obtained from official Apollo0.4.6; DLL/catalog
  Authenticode Valid. INF/catalog/certificate hashes match original tracked
  inputs. nefconc was compiled from its pinned source with the exact minimal
  x64 vcpkg overlay retained in `packaging/nefcon/`.
- Linux removal stops only a matching installed user service, then uses pidfds
  to terminate exact-path standalone processes without PID-reuse races. A
  relative service ExecStart is accepted only when its running MainPID image
  matches. Ambiguous active services abort removal; unrelated units survive.
- One combined DEB keeps host, helper, notices and removal hook together;
  Debian control scripts use strict executable permissions.
- Optional legacy aurora-link children hold a verified unique same-session host
  handle. Host death or a named stop event requests normal pad/mic cleanup;
  a bounded hard stop is a fallback. Session cleanup checks full executable
  path, awaits termination and retains failed PID records instead of silently
  forgetting them. New helpers cannot overwrite an incompletely cleaned set.
- Removed the Linux Low Res Desktop sample that assumed HDMI-1 and particular
  resolutions. Desktop and Steam entries remain.

Measured isolated tests (no driver/device installation or live streaming):

- Native Windows helper fixture built actual main.cpp with stub pad/mic loops:
  wrong installation refused, ambiguous same-session owners refused, run/mic
  exit cooperatively after owner death, named stop event exits helper with0
  while host remains. This does not demonstrate real ViGEm/audio resource release.
- Native Linux pidfd helper: normal process stopped0.01s; deleted executable
  stopped0.01s; stubborn process killed after30.07s. Same-name process from a
  different installation remained alive. All fixture processes removed.
- Fresh Linux JSON parses and no longer contains connector-specific commands.

## Package verification

The Ubuntu24.04 DEB was built with CUDA12 support. Extraction verifies prerm0755
and `/usr/libexec/umbra-stop-host` present in the same package. The packaged
helper passed normal/deleted/stubborn-process tests (0.00s/0.00s/30.04s), retaining
unrelated same-name processes. The package has no missing shared objects on its
build baseline. dpkg-shlibdeps adds the measured ICU74, GLIBC2.38, libstdc++13.1
and Ubuntu t64 dependencies, preventing unsupported older installations.

Windows host/tools and GPU DLL imports were inspected: no non-system imported
DLL remains unbundled. Explicitly packaged zlib supports its dynamic load path.
Source-built MinHook replaces an old developer archive of unidentified origin.
SudoVDA prerequisite failure propagation now stops setup; this is checked with
harmless stand-in programs, never real certutil or device installation.

## Remaining runtime release gates

The build candidates require isolated clean-machine install, upgrade, tray Quit,
service stop, uninstall and rollback tests using their exact hashes. Windows
must cover paths containing spaces, actual prerequisite failure/reboot handling,
new SudoVDA device creation and ViGEm/audio cleanup. Linux runtime support is
currently targeted at Ubuntu24.04 x86_64; the CUDA-enabled build alone cannot
prove capture/encoding across GPUs or other distributions.

The signed SudoVDA distribution is authenticated, but upstream does not provide
an exact reproducible source-to-signed-driver attestation. Keep that limitation
in the manifest. External dependency source delivery must be inventoried with
any remaining gaps before public distribution. No public release is approved
by the isolated fixtures or by package presence checks.

Normal service/tray shutdown preserves Apollo job-object cleanup and its
special Quit exit status preventing respawn. Closing a settings browser leaves
the host running intentionally. User-configured detached games and shared
system drivers must not be indiscriminately killed or removed.

## Final installer failure and ownership checks

Windows service scripts now verify the service executable belongs to this install
before stopping/removing it. A foreign legacy Sunshine service is preserved; a
foreign ApolloService fails closed. SCM handles are disposed before deletion,
stop completion is awaited, and setup/removal errors reach NSIS. Nine mocked
SCM scenarios passed, including foreign service, stop/wait/delete failure.

The uninstaller invokes the held-handle process helper after service removal and
before deleting files. Its benign unique-name fixture stopped seven exact-path
owned executables, preserved six unrelated same-name copies, observed cooperative
helper cleanup/exit0 in342ms and bounded legacy fallback in5118ms; a repeated
cleanup passed. The production-name fixture encountered access denied on the
existing SYSTEM host and failed closed without stopping it. The alias fixture
changes executable leaf names only, verified by a reverse-transform assertion.
Elevated real installer acceptance remains unmeasured.

Optional gamepad/autostart components are checked for presence before execution.
Gamepad removal propagates MSI status/reboot/failure; seven mocked CIM cases passed.
SudoVDA install/uninstall fixtures passed nine success/failure/reboot cases with
harmless native stand-ins in a path containing spaces, never actual driver tools.

Linux postinstall targets its absolute installed executable, requires libcap2-bin
and udev, and propagates capability/udev failures. Its fixture proves an earlier
foreign sunshine on PATH is untouched and a failed capability update cannot claim
success. Avahi is a recommended dependency for default mDNS discovery; package
metadata alone does not demonstrate network discovery.

Windows retains Apollo's explicit keep/remove-settings choice. Choosing removal
also removes the saved start-type file and empty app-specific containers. Linux
preserves inherited per-user Sunshine profiles: shared naming does not prove
Umbra ownership and a root hook must not sweep unrelated user data. Shared drivers
remain by default. Therefore these candidates do not claim literally zero
remaining user data after every uninstall choice.
