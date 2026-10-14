# Handoff: prebuilt Linux HDR extras and Umbra 0.5.2 for Linux

Written 2026-10-14 by the Claude Code cloud session that did this work, for a Claude agent on the
maintainer's own machine. Read it from this branch:

```sh
git fetch origin claude/great-pascal-njfsyf
git show origin/claude/great-pascal-njfsyf:docs/handoff/umbra-linux-0.5.2-hdr-extras.md
```

This file is a working note, not product documentation. It is the newest commit on
`claude/great-pascal-njfsyf`, after the release's packaging commit `7456c15`. Delete it
(`git rm`) in the commit you tag, so it is not part of the release (section 6).

## 1. Your job, and who is driving

**You now own the release. The cloud session has stopped:** it will not tag, publish or push anything
more. Nothing is published yet: no tag `umbra-linux-v0.5.2`, no release, release branches unchanged.

The maintainer's plan, in order:

1. **Hardware tests first** (section 7), with the release candidate builds CI already made (section 5).
2. **Record the results** in the TESTED texts, release notes, READMEs and website (section 7, "Recording
   results"), so each download states how far it has been tested.
3. **Publish** (section 6), once the maintainer has seen the results and says go. If a test fails, fix
   it first (section 7, "If a test fails").

Check the state before acting, in case anything changed since this was written:

```sh
git ls-remote --tags origin umbra-linux-v0.5.2                 # expect nothing until you tag
git ls-remote origin refs/heads/release/umbra-linux-0.5        # expect f9cff84… until you publish
gh release view umbra-linux-v0.5.2 --repo PreceptorOfMagic/Umbra   # expect "release not found"
```

Once a tag `umbra-linux-v0.5.2` exists, never move, delete or re-push it. A failed release run
publishes nothing (the publish job runs only after every build passes). Fix the cause on the branch, then
re-run the failed jobs if the failure was infrastructure, or ask the maintainer about a new tag.

## 2. What the maintainer asked for (verbatim, in order)

1. "Currently for someone to install the HDR extra needed for their linux version they need to rebuild
   the package, this is fine for edge distros but we need to have a version already made for the major
   version of linux"
2. "What about cashyos?" (CachyOS)
3. "Do the smoke tests that you can for each build"
4. "I want you to publish everything and make the changes to the website needed. For each of them
   mention the amount of testing limited"
5. "I want you to write a handoff that a local Claude agent can access" (this file)
6. "I will be getting the agent to run hardware tests first so once you are finished do any updates to
   the handover and give me the prompt to give to the local agent"

Requests 4 and 6 together mean: publish all 14 extras with the Flatpak as Umbra 0.5.2 for Linux and
update the website, but run the hardware tests first. Every download's testing limits must be stated
wherever it is offered, updated with whatever the hardware tests show.

## 3. What exists now

### Repositories and branches

| Repo | Branch | State |
|---|---|---|
| `PreceptorOfMagic/Umbra` | `claude/great-pascal-njfsyf` | All work. `62ea2d0` = release code commit (the Flatpak builds it), `7456c15` = packaging commit pinning it; this handoff on top. Add test results here, then tag (section 6) |
| `PreceptorOfMagic/Umbra` | `release/umbra-linux-0.5` | Still at `f9cff84` (0.5.1 docs). Fast-forward to the commit you tag (the claude branch descends from it) |
| `PreceptorOfMagic/Umbra` | `release/umbra-0.5` (default branch) | At `fee8b4e`. Needs the README/workflow commit in section 6 after release |
| `PreceptorOfMagic/Eclipse-Umbra-Project-Hub` | `claude/great-pascal-njfsyf` | Website and docs for 0.5.2 (`2208f00`, on top of `dfae32f`, `7fd5d88`). Merge to `main` after release; Pages deploys from `main` |

No pull requests exist. The cloud session only ever pushed to `claude/great-pascal-njfsyf`; moving the
release branches is part of "publish everything", after the hardware tests.

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

## 5. The release candidates CI built

<!-- CANDIDATES -->

## 6. Publishing, after the hardware tests and the maintainer's go-ahead

### A. Tag and publish

Commit the recorded results on `claude/great-pascal-njfsyf` (section 7) and delete this handoff in the
same or a following commit, then:

```sh
git fetch origin
git switch --detach origin/claude/great-pascal-njfsyf      # the commit you will release
test ! -e docs/handoff/umbra-linux-0.5.2-hdr-extras.md     # the handoff is gone
git merge-base --is-ancestor origin/release/umbra-linux-0.5 HEAD && echo fast-forward ok
grep -n 'BUILD_VERSION\|COMMIT:' packaging/linux/flatpak-umbra/io.github.PreceptorOfMagic.Umbra.yml
git push origin HEAD:refs/heads/release/umbra-linux-0.5
git tag -a umbra-linux-v0.5.2 -m "Umbra 0.5.2 for Linux" HEAD
git push origin umbra-linux-v0.5.2
gh run watch --repo PreceptorOfMagic/Umbra $(gh run list --repo PreceptorOfMagic/Umbra --workflow linux-release.yml --limit 1 --json databaseId --jq '.[0].databaseId')
```

- The manifest pins the code commit `62ea2d0`: the release builds the same Umbra code as the candidate
  Flatpak you tested. Leave the pin alone unless you changed Umbra's code (section 7, "If a test
  fails"). Commits after it that only change docs, README.txt text or the notes need no new pin; the
  release workflow takes those from the tag.
- The extras are rebuilt at tag time from each distribution's current packages. Compare the release's
  table (package version column) with the candidates you tested. If a distribution updated KWin or
  Mutter in between, the published build is not the one you tested: say so in its row, or retest.
- Expect 2–3 h; Arch KDE once took 56 min to build. Then check:

```sh
gh release view umbra-linux-v0.5.2 --repo PreceptorOfMagic/Umbra --json assets --jq '.assets[].name'
mkdir -p /tmp/rel && cd /tmp/rel && gh release download umbra-linux-v0.5.2 --repo PreceptorOfMagic/Umbra && sha256sum -c SHA256SUMS
```

Expect 14 extras and 14 source tarballs, each with `.sha256`; the Flatpak, its sources archive and the
project source, each with `.sha256`; and `SHA256SUMS`. Read the published notes: the extras table must
have 14 rows, with your hardware results.

### B. Default branch (`release/umbra-0.5`), after the release exists

```sh
git switch -c default-0.5.2 origin/release/umbra-0.5
git checkout origin/release/umbra-linux-0.5 -- .github/README.md .github/workflows/linux-hdr-extras.yml
git commit -m "README: Umbra 0.5.2 for Linux, and how far each HDR extra is tested [skip ci]"   # plus the trailers in section 8
git push origin default-0.5.2:release/umbra-0.5
```

The README's patch links name the tag `umbra-linux-v0.5.2`, so they work from the default branch.
With the workflow file there, the weekly run (`17 4 * * 1`) and manual dispatch build
`release/umbra-linux-0.5`.

### C. Website (Hub), after the release exists

`Eclipse-Umbra-Project-Hub` branch `claude/great-pascal-njfsyf` has the 0.5.2 pages: `docs/installation.md`
and `site/setup.html` (a row per extra with its testing), `docs/umbra.md`, `site/umbra.html`,
`ACKNOWLEDGEMENTS.md`. Add your hardware results there (the same rows as the release notes), then merge
into `main` and push; `pages.yml` validates and deploys. Validate locally first:

```sh
node scripts/launch/check_hub_boundary.mjs && node scripts/launch/check_public_site.mjs
node --test scripts/launch/check_publication_limits.test.mjs scripts/launch/check_downloads.test.mjs scripts/launch/check_disclosures.test.mjs
```

The site's download buttons (`site/assets/downloads.mjs`) find the newest `umbra-linux-v*` release by
themselves. The hard-coded fallback links and text already say 0.5.2. The website lists compositor
series (KWin 6.6/6.7, Mutter 50/51), not exact versions.

### D. Tell the maintainer

Report what was published, with links, and the final testing state of each download.

## 7. Hardware testing (do this first)

The cloud session had no hardware, so everything in section 4 marked "No" or "None" is untested on a
real PC. Ask the maintainer which machines and distributions are available. Earlier records name the
Ubuntu 26.04.1 PC "VEGA" (RTX 2060 SUPER, NVIDIA 595.99.02), an AMD Radeon RX Vega, and Eclipse on an
LG G5 as the client. Reuse the probes and methods of those records where they exist (the measurement
sections of `packaging/linux/kwin-virtual-hdr/README.md` and `packaging/linux/mutter-virtual-hdr/README.md`,
`docs/umbra-linux-coop.md`, the private `test-records/`). Note exact package versions, Umbra version,
GPU and driver, and what was measured, not impressions.

Install the candidate Flatpak over 0.5.1 as a user would:

```sh
flatpak install --user --reinstall Umbra-0.5.2-linux-x86_64.flatpak
flatpak info --user io.github.PreceptorOfMagic.Umbra | grep -i version       # 0.5.2
```

Useful signals: the host log line `KDE virtual displays offer HDR` / `GNOME virtual displays offer
HDR`; `curl -s http://localhost:47989/serverinfo` (`UmbraCoopHdr` 0 -> 1, `ServerCodecModeSupport`
0x101 -> 0x301); `kscreen-doctor -o` (KDE: the virtual output, its mode and HDR state); the client's
`HDR_MODE enabled=1`; frame counts over a fixed time. Each extra's README.txt has its install, VERIFY and
rollback commands.

In order of value:

1. **Umbra 0.5.2 on Ubuntu 26.04, KDE Plasma 6.6** (stock KWin, then the KDE extra `+umbrahdr4`): an
   ordinary stream on its own virtual display at the client's mode, co-op with a Windows host,
   Disconnect/Resume/Quit; then with the extra: HDR desktop stream and HDR co-op pane. Compare with the
   KWin README's measurements. This checks that the `kde-output.c` change (registry fallback, nearest
   mode) did not regress KWin 6.6.
