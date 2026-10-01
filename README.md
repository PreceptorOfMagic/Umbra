# Umbra

**The Windows host for Eclipse/Umbra game streaming.**

[Project Hub](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub) · [Installation Guide](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/site/setup.html) · [Eclipse](https://github.com/PreceptorOfMagic/Eclipse) · [Umbra](https://github.com/PreceptorOfMagic/Umbra) · [Development](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/site/development.html) · [Acknowledgements](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/site/credits.html)

Umbra is a self-hosted, Moonlight-compatible Windows server derived from Apollo and Sunshine. Install it on one gaming PC for conventional game or desktop streaming to Eclipse. Install the same compatible release on two PCs for Eclipse's side-by-side or stacked couch co-op mode.

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

## Why Umbra and Eclipse are developed together

Two independently encoded streams cannot always be joined safely when their GPUs produce different bitstream structures. This is especially important for cross-vendor pairs. Eclipse coordinates the two sessions and requests a compatible mode; Umbra supplies the host capability reporting, custom co-op encoder behaviour and session contract that request needs.

The work therefore spans a client and a host repository. Anyone may adapt it for Apollo, Aurora, Sunshine, Moonlight or another compatible fork under the relevant licences.

## Download or build

| Need | Official route |
|---|---|
| Umbra installer and checksums | [Umbra downloads](https://github.com/PreceptorOfMagic/Umbra/releases) |
| Source archive | [Default-branch ZIP](https://github.com/PreceptorOfMagic/Umbra/archive/refs/heads/beta/vega-coop.zip) |
| Git clone | `git clone --recurse-submodules --branch beta/vega-coop https://github.com/PreceptorOfMagic/Umbra.git` |
| Windows source build | [Build Umbra on Windows](docs/umbra-windows-build.md) |
| Eclipse client | [Eclipse repository](https://github.com/PreceptorOfMagic/Eclipse) |
| Full installation path | [Eclipse/Umbra Installation Guide](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/site/setup.html) |

Only install packages and supporting components delivered through the Umbra release. Do not assemble or download individual integration pieces from unrelated links.

## Quick start

1. Install Umbra on the Windows gaming PC and allow the requested private-network firewall access.
2. Open its local web interface, complete initial configuration and add the games or desktop entries you want to stream.
3. Open Eclipse on the television or desktop client, pair using the displayed PIN and launch an ordinary one-host session.
4. For co-op, repeat the same process on a second PC, verify both normal streams, then choose **Co-op** in Eclipse.

The co-op entry automatically creates the required host session and negotiates working encoder defaults. Users do not need to create a `Co-op Pane` application or copy encoder settings by hand. Eclipse reveals the separate co-op options after co-op has been used.

## Host access and security

The first paired client receives broad permissions under the inherited Apollo permission model. Review later clients and grant only the permissions they need. Umbra's configuration interface is local to the host at `https://localhost:47990` by default; its locally generated certificate can produce a first-use browser warning. Do not expose the management interface directly to the public internet.

## Diagnostics and issue reports

Use [Umbra issues](https://github.com/PreceptorOfMagic/Umbra/issues/new/choose) for host capture, virtual display, service, pairing, permissions, encoding and installer problems. Include the Umbra and Eclipse versions, Windows version, GPU and driver, whether a normal stream works, exact reproduction steps and Eclipse's reviewed diagnostic bundle.

Use [Eclipse issues](https://github.com/PreceptorOfMagic/Eclipse/issues/new/choose) for client decoding, presentation, layout, composition, client-side input routing and Share/diagnostics behaviour.

## Lineage, licence and independence

Sunshine was originally created by `@loki-47-6F-64` and is maintained by LizardByte and contributors. Apollo was created by `@ClassicOldSong` and supplies Umbra's direct host and virtual-display lineage. The matching client descends from Moonlight, Moonlight TV by Mariotaku and Aurora by GuiDev1994. Read the [full acknowledgements](https://github.com/PreceptorOfMagic/Eclipse-Umbra-Project-Hub/blob/main/site/credits.html) for the wider dependency and driver contributors.

Umbra is distributed under the GNU General Public License v3.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE). Components and dependencies retain their own copyright and licence terms.

Eclipse/Umbra is independent and is not affiliated with or endorsed by Moonlight, Sunshine, LG Electronics, NVIDIA, AMD, Intel, Amazon, DXC or the Eclipse Foundation. Third-party names identify lineage or compatibility.
