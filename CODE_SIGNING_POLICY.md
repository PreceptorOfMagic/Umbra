# Code signing policy

**Status:** an application to SignPath Foundation is pending. Every release up to and including Umbra 0.5.0 is **unsigned**. Until signing is approved, check each download against its published SHA-256.

Once approved: free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

## What is signed

- **Signed:** the Windows installer (`Umbra-windows-x64.exe`), and the binaries this project builds:
  - `sunshine.exe`, `aurora-link.exe`, and `tools\audio-info.exe`, `tools\dxgi-info.exe`, `tools\sunshinesvc.exe`;
  - the GPU HEVC co-op encoder, `gpu-hevc\aurora_gpu_hevc8.dll` and `gpu-hevc\aurora_gpu_hevc10.dll`, built from the Eclipse repository's `experimental/gpu-hevc`.
- **How they're built:** by the [`release-windows-signed.yml`](.github/workflows/release-windows-signed.yml) GitHub Actions workflow, from the release tag, with every downloaded input pinned by commit or SHA-256. Nothing built on a personal machine is sent for signing.
- **Not signed by this project:** third-party components keep their own vendors' signatures or ship as their projects built them:
  - the SudoVDA virtual display driver (signed by its author);
  - the ViGEmBus installer (signed by Nefarius);
  - `nefconc.exe`, built from [nefcon](https://github.com/nefarius/nefcon)'s MIT source;
  - `zlib1.dll`.
- **Linux:** the `.deb` packages don't use Authenticode. Verify them with the published SHA-256 files.

## What the installer changes on the PC

- **Installs:** the Umbra host service, firewall rules for its streaming ports, the ViGEmBus virtual gamepad driver and the SudoVDA virtual display driver.
- **Trusts a certificate:** to install SudoVDA, the installer adds SudoVDA's own code-signing certificate (`CN=sudovda@su.mk`) to the PC's *Trusted Root* and *Trusted Publisher* certificate stores. This is how Apollo installs the same driver. Uninstalling Umbra currently leaves that certificate in place.

## Team roles

| Role | Members |
|---|---|
| Committers and reviewers | [PreceptorOfMagic](https://github.com/PreceptorOfMagic) |
| Approvers | [PreceptorOfMagic](https://github.com/PreceptorOfMagic) |

- **Committers** can change the source directly. Contributions from anyone else arrive as pull requests, and a reviewer must approve them before they are merged.
- **Approvers** approve each signing request in SignPath, one release at a time.
- **AI assistance:** parts of this project are written and debugged with AI coding assistants (Claude, Codex). They work under the maintainer's account and direction. The maintainer reviews and tests what they produce before it is merged or released. They have no separate access and cannot approve signing.
- **Sign-in security:** every team member uses multi-factor authentication for GitHub and SignPath.

## Privacy

Umbra collects no personal data and has no telemetry or analytics. It makes these network connections, and no
others:

- streams only to devices you pair with it, and announces itself on your local network (mDNS) so those devices can
  find it;
- maps its ports on your router through UPnP only if you turn that setting on (it is off by default);
- its settings page, while open in your browser, asks GitHub for the latest Apollo release and downloads the Project
  Hub's list of Umbra releases (`https://preceptorofmagic.github.io/Eclipse-Umbra-Project-Hub/updates.json`), to
  tell you whether an update is out.

These requests send nothing about you, your PC or your devices beyond what any web request carries: your IP address
and the browser's own headers. They are served by GitHub, whose [privacy statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement) applies to them.

## Reporting

Report a signed binary that you think is malicious, or a breach of this policy, through this repository's issues. If it concerns the certificate itself, also report it to [SignPath Foundation](https://signpath.org/).
