# Umbra 0.4.6-umbra.5-rc1 candidate source manifest

Prepared 2026-10-08. This is a local test candidate; no public release tag or
public distribution has been created. Runtime release qualification is tracked
in `docs/release-installer-audit.md`.

## Source delivery

The sibling `umbra-project-source.tar.gz` contains the committed Umbra build
source and recursive Git-pinned submodule source. `umbra-external-source.tar.gz`
contains GPU HEVC source, pinned x265/OpenCL source, nefcon and its source build
material, public SudoVDA source, and other dependency source collected for this
candidate. The source inventory records exact Git revisions and per-file hashes.
Release artifact/source hashes are outside the packages in `SHA256SUMS`, avoiding
a circular binary-hash reference. Neither source archive is a public source offer.

These files live together under `release-candidates/2026-10-08/umbra/` in the
release workspace. Keep them beside the candidate installers when transferring
for isolated testing. The external source inventory explicitly lists any remaining
correspondence gaps; possession of the main Git repository alone is insufficient.

## Build inputs

- Umbra branch: codex/release-installers, based on Apollo-derived 8973cdc9.
  Exact snapshot commit is in the source inventory and generated package version.
- Windows: MSYS2 UCRT64 GCC16.1, Ninja, CMake Release;
  `-DAPOLLO_BETA_PROFILE=OFF -DUMBRA_GPU_HEVC_DIR=<built GPU payload>`.
- Linux: isolated Ubuntu24.04 x86_64, GCC13 main compiler and GCC12 CUDA host,
  CUDA12.0; Release, `/usr` prefix, assets `share/sunshine`, executable
  `/usr/bin/sunshine`, `-DSUNSHINE_ENABLE_CUDA=ON -DAPOLLO_BETA_PROFILE=OFF`.
  This is not evidence of compatibility with older or every Linux distribution.
- Main FFmpeg/x264/x265/SVT-AV1 static inputs: build-deps distribution commit
  cf5dffaf4c62a5e5e2949c37b642cfc5ad962b98, source recipe commit
  6ad5cf841f592f95be47fb401cde02ae621acd0f and its pinned submodules.
- GPU HEVC: audited v12b source; x265 b81f650e21e8aacbe6a9ad04ce14aefc05b932c0,
  OpenCL-Headers386ca390b2f52efeb3e1a55a500690eb8013f60e. DLL provenance and
  hashes are installed at `gpu-hevc/BUILD-PROVENANCE.json`.
- nefconc: v1.12.0 f5dd078b3d3045be3127d962c62b1c4b2710bb9a, locally built
  Visual Studio2022 x64 static runtime. Exact overlay and recipe are in
  `packaging/nefcon/`; driver-folder PROVENANCE.json identifies its binary hash.

## Signed driver exception

SudoVDA DLL, INF, CAT and certificate come from the official Apollo0.4.6 release.
`drivers/sudovda/PROVENANCE.json` records all hashes and successful Windows
Authenticode verification. Public source a4b09fa2aa731a964d0cb5d139cb1e6240e4da12
is included for inspection, but upstream does not publish a reproducible
source-to-signed-driver attestation. It is not claimed to prove exact binary
correspondence. See installed SudoVDA and inherited Microsoft notices.

The ViGEmBus installer is the hash-pinned official v1.21.442.0 release, used as a
shared system prerequisite. Uninstall must preserve shared drivers used by other
applications. No driver was installed during candidate compilation.
