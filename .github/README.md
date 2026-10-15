# Umbra

**The gaming-PC app: a Windows and Linux host for everyday streaming and Eclipse’s two-host couch co-op.**

[Project Hub](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub) · [Installation Guide](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/installation.md) · [Eclipse](https://github.com/PreceptorOfMagic/Eclipse) · [Umbra](https://github.com/PreceptorOfMagic/Umbra) · [Development](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/development.md) · [Acknowledgements](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/ACKNOWLEDGEMENTS.md)

New here? [The Project Hub](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub) explains both applications, requirements and tested hardware. This repository owns Umbra source and host release files. Umbra descends from Sunshine and Apollo.

## Umbra and Apollo

Umbra is [Apollo](https://github.com/ClassicOldSong/Apollo) with the additions Eclipse needs for co-op, and as little else changed as possible. Apollo is ClassicOldSong's project and is still maintained, so Umbra follows Apollo's releases instead of redoing its work: Umbra's additions are kept apart from Apollo's own code wherever they can be, so each new Apollo release can be merged in. Umbra's web interface says which Apollo release it is built on, and whether a newer Apollo release is out that Umbra has not caught up with yet.

Umbra is an independent fork. It is not affiliated with or endorsed by ClassicOldSong (Apollo), SudoMaker or LizardByte (Sunshine). The links to Apollo's resources that Umbra keeps go to Apollo's own pages; report Umbra problems here, not to Apollo.

## Downloads and installation

Open [Umbra releases](https://github.com/PreceptorOfMagic/Umbra/releases), choose the compatible release and expand **Assets**. Download the **Windows x64 installer .exe** or the **Linux .flatpak**, not GitHub’s automatically generated source archives. Windows and Linux are published as separate releases. If no installer is attached, there is no published installation package yet.

| Device | Package | Install |
|---|---|---|
| Windows x64 gaming PC | Umbra Windows installer, with bundled virtual-display and device components | [Windows steps](#windows-x64) |
| Linux x86_64 gaming PC | Umbra Flatpak bundle, plus optional HDR extras for Ubuntu, Fedora, openSUSE Tumbleweed, Arch and CachyOS | [Linux steps](#linux-x86_64-flatpak) |
| LG TV / Windows / Linux display device | Eclipse client, from its own repository | [Eclipse downloads](https://github.com/PreceptorOfMagic/Eclipse#downloads-and-installation) |

### Windows x64

1. Download the installer and matching checksum if provided. Compare the file’s SHA-256 hash with PowerShell’s `Get-FileHash`. The installer is unsigned for now; signing through SignPath Foundation is being set up (see the [Code signing policy](../CODE_SIGNING_POLICY.md)).
2. Stop any existing Sunshine/Apollo host service before starting Umbra. Back up its configuration before migration.
3. Run the installer with administrator approval and include its bundled virtual-display/controller/device components. Do not download the device bridge separately.
4. Allow access on your trusted private network. Use the installed shortcut or tray action to open the web interface and create the local administrator login.
5. Review game/desktop entries, keep client-driven display defaults and pair Eclipse using its PIN.

Windows 11 x64 on RTX 4070, RTX 3070 and Radeon RX Vega is the current physical test base. macOS host packages are not an Umbra release target here.

[Full Windows host installation](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/installation.md#umbra-windows).

### Linux x86_64 (Flatpak)

Download `Umbra-0.5.2-linux-x86_64.flatpak` from the [Umbra 0.5.2 for Linux release](https://github.com/PreceptorOfMagic/Umbra/releases/tag/umbra-linux-v0.5.2).

1. Install Flatpak and add Flathub if you have not already (on Ubuntu: `sudo apt install flatpak`), then `flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo`.
2. Check the download: `sha256sum -c Umbra-0.5.2-linux-x86_64.flatpak.sha256`.
3. Install it: `flatpak install --user Umbra-0.5.2-linux-x86_64.flatpak` (from 0.5.1, add `--reinstall`; settings and pairings are kept). Flatpak fetches the Freedesktop 25.08 runtime and, on NVIDIA, the driver extension matching your installed driver.
4. Start **Umbra** from the application menu. Accept **Input Setup** when it is offered: it adds the input rule streamed controllers, mouse and keyboard need, through your desktop's administrator prompt, and can start Umbra when you sign in. If the desktop asks to allow screen sharing, allow it.
5. Open `https://localhost:47990`, create the administrator login and pair Eclipse using its PIN.

| Desktop | Streaming | Per-client virtual display | Co-op | HDR |
|---|---|---|---|---|
| KDE Plasma 6.6, Wayland (tested: Ubuntu 26.04, KWin 6.6.6) | Yes | Yes | Yes | With the KDE HDR extra |
| KDE Plasma 6.7, Wayland (tested: Ubuntu 26.10, Fedora 44 and 45, openSUSE Tumbleweed; KWin 6.7.5) | Yes | Yes | Yes | With the KDE HDR extra |
| GNOME, Wayland (tested: Ubuntu 26.04 and 26.10, Fedora 44 and 45, openSUSE Tumbleweed; Mutter 50 and 51) | Yes | Yes | Yes | With the GNOME HDR extra; a physical HDR monitor streams HDR without it |
| X11 desktops (Cinnamon, Xfce, MATE …) | Yes (tested during development on Linux Mint 22.3) | No | No | No |
| Sway, Hyprland and other wlroots desktops, COSMIC | Not supported | | | |

- **How far 0.5.2 is tested:** on hardware on 2026-10-15, on one PC with an AMD Radeon RX Vega and Secure Boot on, on every desktop in the "Yes" rows: Ubuntu 26.04 and 26.10, Fedora 44 and 45 (pre-release) and openSUSE Tumbleweed, each with the distribution's own compositor and with its HDR extra. On each: a 4K desktop stream on its own virtual display, a co-op pane, a co-op disconnect and resume at a new size, and with the extra a co-op pane in 10-bit HDR, checked in the recorded stream. Not run with 0.5.2: NVIDIA GPUs (0.5.1 was tested on an RTX 2060 SUPER, HDR desktop streams included), an HDR desktop stream (it needs a 10-bit video encoder, which the RX Vega lacks), a co-op session with two hosts, Input Setup's administrator prompt and streamed controller input on the new distributions, and Arch Linux and CachyOS, which cannot start on the test PC without changing its Secure Boot setup. On openSUSE Tumbleweed with GNOME, co-op panes ran at about 20 fps on the RX Vega (27 to 39 fps elsewhere); Umbra 0.5.1 measured the same there.
- **KDE with Flatpak 1.18.4 or newer** (every distribution above except Ubuntu 26.04): Flatpak no longer exports the desktop-file key KDE checks before it grants screen capture, so Umbra writes a copy of its desktop file that keeps it to `~/.local/share/applications` when started from the application menu or Input Setup. Start it that way once before streaming; the removal helper deletes the copy.
- GPUs: NVIDIA (tested: RTX 2060 SUPER, driver 595) and AMD (tested: Radeon RX Vega). The only AMD card available for testing is a Radeon RX Vega from 2017: its video encoder reaches about 60 fps at 4K and 165 fps only at 1080p, and its co-op panes about 35 fps. The same card hit the same 4K limit on Windows (62 fps), so its age, not Umbra, is the likely limit; newer AMD cards could not be tested. Intel GPUs and multi-GPU systems are untested.
- Other distributions running KDE Plasma or GNOME on Wayland are expected to behave like those rows but are untested.
- A Linux host and a Windows host can share one co-op session (tested with Umbra 0.5.0 on Windows).
- **HDR extras** (optional): stock KWin and Mutter keep the virtual displays Umbra streams SDR. Small patches to KWin (KDE) and Mutter (GNOME) add HDR virtual displays and 10-bit screen capture; Umbra detects them automatically. The release carries them prebuilt, each built from that distribution's own KWin or Mutter package. Their testing is limited, and differs per extra:

  | Distribution | KDE extra | GNOME extra |
  |---|---|---|
  | Ubuntu 26.04 | KWin 6.6: tested on hardware with Umbra 0.5.2 (AMD) and 0.5.1 (NVIDIA, AMD) | Mutter 50: tested on hardware with Umbra 0.5.2 (AMD); its previous revision also with 0.5.1 (NVIDIA, AMD) |
  | Ubuntu 26.10 | KWin 6.7: tested on hardware with Umbra 0.5.2 (AMD) | Mutter 51: tested on hardware with Umbra 0.5.2 (AMD) |
  | Fedora 44 | KWin 6.7: tested on hardware with Umbra 0.5.2 (AMD) | Mutter 50: a build of the same version tested on hardware with Umbra 0.5.2 (AMD) |
  | Fedora 45 | KWin 6.7: tested on hardware with Umbra 0.5.2 (AMD) | Mutter 51: **in progress**, not in the release yet (Fedora 45's mirrors served the 51.1 package but only the 51.0 source); a build of the same version was tested on hardware with Umbra 0.5.2 (AMD) |
  | openSUSE Tumbleweed | KWin 6.7: tested on hardware with Umbra 0.5.2 (AMD) | Mutter 50: tested on hardware with Umbra 0.5.2 (AMD) |
  | Arch Linux | KWin 6.7: automated test only (cannot start on the test PC with Secure Boot on) | Mutter 51: automated test only (same) |
  | CachyOS | KWin 6.7: automated test only (same) | Mutter 51 (CachyOS's own): automated test only (same) |

  The automated test runs for every extra before it is published: it is installed over its distribution's own packages in a clean container of that distribution and checked with its `README.txt` commands, the patched compositor runs without a GPU and must offer HDR and 10-bit capture on the virtual display Umbra asks for (the stock one must not), and it is rolled back. It does not use a real GPU or display, does not stream and does not show HDR on a screen. "Tested on hardware" means a co-op pane streamed in 10-bit HDR on an AMD Radeon RX Vega and the ordinary streams worked as on the stock compositor; an HDR desktop stream and NVIDIA GPUs were not run with these extras, except the Ubuntu 26.04 ones with Umbra 0.5.1. Each `README.txt` gives the exact version tested. The KWin 6.7 extras need Umbra 0.5.2.

  Download `Umbra-HDR-KDE-<distribution>.tar.gz` or `Umbra-HDR-GNOME-<distribution>.tar.gz` from the release, or pick yours on the [Project Hub](https://preceptorofmagic.github.io/Eclipse-Umbra-Project-Hub/setup.html#umbra-linux-hdr), which also explains how Umbra detects it. Its `README.txt` names the exact KWin or Mutter version it fits (install it over no other), says how far it has been tested, and has the install, verify and rollback steps. They replace your desktop's compositor packages, a distribution update of KWin or Mutter replaces them again (streams are SDR until a matching extra is published), and reinstalling the distribution's package undoes them. Debian 13, openSUSE Leap 16.0 and Fedora 43 ship KWin, Mutter or PipeWire too old for the patches. On other distributions, add the matching patch from [kwin-virtual-hdr](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.2/packaging/linux/kwin-virtual-hdr) or [mutter-virtual-hdr](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.2/packaging/linux/mutter-virtual-hdr) to your own `kwin` or `mutter` package and rebuild it; [hdr-extras](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.2/packaging/linux/hdr-extras) shows how the prebuilt ones are made.
- To remove Umbra: `flatpak run --command=remove-additional-install.sh io.github.PreceptorOfMagic.Umbra` removes the input rule and sign-in start, then `flatpak uninstall --user io.github.PreceptorOfMagic.Umbra` removes the app.

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

## What Umbra provides

- PIN pairing, application management and local browser configuration.
- Granular per-device viewing, application launch, input, clipboard and command permissions.
- On-demand Windows virtual display creation at the client-requested resolution and refresh rate, with per-device display overrides and lifecycle-aware cleanup.
- Client-requested HDR where Windows, the GPU, driver, display and receiving client support it.
- Runtime probing of NVIDIA NVENC, AMD AMF, Intel Quick Sync and software encoder paths.
- Low-delay stereo, 5.1 and 7.1 audio for ordinary sessions; co-op requests compatible stereo streams for client-side mixing or selection.
- Keyboard, mouse and controller input, optional input-only access, clipboard synchronisation, connection hooks and resumable sessions.
- Host capabilities, session events and logs that Eclipse can include in its diagnostic bundle.

Normal one-host streaming remains the default experience. Co-op extends the same host rather than replacing it.

## First stream and co-op

Start with a normal one-PC game or desktop stream to Eclipse. For co-op, install the same compatible Umbra release on a second PC and verify each host individually. Choose **Co-op** in Eclipse, select the hosts/layout and assign player inputs.

Working co-op defaults are separate from ordinary streaming settings. The Co-op Pane appears automatically through first co-op use; users should not create app entries, copy IDs or set encoder parameters manually. The custom host encoder and client composition contract are the reason Eclipse and Umbra are developed together for cross-vendor co-op.

## Reporting and contributing

Use [Umbra issues](https://github.com/PreceptorOfMagic/Umbra/issues/new/choose) for installation, capture, virtual display, host service, pairing or encoder problems. Include both app versions, Windows or Linux distribution and desktop, GPU/driver, reproduction steps and the [reviewed diagnostic bundle](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/installation.md#diagnostics).

[Windows build guide](../docs/umbra-windows-build.md) · [Linux Flatpak build](https://github.com/PreceptorOfMagic/Umbra/blob/umbra-linux-v0.5.2/packaging/linux/flatpak-umbra/DEVELOPMENT.md) · [Development and milestones](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/development.md) · [Acknowledgements](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/ACKNOWLEDGEMENTS.md)

[Eclipse/Umbra licence & notices](../LICENSES.md) · [GPL-3.0](../LICENSE)
