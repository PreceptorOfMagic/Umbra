# Handoff: prebuilt Linux HDR extras and Umbra 0.5.2 for Linux

Written 2026-10-14 (about 20:45 UTC) by the Claude Code cloud session that did this work, for a Claude
agent on the maintainer's own machine. Read it from this branch:

```sh
git fetch origin claude/great-pascal-njfsyf
git show origin/claude/great-pascal-njfsyf:docs/handoff/umbra-linux-0.5.2-hdr-extras.md
```

This file is a working note, not product documentation. It is the newest commit on
`claude/great-pascal-njfsyf`, after the release's packaging commit, so it is **not** part of the
release. Leave it out of `release/umbra-linux-0.5` (fast-forward that branch to `7456c15`, not to the
branch head), or delete it once the work is finished.

## 1. Read this first: who is driving the release right now

The cloud session was publishing Umbra 0.5.2 for Linux when this was written. It has a scheduled
check-in at **21:07 UTC on 2026-10-14**. At that check-in, if the CI runs in section 5 passed, it
will move `release/umbra-linux-0.5` to `7456c15`, push the tag `umbra-linux-v0.5.2`, and let the release
workflow publish. The maintainer may tell it to stop and hand over to you instead.

**Before you change anything, find out which of these happened:**

```sh
git ls-remote --tags origin umbra-linux-v0.5.2                 # does the tag exist?
git ls-remote origin refs/heads/release/umbra-linux-0.5        # 7456c15… once moved
gh release view umbra-linux-v0.5.2 --repo PreceptorOfMagic/Umbra   # is the release published?
gh run list --repo PreceptorOfMagic/Umbra --workflow linux-release.yml --limit 5
```

- If the tag exists, **never move, delete or re-push it**. A failed release run publishes nothing (the
  publish job runs only after every build passes); fix the cause on the branch and ask the maintainer
  how to proceed (a new tag such as `umbra-linux-v0.5.3`, or re-running failed jobs if the failure
  was infrastructure).
- If the release exists, skip to section 6 (the remaining steps after publishing).
- Ask the maintainer before taking over steps the cloud session may still be doing. Two agents pushing
  the same tag or merging the same branch at once is the main risk here.

## 2. What the maintainer asked for (verbatim, in order)

1. "Currently for someone to install the HDR extra needed for their linux version they need to rebuild
   the package, this is fine for edge distros but we need to have a version already made for the major
   version of linux"
2. "What about cashyos?" (CachyOS)
3. "Do the smoke tests that you can for each build"
4. "I want you to publish everything and make the changes to the website needed. For each of them
   mention the amount of testing limited"
5. "I want you to write a handoff that a local Claude agent can access" (this file)

Request 4 means: publish all 14 extras with the Flatpak as a new release, and update the website. Every
download's testing limits must be stated wherever it is offered. Only the Ubuntu 26.04 extras have ever
run on hardware.

## 3. What exists now

### Repositories and branches

| Repo | Branch | State |
|---|---|---|
| `PreceptorOfMagic/Umbra` | `claude/great-pascal-njfsyf` | All work. `7456c15` = release packaging commit, `62ea2d0` = release code commit; this handoff on top |
| `PreceptorOfMagic/Umbra` | `release/umbra-linux-0.5` | Still at `f9cff84` (0.5.1 docs). Fast-forward to `7456c15` at release (`7456c15` descends from it) |
| `PreceptorOfMagic/Umbra` | `release/umbra-0.5` (default branch) | At `fee8b4e`. Needs the README/workflow commit in section 6 after release |
| `PreceptorOfMagic/Eclipse-Umbra-Project-Hub` | `claude/great-pascal-njfsyf` | Website and docs for 0.5.2 (`2208f00`, on top of `dfae32f`, `7fd5d88`). Merge to `main` after release; Pages deploys from `main` |

No pull requests exist. The cloud session only ever pushed to `claude/great-pascal-njfsyf`; moving the
release branches is part of "publish everything".