2. **Umbra 0.5.2 on Ubuntu 26.04, GNOME 50.1** with the GNOME extra `+umbrahdr2` (patch revision 2): as
   in the Mutter README (HDR desktop, HDR co-op pane, SDR unchanged). Revision 2 fixes two memory leaks,
   so also watch `gnome-shell`'s memory (`ps -o rss= -C gnome-shell`) over a long HDR stream (30 min or
   more); it should stay flat.
3. **KDE Plasma 6.7** on any KWin 6.7 distribution (Fedora 44/45 KDE, Ubuntu 26.10, openSUSE Tumbleweed,
   Arch, CachyOS): Umbra 0.5.2 gets its own virtual display at the requested mode (KWin reports 60 Hz as
   59.963 Hz; `kscreen-doctor -o` shows it), co-op, then that distribution's KDE extra: HDR stream,
   `UmbraCoopHdr` 1. Also check that Umbra 0.5.1 really fails there (no virtual display), as the docs
   claim.
4. **Mutter 51** (Fedora 45 Workstation, Ubuntu 26.10, Arch, CachyOS): that distribution's GNOME extra,
   HDR stream.
5. Any other extra on its own distribution. Each one tested moves its row from "None".

### Recording results

Change only what was actually run. For each hardware result update all of:

- `packaging/linux/hdr-extras/build.sh`, `tested_note()`: the "On hardware" lines for that target and
  series. This is the README.txt the release builds.
