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
| Linux x86_64 gaming PC | Umbra Flatpak bundle, plus optional HDR extras for Ubuntu 26.04 | [Linux steps](#linux-x86_64-flatpak) |
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

Download `Umbra-0.5.1-linux-x86_64.flatpak` from the [Umbra 0.5.1 for Linux release](https://github.com/PreceptorOfMagic/Umbra/releases/tag/umbra-linux-v0.5.1).

1. Install Flatpak and add Flathub if you have not already: `sudo apt install flatpak`, then `flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo`.
2. Check the download: `sha256sum -c Umbra-0.5.1-linux-x86_64.flatpak.sha256`.
3. Install it: `flatpak install --user Umbra-0.5.1-linux-x86_64.flatpak`. Flatpak fetches the Freedesktop 25.08 runtime and, on NVIDIA, the driver extension matching your installed driver.
4. Start **Umbra** from the application menu. Accept **Input Setup** when it is offered: it adds the input rule streamed controllers, mouse and keyboard need, through your desktop's administrator prompt, and can start Umbra when you sign in. If the desktop asks to allow screen sharing, allow it.
5. Open `https://localhost:47990`, create the administrator login and pair Eclipse using its PIN.

| Desktop | Streaming | Per-client virtual display | Co-op | HDR |
|---|---|---|---|---|
| KDE Plasma, Wayland (tested: Ubuntu 26.04, KWin 6.6.6) | Yes | Yes | Yes | With the KDE HDR extra |
| GNOME, Wayland (tested: Ubuntu 26.04, Mutter 50.1) | Yes | Yes | Yes | With the GNOME HDR extra; a physical HDR monitor streams HDR without it |
| X11 desktops (Cinnamon, Xfce, MATE …) | Yes (tested during development on Linux Mint 22.3) | No | No | No |
| Sway, Hyprland and other wlroots desktops, COSMIC | Not supported | | | |

- GPUs: NVIDIA (tested: RTX 2060 SUPER, driver 595) and AMD (tested: Radeon RX Vega). The only AMD card available for testing is a Radeon RX Vega from 2017: its video encoder reaches about 60 fps at 4K and 165 fps only at 1080p, and its co-op panes about 35 fps. The same card hit the same 4K limit on Windows (62 fps), so its age, not Umbra, is the likely limit; newer AMD cards could not be tested. Intel GPUs and multi-GPU systems are untested.
- Other distributions running KDE Plasma or GNOME on Wayland are expected to behave like those rows but are untested.
- A Linux host and a Windows host can share one co-op session (tested with Umbra 0.5.0 on Windows).
- **HDR extras** (optional): stock KWin and Mutter keep the virtual displays Umbra streams SDR. Two small patches, to KWin 6.6.6 (KDE) and Mutter 50.1 (GNOME), add HDR virtual displays and 10-bit screen capture; Umbra detects them automatically. Like the Flatpak they were tested on Ubuntu 26.04, and both apply unchanged to KDE's and GNOME's own 6.6.6 and 50.1 releases, so other distributions shipping those versions are expected to work but are untested. The release carries prebuilt packages for Ubuntu 26.04; elsewhere, add the patch to your distribution's own `kwin` or `mutter` package and rebuild it. They replace your desktop's compositor packages, a distribution update of KWin or Mutter replaces them again, and reinstalling the distribution's package undoes them. Install, verify and rollback steps are in each extra's `README.txt`, and the patches are in [kwin-virtual-hdr](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.1/packaging/linux/kwin-virtual-hdr) and [mutter-virtual-hdr](https://github.com/PreceptorOfMagic/Umbra/tree/umbra-linux-v0.5.1/packaging/linux/mutter-virtual-hdr).
- To remove Umbra: `flatpak run --command=remove-additional-install.sh io.github.PreceptorOfMagic.Umbra` removes the input rule and sign-in start, then `flatpak uninstall --user io.github.PreceptorOfMagic.Umbra` removes the app.

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

[Windows build guide](../docs/umbra-windows-build.md) · [Linux Flatpak build](https://github.com/PreceptorOfMagic/Umbra/blob/umbra-linux-v0.5.1/packaging/linux/flatpak-umbra/DEVELOPMENT.md) · [Development and milestones](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/development.md) · [Acknowledgements](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/ACKNOWLEDGEMENTS.md)

[Eclipse/Umbra licence & notices](../LICENSES.md) · [GPL-3.0](../LICENSE)
