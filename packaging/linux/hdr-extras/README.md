# Prebuilt HDR extras

Stock KWin and Mutter keep the virtual displays Umbra streams SDR. The patches in
[kwin-virtual-hdr](../kwin-virtual-hdr/) and [mutter-virtual-hdr](../mutter-virtual-hdr/) add HDR
virtual displays and 10-bit capture. This directory builds them into ready-to-install packages for
the major distributions, so users do not have to rebuild their compositor. Users of other
distributions can still add the patch to their own `kwin` or `mutter` package.

Each build runs in a container of the target distribution. It starts from that distribution's
current `kwin` or `mutter` source package (Arch's and CachyOS's recipes for those two) and adds the
patch for its version series. The result therefore matches the packages its users have installed,
including the distribution's own patches.

## Targets

[`targets`](targets) is the list. As of October 2026:

| Target | KDE: KWin | GNOME: Mutter | Package version |
|---|---|---|---|
| `ubuntu26.04` | 6.6.6, patch 6.6 | 50.1, patch 50 | `<archive version>+umbrahdrN` |
| `ubuntu26.10` | 6.7.5, patch 6.7 | 51.0, patch 51 | `<archive version>+umbrahdrN` |
| `fedora44` | 6.7.5, patch 6.7 | 50.5, patch 50 | `<release>.umbrahdrN` |
| `fedora45` | 6.7.5, patch 6.7 | 51.0, patch 51 | `<release>.umbrahdrN` |
| `opensuse-tumbleweed` | 6.7.5, patch 6.7 | 50.5, patch 50 | `<release>.umbrahdrN` |
| `arch` | 6.7.5, patch 6.7 | 51.1, patch 51 | Arch's own |
| `cachyos` | 6.7.5, patch 6.7 | 51.0 (CachyOS's own build), patch 51 | CachyOS's own |

Not covered:
- Debian 13 (KWin 6.3, Mutter 48) and openSUSE Leap 16.0 (KWin 6.4, Mutter 48). Their compositors
  predate the colour-managed screencast these patches extend, so supporting them would mean
  backporting that work, not porting a patch.
- Fedora 43. Its PipeWire 1.4 has no PQ transfer function for the 10-bit capture to announce
  (PipeWire 1.6 added it; the KWin 6.7 patch does not compile against 1.4), and its Mutter 49 has
  no HDR screen capture. Fedora 43 reaches its end of life a month after Fedora 45 is released.

Version numbers: Debian, Ubuntu, Fedora and openSUSE packages sit above the distribution's own
version and below its next update, so a distribution update replaces them, as with the Ubuntu
26.04 extra of Umbra 0.5.1. `N` comes from [`revisions`](revisions). pacman has no such slot for a
third party: CachyOS already uses Arch's sub-release (`6.7.5-3.1`) for its own rebuilds. So the
Arch and CachyOS packages keep the repository's version. `pacman -U` reinstalls over the
repository's package, and the next repository update replaces it. Their README.txt shows how to
tell them apart (`pacman -Qi`, Packager: Umbra HDR extra).

CachyOS:
- KWin: CachyOS has no KWin recipe of its own. Its x86-64-v3, v4 and Zen 4 repositories carry
  Arch's KWin rebuilt as `<Arch version>.1`, and its generic repositories carry Arch's package. The
  CachyOS extra is Arch's PKGBUILD at CachyOS's version, which fits all of them; the build checks
  that the v4 and Zen 4 repositories carry that version too.
- Mutter: CachyOS builds its own from [CachyOS-PKGBUILDS](https://github.com/CachyOS/CachyOS-PKGBUILDS),
  with its own patches. Its `[cachyos]` repository puts that Mutter ahead of Arch's.
- Both extras are built with Arch's generic x86-64 flags (the CachyOS image would otherwise use
  `-march=native`), so they run on any CachyOS machine.

## What a build produces

`Umbra-HDR-<KDE|GNOME>-<target>.tar.gz`, with `.sha256`, containing:
- the packages a desktop already has installed (no `-dev`, tests or debug packages);
- `README.txt`: what it is, the exact compositor version it is for, and that distribution's
  install, verify, update and rollback commands;
- `SHA256SUMS`.

`Umbra-HDR-<KDE|GNOME>-<target>-source.tar.gz` holds the complete source: the distribution's source
package with the patch integrated, plus the patch.

The build fails, rather than guessing, in three cases: the distribution's compositor version has no
patch, its source and binary packages disagree, or the packages it would ship lack the patched
library (or, for KWin, the screencast plugin).

## Smoke test

[`smoke-test.sh`](smoke-test.sh) checks each built extra in a fresh container of its distribution
([`smoke-local.sh`](smoke-local.sh) starts one; the workflow runs it after every build). It
installs the distribution's own compositor, then follows the extra's `README.txt`, running each
command as it is printed there (without `sudo`): the version check, `sha256sum -c`, the install,
the VERIFY checks that need no running Umbra, and the rollback. It checks the installed version
after each step, and that every library and program in the packages finds its libraries. In
between, it runs the compositor without a GPU, first stock and then patched:

- GNOME: Mutter headless, with software rendering, creates a virtual monitor the way Umbra asks
  for one (ScreenCast `RecordVirtual`), driven by
  [`smoke/gnome-virtual-hdr-probe.py`](smoke/gnome-virtual-hdr-probe.py). Patched, the monitor
  must offer BT.2100 (`supported-color-modes` [0, 1]), switch to it, and its PipeWire stream must
  then offer 10-bit formats. Stock, it must offer neither. This is what the patch changes, measured
  the same way as "Patch alone" in the Mutter README.
- KDE: KWin creates a virtual output the way Umbra asks for one (`zkde_screencast_unstable_v1`
  `stream_virtual_output`), and [`smoke/kde-virtual-hdr-probe.c`](smoke/kde-virtual-hdr-probe.c)
  queries and switches its HDR with Umbra's own output control (`src/platform/linux/kde-output.c`).
  The patch changes the virtual outputs of KWin's DRM backend, which needs a KMS device: with one
  (`vkms` on the CI runner), patched KWin must report the output HDR-capable and switch it to HDR,
  and stock KWin must not. Without one, KWin falls back to its headless backend, which has no
  OpenGL and refuses screencasts. The test then only shows that patched KWin starts, loads the
  screencast plugin and answers the request, and it says so.

Nothing here drives a display or an encoder: HDR output on real hardware and Umbra's capture of it
stay hardware tests.

## Building

On a push or pull request that touches the patches or this directory, and weekly, the
[Linux HDR extras workflow](../../../.github/workflows/linux-hdr-extras.yml) builds every target.
The weekly run notices when a distribution update leaves a published extra stale or brings a
version no patch fits. Each run's summary lists the versions it built.

To publish, run the workflow by hand with `release_tag` set to an existing release. It attaches the
tarballs (replacing older ones of the same name) and updates the release's `SHA256SUMS`. `targets`
limits a run to some distributions, for example to refresh the Fedora 44 extras after a Fedora KWin
update.

Locally, with podman or docker:

```
packaging/linux/hdr-extras/run-local.sh fedora44 kde out
```

A KWin build takes 20 to 40 minutes on four cores; Mutter about 10.

## When a distribution moves to a new KWin or Mutter

If the build fails with "no patch for KWin 6.8.0", port the nearest patch to that series. Check
that it compiles against that distribution's source, and add it as
`kwin-virtual-hdr-screencast-<major.minor>.patch` or `mutter-virtual-hdr-screencast-<major>.patch`
with a line in `revisions`. Patches within one series are selected by series, so a bugfix release
(6.7.5 to 6.7.6) needs nothing unless the patch stops applying. Raise a series' revision whenever its
patch changes.

## Tested

Only the Ubuntu 26.04 extras have run on hardware: KWin 6.6.6 and Mutter 50.1, with the
measurements in each patch's README. Every other build carries the smoke test above and says so in
its README.txt. So far that covers the KWin 6.7 and Mutter 51 patches, and the Mutter 50 patch on
Fedora and openSUSE.
