# Umbra

<p align="center">
  <strong>The Windows host application for the Eclipse/Umbra project.</strong><br>
  Two Umbra gaming PCs can feed one Eclipse client for a shared couch co-op display.
</p>

<p align="center">
  <a href="https://github.com/PreceptorOfMagic/PreceptorOfMagic.github.io">Project hub</a> ·
  <a href="https://github.com/PreceptorOfMagic/PreceptorOfMagic.github.io/blob/main/site/setup.html">Setup guide</a> ·
  <a href="https://github.com/PreceptorOfMagic/eclipse">Eclipse client</a> ·
  <a href="https://github.com/PreceptorOfMagic/umbra/releases">Umbra downloads</a> ·
  <a href="https://github.com/PreceptorOfMagic/PreceptorOfMagic.github.io/blob/main/site/credits.html">Acknowledgements</a> ·
  <a href="https://preceptorofmagic.github.io/">Live site after publication</a>
</p>

> [!IMPORTANT]
> Umbra is the **host**. Install it on each Windows gaming PC. Eclipse is the
> **client** at the television or shared display. For co-op, each PC runs its own
> game session; Eclipse joins the two live streams and sends each assigned input
> back to the correct Umbra host.

> [!NOTE]
> This repository is currently a source-first engineering preview. A reviewed
> Umbra installer has not been published yet. Use this repository's source and
> build guide during private testing; when a release is approved, its installer,
> checksums and notes will appear only on the
> [Umbra Releases page](https://github.com/PreceptorOfMagic/umbra/releases).

## What Umbra provides

Umbra is derived from [Apollo](https://github.com/ClassicOldSong/Apollo), which
is derived from [Sunshine](https://github.com/LizardByte/Sunshine). It keeps the
normal Moonlight-compatible host path and Apollo's virtual-display and
permission features, then adds the host-side work used by Eclipse/Umbra co-op.

- Low-latency hardware encoding on supported NVIDIA, AMD and Intel GPUs.
- Pairing, application management and per-client permissions through a local
  web interface.
- A virtual display that can follow the client-requested resolution and frame
  rate on Windows.
- A dedicated `Co-op Pane` application path for two-host Eclipse composition.
- Per-client configuration switching so the same host can serve the ordinary
  and co-op client profiles.
- Host-side companion handling for the project's input, audio and display
  session lifecycle.
- Recovery and aspect-correct fitting for capture changes encountered during
  the current co-op path.

Normal one-host streaming remains available. Co-op is an additional mode, not a
replacement for the standard Eclipse-to-Umbra streaming path.

## Download or build

| Need | Official route |
|---|---|
| Reviewed Umbra installer | [Umbra Releases](https://github.com/PreceptorOfMagic/umbra/releases) — no approved installer is published yet |
| Current source archive | [Download the default branch as ZIP](https://github.com/PreceptorOfMagic/umbra/archive/refs/heads/beta/vega-coop.zip) |
| Git clone | `git clone --recurse-submodules --branch beta/vega-coop https://github.com/PreceptorOfMagic/umbra.git` |
| Windows source build | [Build Umbra on Windows](docs/umbra-windows-build.md) |
| Eclipse client | [Eclipse repository](https://github.com/PreceptorOfMagic/eclipse) |
| Complete two-host setup | [Eclipse/Umbra setup guide source](https://github.com/PreceptorOfMagic/PreceptorOfMagic.github.io/blob/main/site/setup.html) |

Do not download an installer offered elsewhere under the Umbra name. Until a
reviewed release exists, source builds are the only project-controlled path.

## Quick start for co-op

1. Build and install the same reviewed Umbra revision on two Windows gaming PCs.
2. Open each host's web interface and complete its initial configuration.
3. Add one application named exactly **Co-op Pane** on each host.
4. Enable the host virtual display and leave its resolution and refresh rate set
   to the values requested by the client.
5. Install or build Eclipse at the shared display, pair it with each host and
   verify an ordinary one-host stream from both PCs first.
6. In Eclipse, choose **Co-op**, select two different hosts, choose side-by-side
   or stacked layout, assign inputs and start the combined session.

The full [setup guide](https://github.com/PreceptorOfMagic/PreceptorOfMagic.github.io/blob/main/site/setup.html) includes
the expected application settings, validation order and troubleshooting route.

## Host access and permissions

The first paired client receives full permissions under Apollo's inherited
permission model. Later clients may need **View Streams**, **List Apps**,
**Launch Apps**, **Mouse Input**, **Keyboard Input** or controller permissions
enabled explicitly in the Umbra web interface.

Umbra's configuration interface is local to the host at
`https://localhost:47990` by default. Its locally generated certificate may
cause the browser to show a certificate warning on first access. Do not expose
the management interface directly to the public internet.

## Current support shape

The project co-op path is being prepared around Windows 11 hosts with supported
hardware encoders. Apollo and Sunshine include broader platform work, but the
Eclipse/Umbra release claims deliberately stay narrower until those combinations
have been verified against this fork.

For ordinary Moonlight-compatible host configuration concepts, the upstream
[Sunshine documentation](https://docs.lizardbyte.dev/projects/sunshine) remains
useful. Umbra-specific co-op instructions and release status come from the
[Eclipse/Umbra hub](https://github.com/PreceptorOfMagic/PreceptorOfMagic.github.io).

## Project map and support

- Use [Umbra issues](https://github.com/PreceptorOfMagic/umbra/issues) for host
  capture, encoding, service, pairing, virtual-display and installer problems.
- Use [Eclipse issues](https://github.com/PreceptorOfMagic/eclipse/issues) for
  client decoding, layout, composition and client-side input routing.
- Optional project support is handled through the hub's
  [support page](https://github.com/PreceptorOfMagic/PreceptorOfMagic.github.io/blob/main/site/support.html). It buys no
  access, features, priority, rewards or influence.

## Lineage and acknowledgements

Umbra would not exist without the projects it builds on:

- **Sunshine**, originally created by `@loki-47-6F-64` and maintained by
  LizardByte and its contributors.
- **Apollo**, created by `@ClassicOldSong`, which supplies Umbra's direct host
  lineage and virtual-display foundation.
- **Moonlight**, created at MHacks 2013 by Cameron Gutman, Diego Waxemberg,
  Aidan Campbell, Aaron Neyer, Michelle Bergeron and Andrew Hennessy.
- **Moonlight TV** by Mariotaku and **Aurora** by GuiDev1994, the direct client
  lineage leading to Eclipse.

The [full acknowledgements](https://github.com/PreceptorOfMagic/PreceptorOfMagic.github.io/blob/main/site/credits.html)
name the wider dependency and driver contributors. That public credit page does
not replace the licence, notice and source material required in a release.

Eclipse/Umbra is independent and is not affiliated with or endorsed by
Moonlight, Sunshine, LG Electronics, NVIDIA, AMD, Amazon, DXC or the Eclipse
Foundation. Third-party names are used to describe lineage or compatibility.

## Licence

Umbra is distributed under the GNU General Public License v3.0. See
[LICENSE](LICENSE) and [NOTICE](NOTICE). Components and dependencies retain their
own copyright and licence terms.
