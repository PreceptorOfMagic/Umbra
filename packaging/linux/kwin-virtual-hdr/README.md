# KWin: HDR virtual outputs and 10-bit screencast

Umbra gives every stream its own KWin virtual output (Windows Umbra's per-client virtual
display). Stock KWin 6.6 cannot make a virtual output HDR and only screencasts 8-bit, so on
stock KDE every stream and both co-op panes are SDR. The patch adds what is missing, in one
version per KWin series: `kwin-virtual-hdr-screencast-6.6.patch` (against KWin v6.6.6) and
`kwin-virtual-hdr-screencast-6.7.patch` (against v6.7.5; the same change on KWin 6.7's DMA-BUF
modifier types, compile-tested only). Umbra detects it at runtime: no patch, no HDR, nothing else
changes.

## What the patch changes

- `drm_virtual_output.cpp`: virtual outputs advertise `HighDynamicRange`; turning it on gives
  the output a BT.2020/PQ colour description (reference and peak luminance from the output
  configuration, 1000 nits peak by default).
- Screencast (`screencaststream`, `outputscreencastsource`, `screencastlayer`): when the
  output's transfer is PQ, the stream offers `ABGR2101010`/`xBGR_210LE` (BT.2020 primaries,
  SMPTE 2084 transfer) before the 8-bit formats and renders into the output's colour
  description when a consumer takes it. 8-bit consumers are unchanged.
- Fix for a stock bug: a screencast stream outlives its consumers and kept the DMA-BUF
  parameters it had tested for the previous consumer's format. A consumer asking for a
  different format got buffers allocated with an unfixated (linear) modifier, which NVIDIA
  cannot allocate ("Buffer allocation failed"). The parameters are now re-tested when the
  negotiated format changes. (The same bug is why the first 8-bit negotiation after a mode
  change could come back linear on stock KWin.)
- Warning lines for each DMA-BUF buffer failure step (`kwin_screencast`).

## Prebuilt packages

[`../hdr-extras`](../hdr-extras/) builds these patches into each major distribution's own KWin
package: Ubuntu 26.04 and 26.10, Fedora 44 and 45, openSUSE Tumbleweed, Arch Linux and CachyOS.
A release carries them as `Umbra-HDR-KDE-<distribution>.tar.gz`, with install, verify and rollback
steps in its `README.txt`, and their source as `Umbra-HDR-KDE-<distribution>-source.tar.gz`. The
Umbra 0.5.1 for Linux release carries the Ubuntu 26.04 one; Umbra 0.5.2 for Linux carries all seven,
with how far each has been tested. The KWin 6.7 ones need Umbra 0.5.2 or newer.

## Other distributions

Each patch applies unchanged to KDE's own release of its series (the Ubuntu source packages' upstream tarballs are byte-identical to them),
so another distribution shipping KWin 6.6 or 6.7 can add the matching patch to its own `kwin` package, rebuild that package with its usual
tools and install it. [`../hdr-extras/lib`](../hdr-extras/lib/) shows how for Debian, Fedora, openSUSE and pacman packaging. Other KWin
series need the patch adapted; KWin 6.3 and 6.4 (Debian 13, openSUSE Leap 16.0) lack the colour-managed screencast it extends, and
PipeWire before 1.6 (Fedora 43) lacks the PQ transfer function it announces. Undo it by
reinstalling the distribution's own package. Check the running version with `kwin_wayland --version`.

## Build and install (Ubuntu 26.04, `kwin` 4:6.6.6-0ubuntu0.1)

```
apt source kwin                       # or: git clone https://invent.kde.org/plasma/kwin -b v6.6.6
cp kwin-virtual-hdr-screencast-6.6.patch kwin-6.6.6/debian/patches/umbra-virtual-output-hdr-screencast.patch
echo umbra-virtual-output-hdr-screencast.patch >> kwin-6.6.6/debian/patches/series
cd kwin-6.6.6 && dch -v 4:6.6.6-0ubuntu0.1+umbrahdr4 "Umbra: HDR virtual outputs, 10-bit screencast"
DEB_BUILD_OPTIONS="nocheck nodoc noautodbgsym" dpkg-buildpackage -b -uc -us
sudo dpkg -i ../kwin-common_*.deb ../kwin-data_*.deb ../kwin-wayland_*.deb ../libkwin6_*.deb
```

Then log out and in (KWin is the compositor). Trap: an incremental rebuild with `-nc` skips
compilation while `debian/debhelper-build-stamp` exists and silently repackages the old
binaries; delete the stamp first and check the new code is in the binary, for example
`strings /usr/lib/x86_64-linux-gnu/qt6/plugins/kwin/plugins/screencast.so | grep "offering HDR"`.

Rollback: reinstall the distribution's versions, e.g.
`sudo apt install --allow-downgrades kwin-common=4:6.6.6-0ubuntu0.1 kwin-data=4:6.6.6-0ubuntu0.1 kwin-wayland=4:6.6.6-0ubuntu0.1 libkwin6=4:6.6.6-0ubuntu0.1`.

## Measured on VEGA (RTX 2060 SUPER, NVIDIA 595.99.02), 2026-10-13

- Host log `KDE virtual displays offer HDR`; `/serverinfo` `ServerCodecModeSupport` 0x101 -> 0x301
  (Main10) and `UmbraCoopHdr` 0 -> 1.
