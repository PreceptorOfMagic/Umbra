Umbra for Linux, as one Flatpak bundle, now with prebuilt HDR extras for seven distributions. This release is for Linux only; the Windows host stays on [Umbra 0.5.0](https://github.com/PreceptorOfMagic/Umbra/releases/tag/umbra-v0.5.0).

**Install guide:** [Umbra on Linux](https://preceptorofmagic.github.io/Eclipse-Umbra-Project-Hub/setup.html#umbra-linux) · [HDR extras](https://preceptorofmagic.github.io/Eclipse-Umbra-Project-Hub/setup.html#umbra-linux-hdr)

## What's new in 0.5.2

- **KDE Plasma 6.7.** Umbra 0.5.1 finds no KDE output on Plasma 6.7 (Ubuntu 26.10, Fedora 44 and 45, openSUSE Tumbleweed, Arch, CachyOS): KWin 6.7 announces its outputs only through a newer protocol, so 0.5.1 streams the desktop there without a virtual display per stream, co-op or HDR. 0.5.2 uses that protocol when KWin offers it, and the old one otherwise. KWin 6.7 also reports a custom mode at its real refresh rate (1920x1080 at 60 Hz comes back as 59.963 Hz), so 0.5.2 takes the nearest mode of the right size instead of an exact match.
- **Prebuilt HDR extras for seven distributions**, KDE and GNOME: Ubuntu 26.04 and 26.10, Fedora 44 and 45, openSUSE Tumbleweed, Arch Linux and CachyOS. Each is built from that distribution's own KWin or Mutter package with one patch added, so you no longer rebuild your compositor. The table below says how far each one has been tested. **The Fedora 45 GNOME extra is in progress:** when this release was built, Fedora 45's mirrors served the Mutter 51.1 package but only the 51.0 source, so it could not be built from the right source yet. It will be added to this release when it builds; until then this release has 13 extras.
- The GNOME patch for Mutter 50 (revision 2) adds two memory-leak fixes that Mutter made to the code it copies.
- **KDE Plasma with Flatpak 1.18.4 or newer.** Flatpak 1.18.4, a security update (2026-09-28), exports an app's desktop file with only a fixed list of keys, which drops the one KDE checks before it lets an app use its screen-cast protocol. KDE then refused Umbra its virtual displays, and Umbra fell back to sharing the desktop, without a virtual display per stream, co-op or HDR. Every distribution above except Ubuntu 26.04 ships that Flatpak. 0.5.2 puts a copy of its desktop file that keeps the key in your own applications folder (`~/.local/share/applications`) when you start it from the application menu or run Input Setup, and the removal helper deletes it. Where Flatpak still exports the key, no copy is made.

Nothing else changed: apart from the KDE output control (`src/platform/linux/kde-output.c` and the KDE protocol files it is built from) and the desktop-file helper (`packaging/linux/flatpak/scripts/umbra-kde-entry.sh`, run by the application-menu launcher, Input Setup and the removal helper), 0.5.2 is the code of 0.5.1.

## How far this release is tested

Testing is limited, and differs per download:

- **The Flatpak** was built by the [Linux release workflow](https://github.com/PreceptorOfMagic/Umbra/blob/umbra-linux-v0.5.2/.github/workflows/linux-release.yml) from this tag, installed, and checked to report 0.5.2 with every library found. The same code, built by the same workflow before the tag, ran on hardware on 2026-10-15, on one PC with an AMD Radeon RX Vega and Secure Boot on: Ubuntu 26.04 (KDE Plasma 6.6, GNOME 50), Ubuntu 26.10 (Plasma 6.7, GNOME 51), Fedora 44 (Plasma 6.7, GNOME 50), Fedora 45 (pre-release; Plasma 6.7, GNOME 51) and openSUSE Tumbleweed (Plasma 6.7, GNOME 50), each with the distribution's own compositor and with its HDR extra. On each: a 4K desktop stream on its own virtual display, a co-op pane, a co-op disconnect and resume at a new size, and with the extra a co-op pane in 10-bit HDR, checked in the recorded stream. **Not run with 0.5.2:** NVIDIA GPUs (0.5.1 was tested on an RTX 2060 SUPER), an HDR desktop stream (it needs a GPU with a 10-bit video encoder, which the RX Vega lacks), a co-op session with two hosts, Input Setup's administrator prompt and streamed controller input on these distributions, and Arch Linux and CachyOS, which cannot start on the test PC without changing its Secure Boot setup.
- **Every HDR extra** passed the automated test before this release was published: installed over its distribution's own packages in a clean container of that distribution, checked with its `README.txt` commands, the patched compositor run without a GPU (it must offer HDR and 10-bit capture on the virtual display Umbra asks for, and the stock compositor must not), then rolled back. That test does not use a real GPU or display, does not stream, and does not show HDR on a screen. **Nine of the thirteen extras in this release also ran on hardware**, with Umbra 0.5.2 on the RX Vega (the Fedora 44 GNOME one as a build of the same version made on the test PC, because Fedora updated Mutter after the release candidates were built); the Arch Linux and CachyOS ones did not. The Fedora 45 GNOME extra, still in progress, ran on hardware the same way:

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
| KDE Plasma 6.7 on Wayland (Ubuntu 26.10, Fedora 44 and 45, openSUSE Tumbleweed; KWin 6.7.5) | Yes | Yes | Yes | With the KDE HDR extra |
| GNOME on Wayland (Ubuntu 26.04 and 26.10, Fedora 44 and 45, openSUSE Tumbleweed; Mutter 50 and 51) | Yes | Yes | Yes | With the GNOME HDR extra; a physical HDR monitor streams HDR without it |
| X11 desktops (Cinnamon, Xfce, MATE …) | Yes (tested during development on Linux Mint 22.3) | No | No | No |
| Sway, Hyprland and other wlroots desktops, COSMIC | Not supported | | | |

- The "Yes" rows were tested on hardware with Umbra 0.5.2 on an AMD Radeon RX Vega (2026-10-15). "Co-op" there means this host's co-op pane: a stream, and a disconnect and resume at a new size. "HDR" means a co-op pane in 10-bit HDR; an HDR desktop stream needs a 10-bit video encoder, which the RX Vega lacks. The Plasma 6.6 and GNOME 50 rows were also tested with Umbra 0.5.1 on an NVIDIA RTX 2060 SUPER (driver 595), HDR desktop streams included. 0.5.2 has not run on NVIDIA; Intel GPUs and PCs with more than one GPU are untested.
- Other distributions running KDE Plasma or GNOME on Wayland are expected to behave like those rows but are untested. Two Linux hosts in one co-op session are untested.

<details>
<summary><strong>Testing in detail: each distribution and desktop, and exactly how it was tested</strong></summary>

Everything below ran on one test PC. The Umbra 0.5.2 tests used its AMD Radeon RX Vega from 2017, with Secure Boot on and each distribution on its own partition, started through its own Microsoft-signed boot loader; the Umbra 0.5.1 NVIDIA tests used an RTX 2060 SUPER in the same PC. The tests themselves are described after the tables.

**Tested in person**

| Distribution | Desktop | Umbra and GPU | How it was tested |
|---|---|---|---|
| Ubuntu 26.04 | KDE Plasma 6.6.6, with the KDE HDR extra | Umbra 0.5.1 development build, AMD Radeon RX Vega (2026-10-14); Umbra 0.5.2 (2026-10-15) | The in-person test. Then the smoke test with Ubuntu's KWin and with the extra `4:6.6.6-0ubuntu0.1+umbrahdr4` |
| Ubuntu 26.04 | GNOME 50.1, with the GNOME HDR extra | Umbra 0.5.1 development builds, AMD Radeon RX Vega and NVIDIA RTX 2060 SUPER (2026-10-14); Umbra 0.5.2 (2026-10-15) | The in-person test (on NVIDIA, an HDR co-op session), with the extra's previous revision. Then the smoke test with Ubuntu's Mutter and with the extra `50.1-0ubuntu2.4+umbrahdr2`, and a memory check: seven back-to-back 290 s HDR co-op streams, gnome-shell's memory flat (the picture moved only in the first 3 minutes) |

**Smoke-tested on hardware (Umbra 0.5.2, AMD Radeon RX Vega, 2026-10-15)**

| Distribution | Desktop | Packages tested | How it was tested |
|---|---|---|---|
| Ubuntu 26.10 | KDE Plasma 6.7.5 | KWin `6.7.5-0ubuntu1`; extra `4:6.7.5-0ubuntu1+umbrahdr1` | The smoke test with each. It ran a 0.5.2 candidate from before the desktop-file fix, with that copy made by hand (the fix itself was tested on Fedora and Tumbleweed). Umbra 0.5.1 was also run: no virtual display, as documented |
| Ubuntu 26.10 | GNOME 51.0 | Mutter `51.0-1ubuntu3`; extra `51.0-1ubuntu3+umbrahdr1` | The smoke test with each |
| Fedora 44 | KDE Plasma 6.7.5 | KWin `6.7.5-1.fc44`; extra `6.7.5-1.fc44.umbrahdr1` | The smoke test with each. On a fresh install, the first start from the application-menu entry wrote the desktop-file copy and offered HDR at once |
| Fedora 44 | GNOME 50.6 | Mutter `50.6-1.fc44`; extra `50.6-1.fc44.umbrahdr2`, built on the test PC | The smoke test with each. Fedora updated Mutter after the release's extras were first built, so the extra was built on the test PC from the same patch; the published one is rebuilt the same way |
| Fedora 45 (pre-release) | KDE Plasma 6.7.5 | KWin `6.7.5-1.fc45`; extra `6.7.5-1.fc45.umbrahdr1` | The smoke test with each; the first start wrote the desktop-file copy |
| Fedora 45 (pre-release) | GNOME 51.1 | Mutter `51.1-1.fc45`; extra `51.1-1.fc45.umbrahdr1`, built on the test PC | The smoke test with each, as for Fedora 44 GNOME. **The extra is in progress:** it is not in the release yet, because Fedora 45's mirrors served the Mutter 51.1 package but only the 51.0 source when the release was built |
| openSUSE Tumbleweed | KDE Plasma 6.7.5 | kwin6 `6.7.5-1.1`; extra `6.7.5-1.1.umbrahdr1` | The smoke test with each; the first start wrote the desktop-file copy |
| openSUSE Tumbleweed | GNOME 50.5 | mutter `50.5-3.1`; extra `50.5-3.1.umbrahdr2` | The smoke test with each. Co-op panes ran at about 20 fps here (27 to 39 fps elsewhere), with or without the extra; Umbra 0.5.1 measured the same |

**Not tested, expected to work (the same packages or versions as a tested row)**

| Distribution | Desktop | Why it should work | HDR extra |
|---|---|---|---|
| Arch Linux | KDE Plasma 6.7.5, GNOME 51 | The same KWin and Mutter releases as the smoke-tested rows. Not run: the test PC cannot start Arch without changing its Secure Boot setup | Automated test only |
| CachyOS | KDE Plasma 6.7.5, GNOME 51 | As Arch; its GNOME uses CachyOS's own Mutter build. Not run, for the same reason | Automated test only |
| Kubuntu, Ubuntu Studio and other Ubuntu 26.04 or 26.10 flavours with KDE | KDE Plasma 6.6 or 6.7 | They install the same KWin packages from Ubuntu's archive as the Ubuntu rows | The Ubuntu KDE extra fits |
| Fedora Kinoite and Silverblue 44, 45 | KDE Plasma 6.7, GNOME | The same KWin and Mutter packages as Fedora | Untested: installing it needs `rpm-ostree`, not the `dnf` command in its README |
| EndeavourOS | KDE Plasma 6.7, GNOME 51 | It uses Arch's own repositories | As Arch |
| Other distributions, for example KDE neon or Manjaro | KDE Plasma 6.6 or 6.7, GNOME 50 or 51, on Wayland | The same desktop versions as tested, in the distribution's own builds | A prebuilt extra fits only if its README's version check passes |

**Unlikely to work, or limited**

| Distribution or desktop | What to expect |
|---|---|
| Debian 13, openSUSE Leap 16.0 | No HDR: their KWin and Mutter are too old for the patches. Umbra itself is untested there |
| Fedora 43 | No HDR: its PipeWire cannot announce HDR capture, and its Mutter 49 has no HDR capture. Umbra itself is untested there |
| X11 desktops (Cinnamon, Xfce, MATE …), for example Linux Mint | Streaming only: no virtual display per stream, co-op or HDR (Linux Mint 22.3 tested during development) |
| Sway, Hyprland and other wlroots desktops, COSMIC (Pop!_OS) | Not supported |

**GPUs.** Umbra 0.5.2 ran only on the AMD Radeon RX Vega. Its encoder limits 4K to about 59 fps and co-op panes to about 35 fps, and it has no 10-bit video encoder, so HDR was tested through co-op panes only. Umbra 0.5.1 was also smoke-tested on an NVIDIA RTX 2060 SUPER (driver 595) on Ubuntu 26.04 with KDE and GNOME, HDR desktop streams included. Intel GPUs and PCs with more than one GPU are untested.

**The tests**

- **In-person test (2026-10-14):** the maintainer used Umbra on the LG TV through Eclipse: a co-op session with the test PC and a Windows Umbra host. It passed; on AMD with the note that the 2017 card's encoder limited how far it could be pushed.
- **Smoke test (2026-10-15):** scripted, with nobody at the PC:
  1. Umbra installed from the bundle with `flatpak install --user`, as in the install steps, and started with the application-menu entry's command. Input Setup was deferred, so its administrator prompt and streamed controllers were not exercised.
  2. A test client on another PC on the network, built on moonlight-common-c (the streaming library Eclipse uses), paired with Umbra and asked for 25-second streams while a moving test window ran on the desktop: the desktop at 3840x2160 and 165 Hz on its own virtual display; the same in HDR (which cannot pass on the RX Vega, as it has no 10-bit video encoder); the co-op pane at 1920x2176, in 8-bit and in 10-bit HDR; the HDR co-op pane recorded and checked (HEVC Main 10 with BT.2020 and PQ tags, a white area at 10-bit brightness of about 576 to 595 where SDR reads 935 to 974, and one frame tone-mapped and looked at); and a co-op disconnect followed by a resume at a new size (2176x1920).
  3. A stream passed when frames arrived, nothing failed and, where asked, HDR was on. Decoded frames were looked at, and no virtual display may remain after the last stream.
  4. Each distribution and desktop ran it twice: with its own compositor, and with its HDR extra installed as its README says (the "ONLY FOR" check, `sha256sum -c`, the install command, a restart, the VERIFY commands).
- **Automated test (every extra, before it is published):** the extra is installed over the distribution's own packages in a clean container of that distribution and checked with its README.txt commands. The patched compositor then runs without a GPU and must offer HDR and 10-bit capture on the virtual display Umbra asks for, as the stock one must not; then the extra is rolled back. No real GPU, display or stream is involved.

</details>

## Known limits

- **AMD:** the only AMD card available for testing is a Radeon RX Vega from 2017. Its video encoder reaches about 60 fps at 4K and 165 fps only at 1080p, and its co-op panes run at about 35 fps. The same card hit the same 4K limit on Windows (62 fps), so its age, not Umbra, is the likely limit; newer AMD cards could not be tested. It has no 10-bit encoder, so its ordinary streams are SDR (co-op HDR works through the GPU HEVC encoder).
- **HDR extras replace your desktop's compositor packages.** A distribution update of KWin or Mutter replaces them again and streams return to SDR until a matching extra is published; reinstalling the distribution's package undoes them (each `README.txt` has the command). Arch and CachyOS extras keep the repository's version number, so `pacman -Qi kwin` or `pacman -Qi mutter` shows `Packager: Umbra HDR extra` while they are installed.
- **Not covered:** Debian 13 and openSUSE Leap 16.0 (KWin and Mutter too old for the patches) and Fedora 43 (its PipeWire cannot announce HDR capture, and its Mutter 49 has no HDR capture). On other distributions, add the [KWin patch](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.2/packaging/linux/kwin-virtual-hdr) or [Mutter patch](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.2/packaging/linux/mutter-virtual-hdr) for your version to your distribution's own package and rebuild it; [hdr-extras](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.2/packaging/linux/hdr-extras) shows how the prebuilt ones are made.
- **KDE with Flatpak 1.18.4 or newer:** start Umbra once from the application menu, or run Input Setup, before you stream. Started only with `flatpak run` from a terminal, it does not write the desktop-file copy KDE needs and streams the desktop without virtual displays. If you uninstall without the removal helper, delete `~/.local/share/applications/io.github.PreceptorOfMagic.Umbra.desktop` yourself.
- **openSUSE Tumbleweed with GNOME:** co-op panes ran at about 20 fps on the RX Vega, against 27 to 39 fps on the other distributions and on Tumbleweed with KDE, with or without the GNOME extra. Umbra 0.5.1 measured the same there; the cause was not found.
- **Fedora 45 GNOME extra: in progress.** It is not in this release yet (see What's new). Fedora 45 itself is still a pre-release.
- **Arch Linux and CachyOS were not tested on hardware.** The test PC starts only with Secure Boot through Microsoft's 2011 UEFI certificate, neither distribution ships a boot loader signed with it, and the PC's Secure Boot setup was not changed for this test. Their HDR extras passed the automated test only.
- **X11** has no way to create a display while it runs, so the host streams the desktop there but never offers co-op or HDR.
