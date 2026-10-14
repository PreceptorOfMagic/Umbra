# Umbra Linux development Flatpak

This recipe builds the published Umbra 0.5.2 for Linux bundle (tag `umbra-linux-v0.5.2`). It fetches
the Umbra commit it pins from the public repository. The [Linux release workflow](../../../.github/workflows/linux-release.yml)
builds the release this way on GitHub from the tag, with the HDR extras, and publishes it once
everything has passed. To build it from a checkout of the tag with flatpak-builder 1.4.4 or newer
(1.4.2, Ubuntu 24.04's, installs the CMake modules to `lib64`, and the build stops at
`libayatana-indicator`; a recursive clone fails: the `third-party/build-deps` submodule pins a commit
no longer on any branch, which the manifest fetches as an archive instead):

```
git clone --branch umbra-linux-v0.5.2 https://github.com/PreceptorOfMagic/Umbra.git && cd Umbra
git submodule update --init packaging/linux/flatpak/deps/shared-modules
flatpak-builder --user --install-deps-from=flathub --force-clean --repo=repo build \
  packaging/linux/flatpak-umbra/io.github.PreceptorOfMagic.Umbra.yml
flatpak build-bundle --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo \
  repo Umbra.flatpak io.github.PreceptorOfMagic.Umbra master
```

For a local build of unpublished changes, copy this packaging directory (dereferencing shared-modules),
replace only the Umbra git URL with the local clone's file URL and pin the local commit. Do not publish
the machine-specific builder copy.

The intended experience is one ordinary Flatpak installation, one desktop screen-sharing approval,
remembered permission where supported, and clear setup for streamed input. A desktop's missing
virtual-display controls must not prevent ordinary desktop streaming.

The final release also requires the custom GPU HEVC encoder for mixed-hardware co-op with Windows
behavioral parity, plus HDR wherever capture, encoding and presentation support it. The development package now includes the Linux custom encoder and portal HDR path.
Both are in the 0.5.1 and 0.5.2 releases; the repository README lists the setups they were tested on. The
development record is [the Linux co-op record](../../../docs/umbra-linux-coop.md).

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
The Input Setup dialog also offers “Start Umbra when I sign in”. This writes an app-specific
XDG desktop autostart entry and disables the older optional systemd registration without stopping
a current stream. It works on desktops that do not activate systemd graphical-session targets.
The graphical launcher offers setup when input access is missing. Users can defer it and reopen
Input Setup from the desktop application menu. The background service starts the host directly.
The dialog uses the same helper and shows failure instead of claiming input is ready.

`remove-additional-install.sh` removes only that app-specific rule, unit and autostart entry.
Run it before removing the Flatpak if you want to remove optional host integration too. It does not delete a native
Sunshine service or older shared files. Earlier Umbra builds installed files named sunshine.service
and60-sunshine.rules; do not delete those blindly because they may belong to a native installation.

The actual dialog and helper were tested with denied host access and a cancelled desktop polkit
prompt: the dialog showed the failure and retry, and no rule/unit was created.
First-run deferral and explicit reopening were also measured. The new startup helper was staged
inside the installed app on Mint: a real reboot/login started the host and serverinfo answered;
disabling startup followed by another reboot left the host stopped. The checkbox builds with
strict compiler warnings. Graphical checkbox interaction and successful polkit authentication
remain acceptance checks. Input rules and reboot access were validated separately through SSH sudo.

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
or Windows-equivalent co-op based on mode labels or counters.
