# Keeping Umbra in step with Apollo

Umbra is Apollo with the additions Eclipse needs for co-op, and as little else changed as
possible. Apollo (ClassicOldSong) is still maintained, so Umbra merges Apollo's updates instead of
redoing them. Everything below exists to keep those merges cheap.

## The rule: Umbra's changes live in Umbra's files

A change to an Apollo file is a place where an Apollo update can conflict. So:

- **New behaviour goes in new files** (`src/aurora_link/`, `src/wired_pin.*`, `src/beta_profile.*`,
  `gpu_hevc_device.inc`, ...) with as few hook lines as possible in Apollo's files. Mark each hook
  with an `Umbra:` comment.
- **Identity is layered on, not edited in:**
  - text: `src_assets/common/assets/web/umbra_identity.js` renames "Apollo" to "Umbra" in every
    language when the page loads and adds Umbra's own wording. Apollo's 20 translation files are
    never edited (they conflicted on every Apollo translation update).
  - colours: `public/assets/css/umbra.css`, loaded by one line in `template_header.html`.
  - home page: `UmbraCard.vue` (version, update notice, Umbra links, Umbra legal). Apollo's version
    card stays in `index.html`, hidden, so Apollo's edits to it still merge.
  - artwork: Umbra's images under Apollo's filenames (Apollo has not changed an image since v0.4.6),
    all rendered by `scripts/umbra-artwork/render.cjs` from `brand-mark.svg` -- a byte-identical copy
    of the Project Hub's mark. Change the master and re-render; never edit an output by hand.
  - GitHub landing page: `.github/README.md` (GitHub shows it before the root one). The root
    `README.md` stays Apollo's.
  - Windows names: one-line or appended changes (project name, service display name appended to
    `install-service.bat`, firewall rule, tray menu). The install folder and registry key stay
    `Apollo` so an Apollo install upgrades in place, keeping its config and pairings.
- **`scripts/umbra-delta.txt` lists every Apollo file Umbra may change, with the reason.**
  `node scripts/umbra_check.mjs [apollo-ref]` fails on an unlisted change, on any edit to Apollo's
  README, translations or resource/client cards, and on a missing identity hook.

## Versions

Umbra builds are versioned `<Apollo release>-umbra.<n>`, for example `0.4.6-umbra.1`, set at
configure time (`BRANCH` and `BUILD_VERSION` environment variables, read by Apollo's
`cmake/prep/build_version.cmake`). Release tags are `v0.4.6-umbra.1`. The Umbra card compares that
Apollo release with Apollo's latest GitHub release, and with Umbra's latest GitHub release, to say
whether a newer Apollo is out and whether Umbra has caught up.

"Release" means a published GitHub release, as for Apollo's own notice: on 2026-10-03 Apollo's latest
was still v0.4.6 (July 2025) -- v0.4.8 exists only as a git tag, and v0.4.7-alpha.1 is a pre-release
(shown only with PreRelease Notifications on).

`build_version.cmake` does NOT cache that version: if ninja re-runs cmake on its own (after a merge
touches a cmake file) from a shell without those variables, the build silently becomes
`0.0.0.<commit>`, which the Umbra card reports as a development build with no known Apollo base.
Build through a script that sets them every time, and check the version line in the log.

Apollo's security fixes can land on its `master` long before a release (v0.4.8's successors did),
so **merge Apollo's `master`**, not only its release tags.

## Merging an Apollo update

1. `git fetch origin` (`origin` is ClassicOldSong/Apollo; never push to it). `git config rerere.enabled true`
   once, so a conflict resolved before is resolved the same way again.
2. `git merge origin/master` on the Umbra branch. Resolve conflicts; prefer Apollo's version
   wherever Apollo now does what Umbra's change did (then drop that file from `umbra-delta.txt`).
3. `node scripts/umbra_check.mjs` -- the delta is still what `umbra-delta.txt` says.
4. Build, package, install over an existing install, and **look**: the home page (Umbra card,
   update notice), the config pages in light and dark mode, the tray icon and its menu. A restyle
   upstream merges cleanly and can still leave `umbra.css` pointing at nothing.
5. Stream once, then a co-op session -- the features Umbra adds live inside Apollo's streaming code.
6. Bump the version to the new Apollo release (`-umbra.1`).
