# Umbra Linux development Flatpak

This recipe is a private candidate, not a published release. Its source commit must be available
before another machine can build it from the public URL. For a local build, copy this packaging
directory (dereferencing shared-modules), replace only the Umbra git URL with the local clone's
file URL, and retain the pinned commit. Do not publish the machine-specific builder copy.

The intended experience is one ordinary Flatpak installation, one desktop screen-sharing approval,
remembered permission where supported, and clear setup for streamed input. A desktop's missing
virtual-display controls must not prevent ordinary desktop streaming.

The final release also requires the custom GPU HEVC encoder for mixed-hardware co-op with Windows
behavioral parity, plus HDR wherever capture, encoding and presentation support it. The development package now includes the Linux custom encoder and portal HDR path.
Mixed-host parity and final release qualification remain open. See the final release
scope and implementation order in [the Linux co-op record](../../../docs/umbra-linux-coop.md).

## Build choices

- Freedesktop runtime/SDK25.08; node22 builds the web UI from its locked offline dependencies.
- CUDA12.9.1 compiler/runtime/CCCL components are SHA256-pinned from NVIDIA's redistributable manifest.
  They replace extraction of a multi-gigabyte full toolkit. None of these build tools are intended
  to remain in the application payload; inspect the final export before release.
- The CUDA header patch adds `noexcept(true)` to six declarations to match SDK25.08 glibc2.42.
  This is confined to the pinned Linux build toolkit. The unpatched compiler fails on those six
  declarations; a minimal sm_50 kernel compiles with the patch. NVIDIA12.9.2 still ships the same
  nvcc12.9.86 headers. See [NVIDIA issue378](https://github.com/NVIDIA/cuda-samples/issues/378).
  This compatibility patch does not imply NVIDIA certification of this SDK/toolchain combination.
- GCC14.4 is compiled from GNU source solely as nvcc's host compiler. Normal C++ uses the SDK compiler.
  The compiler version guard is retained. GCC14 supports CUDA12.9 without dropping the existing
  Maxwell/Pascal targets, as CUDA13 would. Actual old-GPU execution remains a validation requirement.
- Position-independent code is explicitly enabled so GCC14-generated CUDA host objects link into
  SDK25.08's PIE executable. Without it, the measured final link fails on an R_X86_64_32 relocation.
- The app identity is separate from Sunshine. Native Sunshine configuration is not migrated or deleted.
- BUILD_TESTS=OFF follows the prior release because inherited pairing tests do not compile against
  Umbra's current API. This does not excuse capture, input, installation or lifecycle validation.

## Optional host integration

Inside the installed app, `additional-install.sh` installs this Flatpak identity's input rule and
optional systemd user unit. The desktop's normal administrator prompt authorizes the rule; setup
returns failure if authorization or device access fails. It loads input device modules if needed,
reloads rules, triggers those two devices and checks access. The service is not enabled automatically.
The graphical launcher offers setup when input access is missing. Users can defer it and reopen
Input Setup from the desktop application menu. The background service starts the host directly.
The dialog uses the same helper and shows failure instead of claiming input is ready.

`remove-additional-install.sh` removes only that app-specific rule/unit. It does not delete a native
Sunshine service or older shared files. Earlier Umbra builds installed files named sunshine.service
and60-sunshine.rules; do not delete those blindly because they may belong to a native installation.

The actual dialog and helper were tested with denied host access and a cancelled desktop polkit
prompt: the dialog showed the failure and retry, and no rule/unit was created.
First-run deferral and explicit reopening were also measured. Successful privileged installation,
logout/login, service autostart and removal still need validation.

## Evidence and release gates

See ../../../docs/umbra-linux-coop.md for measured capture and host/client behavior. Portal host checks passed on runtime23.08 and the rebuilt runtime25.08 candidate. A direct runtime25 override failed on the old
ICU73 dependency; rebuild dependencies rather than shipping old runtime libraries to hide the failure.

| Environment | Evidence | Still required |
|---|---|---|
| Plasma6.6.6 Wayland, NVIDIA2060SUPER | Portal capture, virtual sizing, HEVCSDR stream, Disconnect/Resume/Quit on23.08 and25.08 | Real co-op, game placement, fractional-scale/negative-origin input, high-rate behavior |
| Mint22.3/Cinnamon6.6.9 X11, NVIDIA580.178.04 | Packaged Main8/Main10 custom streams; automatic capture/GPU; synthetic network controller delivered to evdev; input ACLs survived reboot | Real gamepad/game/audio, GUI privileged setup, co-op, performance |
| AMD X11 | Previous0.5.0 VAAPI live stream | Regression test new artifact |
| GNOME50.1 Wayland, NVIDIA595.99.02 | Packaged ABI3 HDR10, SDR8 fallback, live HDR-to-SDR switch; source and decoded images inspected | TV/HDR-game presentation, input/audio/gameplay, co-op; virtual pane not advertised |
| wlroots Wayland | Existing capture paths retained | Real installation and regression tests; KDE-only mode control unavailable |

Also validate clean install, upgrade, permission denial/revocation, restart, uninstall, Intel VAAPI,
older NVIDIA GPUs, older Intel hardware (25.08 removed intel-vaapi-driver in favor of
intel-media-driver), fractional scaling and multi-monitor input. HDR evidence currently covers GNOME/NVIDIA ordinary streaming. TV presentation,
HDR games, other GPU/desktop combinations and HDR co-op remain unqualified. Do not promise165fps
or Windows-equivalent co-op based on mode labels or counters. Publication stays deferred.
