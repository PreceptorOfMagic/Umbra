# Mutter: HDR virtual monitors and 10-bit virtual-monitor screencast

Umbra gives every stream its own GNOME virtual monitor (Mutter ScreenCast `RecordVirtual`), as Windows
Umbra gives every client its own virtual display. Stock Mutter 50.1 keeps virtual monitors SDR: BT.2100
(HDR) colour mode is selectable only for outputs that report BT.2020 + PQ, which virtual outputs never do,
and the virtual-monitor screencast only offers 8-bit formats. The patch adds both, in one version per
Mutter series: `mutter-virtual-hdr-screencast-50.patch` (against Ubuntu's Mutter 50.1-0ubuntu2.4; applies
unchanged to 50.5 and 50.6) and `mutter-virtual-hdr-screencast-51.patch` (against 51.0 and 51.1, where
Mutter moved this code to `src/backends/meta-stream-source-virtual.c`; compile-tested only). Umbra detects
it at runtime: no patch, no HDR on virtual monitors (a physical monitor in HDR mode still streams HDR),
nothing else changes.

## What the patch changes

- `src/backends/native/meta-output-virtual.c`: virtual outputs report BT.2020 colourspace and PQ, so
  BT.2100 becomes selectable through DisplayConfig (`color-mode` 1). Virtual monitors still start SDR
  (a new monitor takes its last stored mode or the default).
- `src/backends/meta-screen-cast-virtual-stream-src.c` (Mutter 51: `meta-stream-source-virtual.c`), ported
  from Mutter's own monitor-stream source:
  - `get_formats`: the default 8-bit formats, plus `XRGB_2101010`, `XBGR_2101010` and `RGBA_FP16` carrying
    the monitor's colour state when it is BT.2100. For an SDR monitor the list equals Mutter's default.
  - An HDR (PQ) stream paints the stage in that colour state into the stream buffer (blending buffer when
    needed, rebuilt if the virtual monitor is resized). SDR streams keep the existing blit.
  - Formats are re-offered on `device-color-state-changed`, so switching a running virtual monitor to HDR
    offers the 10-bit formats.
- Revision 2 of the 50 patch (`+umbrahdr2`) adds the two leak fixes Mutter made to the monitor source's
  copy of this code and backported to 50.x (3bb5de59, b409822e): the blending colour state is released
  after each HDR frame, and the blending pipeline and framebuffer no longer keep an extra reference. The
  51 patch has them from the start.

## Install (prebuilt, Ubuntu 26.04 with GNOME 50.1, mutter 50.1-0ubuntu2.4 only)

[`../hdr-extras`](../hdr-extras/) builds these patches into each major distribution's own Mutter
package: Ubuntu 26.04 and 26.10, Fedora 44 and 45, openSUSE Tumbleweed, Arch Linux and CachyOS (its own
Mutter build). A release carries them as `Umbra-HDR-GNOME-<distribution>.tar.gz`, each with a `README.txt`
for that distribution, and their source as `Umbra-HDR-GNOME-<distribution>-source.tar.gz`. Umbra 0.5.2
for Linux carries all seven, with revision 2 of the Mutter 50 patch and how far each has been tested.
The Umbra 0.5.1 for Linux release carries the Ubuntu 26.04 revision 1 packages
(`Umbra-HDR-GNOME-ubuntu26.04.tar.gz`), the ones measured below:

```
sudo dpkg -i gir1.2-mutter-18_50.1-0ubuntu2.4+umbrahdr1_amd64.deb \
             libmutter-18-0_50.1-0ubuntu2.4+umbrahdr1_amd64.deb \
             mutter-common_50.1-0ubuntu2.4+umbrahdr1_all.deb \
             mutter-common-bin_50.1-0ubuntu2.4+umbrahdr1_amd64.deb
```

Then log out and in (Mutter is the compositor), and restart Umbra. If Umbra ran on stock GNOME before,
delete `~/.var/app/io.github.PreceptorOfMagic.Umbra/config/sunshine/gnome_virtual_hdr` first or start
one stream, so it re-detects. A distribution Mutter update replaces these packages: re-apply after updates.

Rollback: `sudo apt install --allow-downgrades gir1.2-mutter-18=50.1-0ubuntu2.4 libmutter-18-0=50.1-0ubuntu2.4 mutter-common=50.1-0ubuntu2.4 mutter-common-bin=50.1-0ubuntu2.4`, then log out and in.

## Other distributions

Each patch applies unchanged to GNOME's own releases of its series (the Ubuntu source package's upstream tarball is byte-identical to
50.1), so another distribution shipping Mutter 50 or 51 can add the matching patch to its own `mutter` package, rebuild that package with
its usual tools and install it. [`../hdr-extras/lib`](../hdr-extras/lib/) shows how for Debian, Fedora, openSUSE and pacman packaging.
Mutter 49 and older have no HDR screen capture at all (Fedora 43, Debian 13, openSUSE Leap 16.0), so no patch of this size can serve them.
Undo it by reinstalling the distribution's own package. Check the running version with `gnome-shell --version` (GNOME Shell 51.0 runs on
Mutter 51.0).

## Build

```
apt-get source mutter=50.1-0ubuntu2.4
cp mutter-virtual-hdr-screencast-50.patch mutter-50.1/debian/patches/umbra-virtual-monitor-hdr-screencast.patch
echo umbra-virtual-monitor-hdr-screencast.patch >> mutter-50.1/debian/patches/series
cd mutter-50.1 && dch -v 50.1-0ubuntu2.4+umbrahdr2 "Umbra: HDR (BT.2100) virtual monitors and 10-bit virtual-monitor screencast"
DEB_BUILD_OPTIONS="nocheck nodoc noautodbgsym" dpkg-buildpackage -b -uc -us
```

## Measured on VEGA (RTX 2060 SUPER, NVIDIA 595.99.02, Ubuntu 26.04.1, GNOME 50.1), 2026-10-14

- Patch alone (`vega/gnome/virtual-hdr-probe.py`): virtual `Meta-0` `supported-color-modes` [0, 1]
  (stock: no 1); `color-mode` 1 applied; PipeWire then offers `xBGR_210LE`, `xRGB_210LE`, `RGBA_F16`.
- Umbra 0.5.1 hdr9 (`d56a51f6`): startup `GNOME virtual displays offer HDR`; `/serverinfo`
  `ServerCodecModeSupport` 0x101 -> 0x301 and `UmbraCoopHdr` 0 -> 1.
- Live probe, 25 s each, decoded frames inspected: HDR desktop 3840x2160@165 (`PipeWire DMA-BUF HDR/PQ,
  GPU frames`, NVENC Main10, client `HDR_MODE enabled=1`) 3795 frames; co-op pane 1920x2176 10-bit HDR
  through GPU HEVC (`GPU frames ... PQ, hand-off=cuda`) 3362; co-op-sized pane 1920x2160 HDR through
  NVENC 3064; SDR desktop on the patched Mutter 3934 (no regression).
- Cost: the HDR stream re-renders the stage, so HDR rates are below KDE's (desktop ~152 vs ~161 fps).
  Rendering the virtual view itself at 10 bits would allow a blit instead (not done).

Upstreaming: not yet submitted to GNOME.