- Automatic HDR desktop 3840x2160@165: KWin negotiated `XB30`, host `PipeWire DMA-BUF HDR/PQ,
  GPU frames` into NVENC Main10, client `HDR_MODE enabled=1` with BT.2020/PQ metadata
  (0.005-1000 nits), 3586 frames per 25 s probe (~161 fps). The recorded bitstream is
  `yuv420p10le, bt2020nc, smpte2084, bt2020`; a tone-mapped frame matches the desktop.
- Co-op pane 1920x2176@165 10-bit through the GPU HEVC encoder: `HDR_MODE enabled=1`, 3489 frames.
- 8-bit Automatic desktop 3840x2160@165 unchanged: 3570 frames.

KWin reports an HDR output to clients by its blending space (`wayland-info`: BT.2020 primaries,
`tf_named: gamma22`, max 1000, reference 200), not PQ. Umbra therefore trusts its own HDR switch
for its virtual output rather than the colour-management query.

## Measured on VEGA (AMD Radeon RX Vega, Umbra 0.5.2), 2026-10-15

Umbra 0.5.2 for Linux (the Flatpak), each distribution with its own KWin and with its KDE extra. Frames
received per 25 s probe; the RX Vega's encoder tops out at about 59 fps at 4K and 35 fps per co-op pane.

| Distribution | KWin | Packages | 4K desktop | Co-op pane, 8-bit | Co-op pane, 10-bit HDR | Disconnect + resume |
|---|---|---|---|---|---|---|
| Ubuntu 26.04 | 6.6.6 | Ubuntu's | 1470 | 881 | (SDR) | 1697 |
| Ubuntu 26.04 | 6.6.6 | `4:6.6.6-0ubuntu0.1+umbrahdr4` | 1470 | 891 | 889 | 1722 |
| Ubuntu 26.10 | 6.7.5 | Ubuntu's | 1459 | 863 | (SDR) | 1737 |
| Ubuntu 26.10 | 6.7.5 | `4:6.7.5-0ubuntu1+umbrahdr1` | 1463 | 859 | 910 | 1622 |
| Fedora 44 | 6.7.5 | Fedora's | 1494 | 767 | (SDR) | 1596 |
| Fedora 44 | 6.7.5 | `6.7.5-1.fc44.umbrahdr1` | 1465 | 866 | 893 | 1733 |
| Fedora 45 (pre-release) | 6.7.5 | Fedora's | 1454 | 320\* | (SDR) | 850\* |
| Fedora 45 (pre-release) | 6.7.5 | `6.7.5-1.fc45.umbrahdr1` | 1476 | 897 | 906 | 1778 |
| openSUSE Tumbleweed | 6.7.5 | openSUSE's | 1467 | 952 | (SDR) | 1939 |
| openSUSE Tumbleweed | 6.7.5 | `6.7.5-1.1.umbrahdr1` | 1464 | 994 | 923 | 1885 |

- With the extra: host log `KDE virtual displays offer HDR`, `/serverinfo` `UmbraCoopHdr` 0 -> 1. The HDR
  co-op pane goes through the GPU HEVC encoder (`GPU frames 1920x2176 PQ`), the client reports
  `HDR_MODE enabled=1`, and the recorded stream is `Main 10, yuv420p10le, bt2020nc, smpte2084, bt2020`:
  a white area reads 576-589 in 10-bit luma (an SDR pane reads 935-944), and the tone-mapped frame shows
  the desktop. `ServerCodecModeSupport` stays 0x101: the RX Vega's video encoder has no Main 10 profile,
  so the HDR desktop stream measured on NVIDIA above cannot run on this card.
- Without the extra every stream works and stays SDR; the 4K desktop and 8-bit pane rates match.
- KWin 6.7 reports Umbra's 165 Hz custom modes at its real rate (164.95 Hz at 3840x2160 and 164.85 Hz
  at 1920x2176 on Ubuntu 26.10 and openSUSE Tumbleweed, 164.99 Hz on Fedora); Umbra 0.5.2 takes the
  nearest mode of that size.
- Flatpak 1.18.4 (Ubuntu 26.10, Fedora 44 and 45, openSUSE Tumbleweed) exports Umbra's desktop file
  without `X-KDE-Wayland-Interfaces`, and KWin then refuses Umbra its screen-cast protocol. On Ubuntu
  26.10 the copy of the desktop file that keeps the key was made by hand. On Fedora 44, Fedora 45 and
  Tumbleweed, Umbra 0.5.2 wrote it at its first start through the application-menu entry's command; on
  a fresh Fedora 44 that first start already offered HDR.
- Umbra builds: the final 0.5.2 for Ubuntu 26.04 with the extra, Fedora 44 with the extra, Fedora 45 and
  Tumbleweed; earlier 0.5.2 candidates, which differ only in the desktop-file helper, for the other rows.
- \* Taken while a Mutter build ran on the same PC.

## Leg test in this directory

- GPU-frame hand-off probes moved to `../gpu-frames-probes/`.
- `alloc10-probe.c`: KWin's per-buffer allocation path (GBM with one modifier, EGL import, FBO)
  for every importable modifier of 10-bit and 8-bit formats.

Upstreaming: not yet submitted to KDE.