### The HDR extras build (`packaging/linux/hdr-extras/`)

- `targets`: 7 distributions × KDE/GNOME = 14 builds: `ubuntu26.04`, `ubuntu26.10`, `fedora44`,
  `fedora45`, `arch`, `cachyos`, `opensuse-tumbleweed`. Excluded, with reasons in the file: Debian 13
  and openSUSE Leap 16.0 (KWin/Mutter too old for the patches); Fedora 43 (PipeWire 1.4 has no PQ
  transfer; Mutter 49 has no HDR capture).
- `build.sh <kde|gnome> <target> <out>` runs inside a container of the target. It starts from the
  distribution's own current `kwin`/`mutter` source package and adds the patch for its series
  (`../kwin-virtual-hdr/kwin-virtual-hdr-screencast-{6.6,6.7}.patch`,
  `../mutter-virtual-hdr/mutter-virtual-hdr-screencast-{50,51}.patch`). Revisions are in `revisions`
  (kwin-6.6 4, kwin-6.7 1, mutter-50 2, mutter-51 1). The tarball is
  `Umbra-HDR-<KDE|GNOME>-<target>.tar.gz` (packages, `README.txt`, `SHA256SUMS`), plus `-source.tar.gz`.
- `lib/<family>.sh` holds the per-family hooks: `deb.sh`, `fedora.sh`/`opensuse.sh` (both use
  `rpm.sh`), `arch.sh`/`cachyos.sh` (both use `pacman.sh`).
- Version numbering: deb `+umbrahdrN`, rpm `<release>.umbrahdrN`. pacman (Arch, CachyOS) keeps the
  repository's version; `pacman -Qi` shows `Packager: Umbra HDR extra`.
- CachyOS: KWin is Arch's PKGBUILD at CachyOS's version (`6.7.5-3.1`); Mutter is CachyOS's own recipe
  (CachyOS-PKGBUILDS). Both are built with Arch's generic x86-64 flags.
- `README.txt` sections: WHAT IT IS, ONLY FOR, REQUIRES (KWin 6.7: Umbra 0.5.2 or newer), PACKAGES,
  INSTALL, VERIFY, UPDATES, ROLLBACK, SOURCE AND LICENCE, TESTED. TESTED has three parts: on hardware,
  automated, and not covered (`tested_note` in `build.sh`).
- `release-table.sh <dir>` makes the per-extra Markdown table for the release notes from the built
  tarballs. Its hardware wording matches `tested_note`.
- `run-local.sh <target> <kde|gnome> <out>` builds locally (podman or docker).
  `smoke-local.sh <target> <kde|gnome> <out>` smoke-tests locally. Load `vkms` first
  (`sudo modprobe vkms`) to get the KDE KMS checks; without a KMS device, KDE only gets a
  headless note.

### The smoke test (`smoke-test.sh`, `smoke/`)

In a fresh container of the distribution, it:
1. installs the distribution's compositor;
2. runs the extra's README.txt commands as printed (version check, `sha256sum -c`, install, VERIFY,
   rollback);
3. checks that every library and program in the packages resolves (`ldd`);
4. runs the compositor stock, then patched.

- **GNOME:** headless Mutter with software rendering, driven by `smoke/gnome-virtual-hdr-probe.py`
  (ScreenCast RecordVirtual). Patched, it must offer BT.2100 (`supported-color-modes` [0, 1]),
  switch to it, and offer 10-bit formats. Stock must offer neither.
- **KDE:** KWin's DRM backend on `vkms` with software GL. `smoke/kde-virtual-hdr-probe.c`, built
  against Umbra's own `src/platform/linux/kde-output.c`, requests a virtual output the way Umbra does,
  then must find it, resize it to 1920x1080 and switch it to HDR, and the stream must offer
  `ABGR_210LE`. Stock must find and resize it but not offer HDR.

