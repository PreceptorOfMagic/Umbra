Umbra for Linux, as one Flatpak bundle, now with prebuilt HDR extras for seven distributions. This release is for Linux only; the Windows host stays on [Umbra 0.5.0](https://github.com/PreceptorOfMagic/Umbra/releases/tag/umbra-v0.5.0).

**Install guide:** [Umbra on Linux](https://preceptorofmagic.github.io/Eclipse-Umbra-Project-Hub/setup.html#umbra-linux) · [HDR extras](https://preceptorofmagic.github.io/Eclipse-Umbra-Project-Hub/setup.html#umbra-linux-hdr)

## What's new in 0.5.2

- **KDE Plasma 6.7.** Umbra 0.5.1 finds no KDE output on Plasma 6.7 (Ubuntu 26.10, Fedora 44 and 45, openSUSE Tumbleweed, Arch, CachyOS): KWin 6.7 announces its outputs only through a newer protocol, so 0.5.1 streams the desktop there without a virtual display per stream, co-op or HDR. 0.5.2 uses that protocol when KWin offers it, and the old one otherwise. KWin 6.7 also reports a custom mode at its real refresh rate (1920x1080 at 60 Hz comes back as 59.963 Hz), so 0.5.2 takes the nearest mode of the right size instead of an exact match.
- **Prebuilt HDR extras for seven distributions**, KDE and GNOME: Ubuntu 26.04 and 26.10, Fedora 44 and 45, openSUSE Tumbleweed, Arch Linux and CachyOS. Each is built from that distribution's own KWin or Mutter package with one patch added, so you no longer rebuild your compositor. The table below says how far each one has been tested.
- The GNOME patch for Mutter 50 (revision 2) adds two memory-leak fixes that Mutter made to the code it copies.

Nothing else changed: apart from the KDE output control (`src/platform/linux/kde-output.c` and the KDE protocol files it is built from), 0.5.2 is the code of 0.5.1.

## How far this release is tested

Testing is limited, and differs per download:

- **The Flatpak** was built by the [Linux release workflow](https://github.com/PreceptorOfMagic/Umbra/blob/umbra-linux-v0.5.2/.github/workflows/linux-release.yml) from this tag, installed, and checked to report 0.5.2 with every library found. **It has not been run on hardware.** Its Plasma 6.7 support has been tested only automatically: with each distribution's real KWin 6.7.5 on a virtual display device without a GPU, Umbra's output control finds the virtual output, resizes it to the stream's mode and switches it to HDR. Streaming, co-op and HDR on a real Plasma 6.7 PC are untested. Everything else is 0.5.1's code, tested on hardware as listed under "What was tested".
- **Every HDR extra** passed the automated test before this release was published: installed over its distribution's own packages in a clean container of that distribution, checked with its `README.txt` commands, the patched compositor run without a GPU (it must offer HDR and 10-bit capture on the virtual display Umbra asks for, and the stock compositor must not), then rolled back. That test does not use a real GPU or display, does not stream, and does not show HDR on a screen. **Only the two Ubuntu 26.04 extras have run on hardware**, with Umbra 0.5.1, and the GNOME one in its previous revision:

<!-- HDR extras table -->

Each extra's `README.txt` repeats its own line in more detail (TESTED), names the exact KWin or Mutter version it fits (install it over no other) and has the install, verify and rollback steps.

## Downloads

| File | What it is |
|---|---|
| `Umbra-0.5.2-linux-x86_64.flatpak` (+ `.sha256`) | Umbra for Linux |
| `Umbra-HDR-KDE-<distribution>.tar.gz` | Optional. Patched KWin packages for that distribution: HDR on KDE virtual displays. See the table above |
| `Umbra-HDR-GNOME-<distribution>.tar.gz` | Optional. Patched Mutter packages for that distribution: HDR on GNOME virtual monitors. See the table above |
| `Umbra-HDR-*-source.tar.gz` | Source of each HDR extra: the distribution's source package with the patch integrated, plus the patch |
| `umbra-linux-0.5.2-project-source.tar.gz` | Umbra's source at this release, with how the bundle was built (`packaging/linux/flatpak-umbra/DEVELOPMENT.md`) |
| `Umbra-0.5.2-linux-flatpak-sources.tar.gz` | Every third-party source the Flatpak build fetched (NVIDIA's CUDA compiler components are listed in `CUDA-SOURCES.txt`, not included) |
| `SHA256SUMS` | Checksums for every file above |

## Install

```sh
sudo apt install flatpak    # or your distribution's package manager
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
sha256sum -c Umbra-0.5.2-linux-x86_64.flatpak.sha256
flatpak install --user Umbra-0.5.2-linux-x86_64.flatpak
```

Start **Umbra** from the applications menu, accept **Input Setup**, open `https://localhost:47990` to create the administrator login, and pair Eclipse with its PIN. To update from 0.5.1, install with `--reinstall` added (`flatpak install --user --reinstall Umbra-0.5.2-linux-x86_64.flatpak`); settings and pairings are kept. If you used the 0.5.0 `.deb`, remove it first (`systemctl --user disable --now sunshine`, then `sudo apt remove umbra`) and pair Eclipse again: the Flatpak keeps its own settings.

## What was tested

| Desktop | Streaming | Own virtual display per stream | Co-op | HDR |
|---|---|---|---|---|
| KDE Plasma 6.6 on Wayland (Ubuntu 26.04, KWin 6.6.6) | Yes | Yes | Yes | With the KDE HDR extra |
| KDE Plasma 6.7 on Wayland (KWin 6.7.5) | Expected; untested on hardware | Automated test only | Untested | With the KDE HDR extra; automated test only |
| GNOME on Wayland (Ubuntu 26.04, Mutter 50.1) | Yes | Yes | Yes | With the GNOME HDR extra; a physical HDR monitor streams HDR without it |
| X11 desktops (Cinnamon, Xfce, MATE …) | Yes (tested during development on Linux Mint 22.3) | No | No | No |
| Sway, Hyprland and other wlroots desktops, COSMIC | Not supported | | | |

- The "Yes" rows were tested on hardware with Umbra 0.5.1, which 0.5.2 matches outside the KDE output control. GPUs: NVIDIA RTX 2060 SUPER (driver 595) and AMD Radeon RX Vega. Intel GPUs and PCs with more than one GPU are untested.
- Other distributions running KDE Plasma or GNOME on Wayland are expected to behave like those rows but are untested. Two Linux hosts in one co-op session are untested.

## Known limits

- **AMD:** the only AMD card available for testing is a Radeon RX Vega from 2017. Its video encoder reaches about 60 fps at 4K and 165 fps only at 1080p, and its co-op panes run at about 35 fps. The same card hit the same 4K limit on Windows (62 fps), so its age, not Umbra, is the likely limit; newer AMD cards could not be tested. It has no 10-bit encoder, so its ordinary streams are SDR (co-op HDR works through the GPU HEVC encoder).
- **HDR extras replace your desktop's compositor packages.** A distribution update of KWin or Mutter replaces them again and streams return to SDR until a matching extra is published; reinstalling the distribution's package undoes them (each `README.txt` has the command). Arch and CachyOS extras keep the repository's version number, so `pacman -Qi kwin` or `pacman -Qi mutter` shows `Packager: Umbra HDR extra` while they are installed.
- **Not covered:** Debian 13 and openSUSE Leap 16.0 (KWin and Mutter too old for the patches) and Fedora 43 (its PipeWire cannot announce HDR capture, and its Mutter 49 has no HDR capture). On other distributions, add the [KWin patch](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.2/packaging/linux/kwin-virtual-hdr) or [Mutter patch](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.2/packaging/linux/mutter-virtual-hdr) for your version to your distribution's own package and rebuild it; [hdr-extras](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.2/packaging/linux/hdr-extras) shows how the prebuilt ones are made.
- **X11** has no way to create a display while it runs, so the host streams the desktop there but never offers co-op or HDR.
