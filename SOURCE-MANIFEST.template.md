# Umbra source and artifact manifest — release template

> **TEMPLATE — NOT RELEASE EVIDENCE.** Copy this file to
> `SOURCE-MANIFEST.md`, replace every placeholder, and attach the completed
> manifest to the matching binary release. A package containing only this
> template has not satisfied the release gate.

This reusable template is not the candidate manifest. See `SOURCE-MANIFEST.md`
for the current build inputs and `docs/release-installer-audit.md` for test limits.

## Release identity

| Field | Required value |
| --- | --- |
| Product/version | `[fill: exact Umbra version]` |
| Release tag | `[fill: immutable tag]` |
| Release date (UTC) | `[fill: YYYY-MM-DD]` |
| Umbra source commit | `[fill: full 40-character commit]` |
| Source-tree state | `[fill: clean, or attach the complete diff]` |
| Binary artifact filename | `[fill]` |
| Binary artifact SHA-256 | `[fill]` |
| Binary artifact size | `[fill: bytes]` |
| Completed SBOM / package inventory | `[fill: filename, SHA-256, and URL]` |
| Completed third-party notice scan | `[fill: command and result]` |

## Exact source delivery

| Field | Required value |
| --- | --- |
| Complete release-source archive URL | `[fill: immutable public URL]` |
| Release-source archive SHA-256 | `[fill]` |
| Archive format and size | `[fill]` |
| Build scripts/patches included | `[fill: paths and commit/diff]` |
| Installation information included | `[fill: path/URL]` |
| Written source offer, if used | `[fill: exact text, duration, and contact; otherwise N/A]` |

The source delivery must contain the exact Corresponding Source for the binary,
including local changes, dependency build recipes, interface definitions, and
the scripts/material used to control compilation and installation. Repository
links alone are not a substitute for a release-matched source bundle.

## Dependency pins and inspected Windows AMD64 inputs

Re-verify every value against the release tree and artifact; do not copy these
anchors forward without checking them.

| Input | Required release record |
| --- | --- |
| `third-party/build-deps` binary dist | commit `cf5dffaf4c62a5e5e2949c37b642cfc5ad962b98`; verify `[fill: actual]` |
| LizardByte/build-deps source recipe | commit `6ad5cf841f592f95be47fb401cde02ae621acd0f`; archive URL/SHA-256 `[fill]` |
| x265 4.1/build 215 | commit `1d117bed4747758b51bd2c124d738527e30392cb`; archive URL/SHA-256 `[fill]` |
| `libx265.a` | inspected SHA-256 `4b81b82a6ed80ced8c506e21a0ce925e7ae5e399768f91401df0a68900a2d028`; release value `[fill]` |
| `libhdr10plus.a` | inspected SHA-256 `f7280580ebc36c397b401b55a23ab8e9f5b0303d7a824b53d0cf0e381768cfd7`; release value `[fill]` |
| ViGEmClient | commit `8d71f6740ffff4671cdadbca255ce528e3cd3fef`; archive URL/SHA-256 `[fill]` |
| ViGEmBus installer | tag `v1.21.442.0`; SHA-256 `155c50f1eec07bdc28d2f61a3e3c2c6c132fee7328412de224695f89143316bc`; release value `[fill]` |
| qrcodejs minified asset | commit `04f46c6a0708418cb7b96fc563eacae0fbf77674`; SHA-256 `c541ef06327885a8415bca8df6071e14189b4855336def4f36db54bde8484f36`; release value `[fill]` |

Record every other submodule, downloaded binary, static library, DLL, driver,
web asset, and generated file in the completed SBOM, with its exact version,
source URL, licence, shipped form, and SHA-256.

## Reproducible build record

| Field | Required value |
| --- | --- |
| OS/architecture | `[fill]` |
| Compiler and linker | `[fill: names and exact versions]` |
| CMake and Ninja versions | `[fill]` |
| MSYS2/mingw packages | `[fill: exact package versions/export]` |
| Environment/toolchain file | `[fill: path and SHA-256]` |
| Full configure command | `[fill]` |
| Full build command | `[fill]` |
| Full package command | `[fill]` |
| Build log | `[fill: filename, SHA-256, URL]` |

A public release configure command **must explicitly contain**:

```text
-DAPOLLO_BETA_PROFILE=OFF
```

Paste the complete command actually used; do not merely assert that the option
was off. Record the generated cache value and a release-binary verification
that demonstrates the beta profile was not compiled in or activated.

## Signed driver and source provenance

Copy the exact `drivers/sudovda/PROVENANCE.json` for this build. Preserve its
explicit distinction between authenticated upstream driver distribution and
reproducible source correspondence. Record the locally built nefconc source pin,
overlay, binary hash and build log. Include every licence notice.

## Final release gate

- Completed candidate manifest and matching source archives are present.
- Package inventories and all binary/source hashes are independently checked.
- Clean-machine install, upgrade, stop, uninstall and rollback evidence is recorded.
- Source and required build material are available alongside the distribution.

Release approval: `[fill: reviewer, date, and evidence link]`
