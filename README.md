# Umbra

<p align="center">
  <strong><a href="https://preceptorofmagic.github.io/eclipse/">Eclipse/Umbra project hub</a></strong> ·
  <a href="https://preceptorofmagic.github.io/eclipse/setup.html">Quick start</a> ·
  <a href="https://github.com/PreceptorOfMagic/eclipse">Eclipse client source</a> ·
  <a href="https://preceptorofmagic.github.io/eclipse/#status">Current status</a> ·
  <a href="https://github.com/PreceptorOfMagic/eclipse/blob/HEAD/ACKNOWLEDGEMENTS.md">Acknowledgements</a>
</p>

> [!WARNING]
> **Engineering preview.** **Eclipse/Umbra** is one project: **Eclipse** is its
> client application and **Umbra** is its host application. There is no public
> Umbra installer yet, and the current source and setup material are not a
> consumer release.

Umbra is the Windows host for a two-PC couch co-op system. Each PC runs and
streams its own game through an independent session; the companion
[Eclipse client](https://github.com/PreceptorOfMagic/eclipse) receives both live
streams, routes each assigned controller to its chosen host, and composes the
two panes on one display.

```text
Windows host A ── live stream A ──┐
                                  ├─ Eclipse client ── one split-screen display
Windows host B ── live stream B ──┘
                    ▲       ▲
             controller A   controller B
```

Umbra descends from [Apollo](https://github.com/ClassicOldSong/Apollo), which
descends from [Sunshine](https://github.com/LizardByte/Sunshine). It keeps
Apollo's Moonlight-compatible host, permission system, virtual-display support,
and session lifecycle while adding the host-side work needed by the co-op
client.

## What this fork changes

- Host-side codec, profile, and capability controls used to coordinate a co-op
  pairing.
- Per-client session configuration for the live and development clients.
- Bounded Windows display/capture recovery for disruptions seen during game
  launch and mode changes.
- An experimental GPU HEVC path used in development. It is not a supported
  consumer path while its rapid-motion blockiness remains unresolved.

Pane geometry and final composition belong to the client. Umbra produces one
normal live stream per host; it does not stitch host frames together.

## Current status

| Area | Status |
| --- | --- |
| Public packages | None; do not install an Apollo release expecting Umbra changes |
| Supported Umbra host | Windows engineering builds |
| Mixed NVIDIA/AMD H.264 | Live synthetic stress passed; real-game user acceptance remains open |
| Experimental AMD HEVC | Rapid-motion blockiness remains open |
| Application identity | Visible host surfaces use Umbra; inherited Sunshine names and migration-sensitive internal identifiers remain where required |

No purchase decision should be based on this preview. Compatibility depends on
the exact host pair, GPUs, games, controllers, network, and client platform.

## Setup and documentation

The co-op setup guide will be published with the public source release:

- [Consumer quick start](https://preceptorofmagic.github.io/eclipse/setup.html)
- [Windows co-op setup](https://github.com/PreceptorOfMagic/eclipse/blob/HEAD/docs/windows-coop-setup.md)
- [Eclipse client source](https://github.com/PreceptorOfMagic/eclipse)

For behavior inherited unchanged from Apollo, use the upstream
[Apollo wiki](https://github.com/ClassicOldSong/Apollo/wiki) and
[Sunshine documentation](https://docs.lizardbyte.dev/projects/sunshine/).
Those pages describe their own releases; they are not Umbra download or support
channels.

## Project family

The two development lineages remain explicit:

- Moonlight → Moonlight TV → Aurora → Eclipse client
- Sunshine → Apollo → Umbra host

Each repository owns its source and technical instructions. The client
repository is also the staging point for the unified project site, release
status, recording plan, and full acknowledgement inventory.

## Credits

This work exists because of the people who built and maintained the projects it
inherits from, especially:

- the [Moonlight Game Streaming Project](https://github.com/moonlight-stream),
  created at MHacks 2013 by Cameron Gutman, Diego Waxemberg, Aidan Campbell,
  Aaron Neyer, Michelle Bergeron and Andrew Hennessy, and its contributors;
- the original Sunshine work by
  [loki-47-6F-64](https://github.com/loki-47-6F-64) and the current
  [LizardByte Sunshine](https://github.com/LizardByte/Sunshine) maintainers and
  contributors;
- [ClassicOldSong](https://github.com/ClassicOldSong) and Apollo contributors;
- [mariotaku](https://github.com/mariotaku) and Moonlight TV contributors;
- [GuiDev1994](https://github.com/GuiDev1994) and Aurora contributors; and
- [SudoVDA](https://github.com/SudoMaker/SudoVDA), which carries inherited
  Microsoft/VirtualDrivers lineage, and the many libraries, drivers, codecs,
  and tools used by the host and client.

The public release must ship the exact licence notices, component inventory,
and corresponding-source information required by the artifacts it distributes.
The detailed inventory is maintained in the client repository's
[acknowledgements](https://github.com/PreceptorOfMagic/eclipse/blob/main/ACKNOWLEDGEMENTS.md).

## Funding

Funding is intentionally disabled while the project resolves its legal recipient,
tax treatment, platform terms, and release-compliance gates.
Donations will never purchase features, priority, warranties, or support.

## Licence

Umbra is a GPL-3.0-licensed derivative of Apollo and Sunshine. See
[LICENSE](LICENSE) for the repository licence text. A public binary release is
blocked until its notices, corresponding source, and build-specific obligations
have been verified.