CI requires the KMS path for KDE (`UMBRA_SMOKE_REQUIRE_KMS=1`).

### Umbra code changes (in the release)

`src/platform/linux/kde-output.c` and `third-party/kde-output-protocols/` (now plasma-wayland-protocols
1.21) are the only compiled-code changes since 0.5.1 (`e104c35`):
- KWin 6.7 announces outputs only through `kde_output_device_registry_v2`. Umbra now binds it (v21)
  when offered and falls back to the per-output globals (`6fb0c20`). Without this, 0.5.1 finds no
  KDE output on Plasma 6.7: no virtual display per stream, no co-op, no HDR.
- KWin 6.7 builds custom modes with libxcvt and reports their real refresh rate (1920x1080@60 becomes
  59.963 Hz). `nearest_mode()` takes the closest mode of the right size within 2% (`6215944`).
- Umbra binds `zkde_screencast` at v2, so the new v6 `serial` event is never sent to it. Output-device
  events added in v22/v23 are never sent at v21 either.

### Release machinery

- `.github/workflows/linux-hdr-extras.yml` builds and smoke-tests the 14 extras. It runs on pushes and
  PRs that touch the extras paths, weekly (once the file is on the default branch), by hand
  (`release_tag` attaches to an existing release), and through `workflow_call` from the release
  workflow.
- `.github/workflows/linux-release.yml` runs on a tag `umbra-linux-v*`:
  - `check`: the manifest's `BUILD_VERSION` matches the tag, the pinned `COMMIT` is in the history, and
    `packaging/linux/release-notes/<tag>.md` exists.
  - `flatpak`: flatpak-builder on ubuntu-24.04 builds the bundle. It installs the published 0.5.1
    bundle, then this one with `--reinstall`, and checks that settings survive, that `--version`
    reports 0.5.2 and that `ldd` finds every library. It also writes the sources archive (CUDA parts
    listed in `CUDA-SOURCES.txt`, not included) and the project source.
  - `extras`: calls the extras workflow.
  - `publish`: only after everything passes. It writes `SHA256SUMS`, fills the
    `<!-- HDR extras table -->` marker in the notes with `release-table.sh` output, and runs
    `gh release create --latest=false` (the Windows 0.5.0 release stays "latest").
  - Pushes that touch the workflow or the Flatpak packaging run `check` + `flatpak` only (a dry run).
- Flatpak manifest: `packaging/linux/flatpak-umbra/io.github.PreceptorOfMagic.Umbra.yml` pins
  `BUILD_VERSION v0.5.2` and code commit `62ea2d0`. Build docs are in `DEVELOPMENT.md` in the same
  directory.
- Release notes: `packaging/linux/release-notes/umbra-linux-v0.5.2.md`.

## 4. Testing done, and its limits (state these wherever a download is offered)

| Download | On hardware | Automated (CI) |
|---|---|---|
| Flatpak 0.5.2 | **No.** 0.5.1 was tested on Ubuntu 26.04 (KDE Plasma 6.6 and GNOME 50.1), RTX 2060 SUPER (driver 595) and RX Vega | Build, install over 0.5.1, version, libraries. Plasma 6.7 output control on real KWin 6.7.5 + vkms on six distributions |
| KDE Ubuntu 26.04 (KWin 6.6.6, +umbrahdr4) | Yes, with Umbra **0.5.1** (not 0.5.2) | Passed |
| GNOME Ubuntu 26.04 (Mutter 50.1, +umbrahdr2) | Revision **1** with 0.5.1; revision 2 (two memory-leak fixes) not on hardware | Passed |
| KDE: Ubuntu 26.10, Fedora 44/45, openSUSE TW, Arch, CachyOS (KWin 6.7.5) | **None.** The KWin 6.7 patch has never run on hardware | Passed |
| GNOME: Ubuntu 26.10 (51.0), Fedora 45 (51.0), Arch (51.1), CachyOS (51.0, its own) | **None.** The Mutter 51 patch has never run on hardware | Passed |
| GNOME: Fedora 44 (50.5), openSUSE TW (50.5) | **None** (the same Mutter 50 patch ran on Ubuntu, revision 1) | Passed |