- `packaging/linux/hdr-extras/release-table.sh`, `hardware()`: the same, one line, for the notes table.
- `packaging/linux/release-notes/umbra-linux-v0.5.2.md`: "How far this release is tested" (the Flatpak
  bullet) and "What was tested" (the desktop table; the KDE Plasma 6.7 row says "untested on hardware").
- `.github/README.md`: "How far 0.5.2 is tested", the desktop table, and the HDR extras table.
- The patch READMEs: a new "Measured on ..." section, like the existing ones.
- `docs/umbra-linux-coop.md`, if the result belongs in the development record.
- Hub: `docs/installation.md`, `site/setup.html` ("How far it is tested", the desktop table, the per-extra
  table), and the testing sentence in `docs/umbra.md` / `site/umbra.html`.

Test the generated text before committing. This prints README.txt's TESTED for one target without a
build:

```sh
sed -n '/^tested_note() {/,/^}/p;/^series_status() {/,/^}/p' packaging/linux/hdr-extras/build.sh > /tmp/tn.sh
bash -c 'source /tmp/tn.sh; target=ubuntu26.04 component=kwin series=6.6 compositor=KWin distro_name="Ubuntu 26.04"; tested_note'
```

Pushing changes to `packaging/linux/hdr-extras/**` starts a 14-build extras run (about 1 h), which also
re-checks the new text.

### If a test fails

- A patch problem: fix the patch, raise that series in `packaging/linux/hdr-extras/revisions`, and push.
  The extras workflow rebuilds and smoke-tests; download the new candidates and retest.
- An Umbra problem: fix it on the branch, then pin the manifest (`BUILD_VERSION` stays `v0.5.2`;
  `COMMIT` and the git source `commit` become the fix commit) in a separate packaging commit, as `7456c15`
  did. The push starts a Flatpak dry run (`linux-release.yml`, about 1.5–2.5 h); download that bundle
  and retest. Update the release notes ("Nothing else changed: ..." names what changed since 0.5.1).
- Something that cannot be fixed now: state it as a known limit in the notes, README and website, and
  ask the maintainer whether to publish with it.

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
