# Umbra

**The gaming-PC app: a Windows host for everyday streaming and Eclipse’s two-host couch co-op.**

[Project Hub](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub) · [Installation Guide](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/installation.md) · [Eclipse](https://github.com/PreceptorOfMagic/Eclipse) · [Umbra](https://github.com/PreceptorOfMagic/Umbra) · [Development](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/development.md) · [Acknowledgements](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/ACKNOWLEDGEMENTS.md)

New here? [The Project Hub](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub) explains both applications, requirements and tested hardware. This repository owns Umbra source and host release files. Umbra descends from Sunshine and Apollo.

## Umbra and Apollo

Umbra is [Apollo](https://github.com/ClassicOldSong/Apollo) with the additions Eclipse needs for co-op, and as little else changed as possible. Apollo is ClassicOldSong's project and is still maintained, so Umbra follows Apollo's releases instead of redoing its work: Umbra's additions are kept apart from Apollo's own code wherever they can be, so each new Apollo release can be merged in. Umbra's web interface says which Apollo release it is built on, and whether a newer Apollo release is out that Umbra has not caught up with yet.

Umbra is an independent fork. It is not affiliated with or endorsed by ClassicOldSong (Apollo), SudoMaker or LizardByte (Sunshine). The links to Apollo's resources that Umbra keeps go to Apollo's own pages; report Umbra problems here, not to Apollo.

## Downloads and installation

Open [Umbra releases](https://github.com/PreceptorOfMagic/Umbra/releases), choose the compatible release and expand **Assets**. Download the **Windows x64 installer .exe**, not GitHub’s automatically generated source archives. If no installer is attached, there is no published installation package yet.

| Device | Package | Install |
|---|---|---|
| Windows x64 gaming PC | Umbra Windows installer, with bundled virtual-display and device components | [Windows steps](#windows-x64) |
| LG TV / Windows / Linux display device | Eclipse client, from its own repository | [Eclipse downloads](https://github.com/PreceptorOfMagic/Eclipse#downloads-and-installation) |

### Windows x64

1. Download the installer and matching checksum if provided. Compare the file’s SHA-256 hash with PowerShell’s `Get-FileHash`. The installer is unsigned for now; signing through SignPath Foundation is being set up (see the [Code signing policy](../CODE_SIGNING_POLICY.md)).
2. Stop any existing Sunshine/Apollo host service before starting Umbra. Back up its configuration before migration.
3. Run the installer with administrator approval and include its bundled virtual-display/controller/device components. Do not download the device bridge separately.
4. Allow access on your trusted private network. Use the installed shortcut or tray action to open the web interface and create the local administrator login.
5. Review game/desktop entries, keep client-driven display defaults and pair Eclipse using its PIN.

Windows 11 x64 on RTX 4070, RTX 3070 and Radeon RX Vega is the current physical test base. Linux/macOS host packages are not an Umbra release target here.

[Full Windows host installation](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/installation.md#umbra-windows).

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

Use [Umbra issues](https://github.com/PreceptorOfMagic/Umbra/issues/new/choose) for installation, capture, virtual display, host service, pairing or encoder problems. Include both app versions, Windows/GPU/driver, reproduction steps and the [reviewed diagnostic bundle](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/installation.md#diagnostics).

[Windows build guide](../docs/umbra-windows-build.md) · [Development and milestones](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/docs/development.md) · [Acknowledgements](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/ACKNOWLEDGEMENTS.md)

[Eclipse/Umbra licence & notices](../LICENSES.md) · [GPL-3.0](../LICENSE)