The automated test uses no real GPU or display, does not stream, and shows no HDR on a screen.

Last all-green extras run at the near-final code:
<https://github.com/PreceptorOfMagic/Umbra/actions/runs/38612599567> (commit `b05af93`, 14/14). The
commits since then change only README.txt wording, workflows and docs.

## 5. CI in flight when this was written

| Run | What | Expected |
|---|---|---|
| [38631968229](https://github.com/PreceptorOfMagic/Umbra/actions/runs/38631968229) | Flatpak dry run 1 (pin `b05af93`, old bundle check) | First proof that the Flatpak builds on GitHub runners (started 20:27; expect 1–2.5 h) |
| [38632835077](https://github.com/PreceptorOfMagic/Umbra/actions/runs/38632835077) | Flatpak dry run 2 on `7456c15` (final pin, upgrade-over-0.5.1 check) | Must pass before tagging |
| [38632834823](https://github.com/PreceptorOfMagic/Umbra/actions/runs/38632834823) | Extras run 15 on `7456c15` (new README.txt text) | 14/14 |

Neither Flatpak build had finished at handoff. Untested risks: the runner's flatpak-builder 1.4.x;
`flatpak run` without a desktop session in the check step; `curl` of the 0.5.1 bundle; the sources tar
step. If a check step fails for a CI-environment reason and not a bundle problem, fix the workflow on
the branch: a push re-runs the dry run.

## 6. Remaining steps

### A. Publish (only once section 5's dry run 2 and extras run 15 are green, and nobody else did it)

```sh
git fetch origin
git push origin 7456c15b03e4246c21188f71b090172070ba6b41:refs/heads/release/umbra-linux-0.5   # fast-forward
git tag -a umbra-linux-v0.5.2 7456c15b03e4246c21188f71b090172070ba6b41 -m "Umbra 0.5.2 for Linux"
git push origin umbra-linux-v0.5.2
gh run watch --repo PreceptorOfMagic/Umbra $(gh run list --repo PreceptorOfMagic/Umbra --workflow linux-release.yml --limit 1 --json databaseId --jq '.[0].databaseId')
```

Expect about 2–3 h. Arch KDE once took 56 min to build. Then check:

```sh
gh release view umbra-linux-v0.5.2 --repo PreceptorOfMagic/Umbra --json assets --jq '.assets[].name'
gh release download umbra-linux-v0.5.2 --repo PreceptorOfMagic/Umbra --pattern 'SHA256SUMS' --pattern '*.sha256' -D /tmp/rel
```

Expect 14 extras plus 14 source tarballs, each with `.sha256`; the Flatpak, its sources archive and
the project source, each with `.sha256`; and `SHA256SUMS`. Read the published notes: the extras table
must have 14 rows.

### B. Default branch (`release/umbra-0.5`), after the release exists

The cloud session prepared commit `e180dce` locally (not pushed). Recreate it:

```sh
git switch -c default-0.5.2 origin/release/umbra-0.5
git checkout origin/claude/great-pascal-njfsyf -- .github/README.md .github/workflows/linux-hdr-extras.yml
git commit -m "README: Umbra 0.5.2 for Linux, and how far each HDR extra is tested [skip ci]"   # plus the trailers in section 8
git push origin default-0.5.2:release/umbra-0.5
```

The README's patch links name the tag `umbra-linux-v0.5.2`, so they work from the default branch.
With the workflow file there, the weekly run (`17 4 * * 1`) and manual dispatch build
`release/umbra-linux-0.5`.

### C. Website (Hub), after the release exists

Merge `claude/great-pascal-njfsyf` into `main` in `Eclipse-Umbra-Project-Hub` and push; `pages.yml`
validates and deploys. Validate locally first:

```sh
node scripts/launch/check_hub_boundary.mjs && node scripts/launch/check_public_site.mjs
node --test scripts/launch/check_publication_limits.test.mjs scripts/launch/check_downloads.test.mjs scripts/launch/check_disclosures.test.mjs
```

The site's download buttons (`site/assets/downloads.mjs`) find the newest `umbra-linux-v*` release by
themselves. The hard-coded fallback links and text already say 0.5.2.

The website lists compositor series (KWin 6.6/6.7, Mutter 50/51), not exact versions. If the
published release's table shows a series change, update `docs/installation.md` and `site/setup.html`.

### D. Tell the maintainer

Report what was published, with links, and repeat the testing limits of section 4.

## 7. What a local agent can add: hardware testing

The cloud session had no hardware. These are the biggest gaps, in order of value. Record results the
way the existing records do (the patch READMEs' measurement sections, `docs/umbra-linux-coop.md`). Then
update the TESTED text (`tested_note` in `build.sh`, `hardware()` in `release-table.sh`), the README,
the release notes (`gh release edit`) and the website tables.

1. **Umbra 0.5.2 on the Ubuntu 26.04 machine** (KDE Plasma 6.6 and GNOME), as 0.5.1 was tested: install
   over 0.5.1 with `--reinstall`, then a stream with its own virtual display, co-op, and HDR with each
   extra. This confirms the `kde-output.c` change did not regress KWin 6.6.
2. **The Mutter 50 revision 2 extra on Ubuntu 26.04** (`+umbrahdr2`): an HDR stream as in the Mutter
   README. It is the only HDR extra shipped untested on hardware for a hardware-tested setup.
3. **KDE Plasma 6.7** on any of the KWin 6.7 distributions (a live USB of Fedora 44/45 KDE or Ubuntu
   26.10 works): Umbra 0.5.2 streams with its own virtual display at the requested mode (the
   59.963 Hz match), co-op, then the KDE extra for that distribution: HDR and `ABGR_210LE`.
4. **Mutter 51** (Fedora 45 Workstation, Ubuntu 26.10, Arch): GNOME HDR with that extra.

## 8. Conventions the work has followed

- Commit messages end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and a `Claude-Session:` line naming the
  session that made the commit. Never put a model identifier anywhere else in commits, PRs or files.
- Docs-only commits on `release/umbra-0.5` carry `[skip ci]`.
- Writing style in docs: plain, specific, measured. State limits; never claim untested behaviour.
- Do not create pull requests unless the maintainer asks.

## 9. Known quirks (fixed, and why they were needed)

- Docker: KWin's binaries carry `cap_sys_nice`, so its container needs `--cap-add SYS_NICE`. KMS
  needs `--privileged -v /dev/dri:/dev/dri`. vkms on GitHub runners comes from
  `linux-modules-extra-$(uname -r)`. KWin's headless backend has no OpenGL, which gives
  "Unsupported compositing type".
- CachyOS's pacman sandboxes install scripts and hooks with the network isolated, which a container
  cannot do; the smoke test sets `DisableSandboxNetwork`. Its cdn77 mirror sometimes 404s, and pacman
  then falls back to another.
- openSUSE: the image's source repository is disabled (it gets enabled); the image installs no
  recommended packages (so `glibc-devel` and `pkgconf-pkg-config` are listed explicitly); rpm numbers
  a bare `Source:` after the one before it (so `Source9990` goes at the end of the preamble); and
  stripping happens only in the debuginfo step (so openSUSE builds keep it, and its debuginfo
  packages are left out).
- Fedora: the Cisco OpenH264 repository often fails, so it is disabled in the containers. Fedora's
  `mutter-devkit` is excluded from the packages.
- Arch and CachyOS GNOME source tarballs are about 114 MB, because their Mutter recipes clone the
  whole Mutter git history. Left as is.
- GitHub job logs are not downloadable while a job runs.
