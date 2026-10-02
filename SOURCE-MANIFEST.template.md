# Umbra source and artifact manifest — release template

> **TEMPLATE — NOT RELEASE EVIDENCE.** Copy this file to
> `SOURCE-MANIFEST.md`, replace every placeholder, and attach the completed
> manifest to the matching binary release. A package containing only this
> template has not satisfied the release gate.

**Current gate: BLOCKED.** SudoVDA is unresolved as described below. Do not
publish a release while that section is incomplete.

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

## SudoVDA / nefconc mandatory closure

**Status: BLOCKED — all boxes must be completed before public packaging or
publication. Do not identify SudoVDA as MIT without a component-by-component
licence mapping.**

Current inspected files:

| File | SHA-256 / state |
| --- | --- |
| `SudoVDA.inf` (`DriverVer = 07/14/2025,1.10.9.289`) | `ad69ac682756f0cf339b081fac7e6e8159fdf2ca01ca69df8945c7246c286925` |
| `sudovda.cat` | `2f9189de5604bec9d86f51640cc540639e394d9ad0f8e689129375e95f2d22f8` |
| `sudovda.cer` | `6accdcd519f6179d967db4eaa20ecf25a732ba30e87f4cffebc768b2c13c9007` |
| `install.bat` | `4e8ceda6094b185ff826aa13d51d6d54f7d399396b21608d169b8328c0e4ed0e` |
| `uninstall.bat` | `c9458b8ad77eeae107057eb76a759e90abdb64184b75910c7ca2ec7dc4728044` |
| Required `SudoVDA.dll` | **MISSING — no hash/source mapping** |
| Required `nefconc.exe` | **MISSING — no hash/source mapping** |
| Compiled `third-party/sudovda/sudovda-ioctl.h` | `dd924c9bd3918f75bdc1c1153ed16f23ae1658fde890d76f5f4f35397062dd73` |
| Compiled `third-party/sudovda/sudovda.h` | `8ebbab2002a00ca410b18d33e4202dd5f39dd9c3810f8c3fcce7c180e99c5efa` |

Required closure evidence:

- [ ] Exact source repository and immutable revision for the INF, CAT, DLL,
      headers, and `nefconc.exe`.
- [ ] SHA-256 for every shipped driver/tool file, including the DLL and
      `nefconc.exe`.
- [ ] Reproducible build instructions and build-material archive for each
      self-built binary.
- [ ] Verbatim standalone and inherited licence texts, with a file-by-file
      mapping covering the SudoMaker changes, Microsoft-derived material,
      parent VirtualDrivers material, and nefconc.
- [ ] Signature issuer, timestamp, verification command, and result for every
      signed binary/catalog.
- [ ] Clean-machine install, device creation, streaming, uninstall, and
      rollback evidence using the exact release package.

The public SudoMaker/SudoVDA repository HEAD inspected during preparation was
`a4b09fa2aa731a964d0cb5d139cb1e6240e4da12`; it is not accepted as the source
mapping for the newer local driver artifacts until provenance is demonstrated.

## Final release gate

- [ ] Completed `SOURCE-MANIFEST.md` contains no placeholders.
- [ ] Package inventory matches the binary artifact byte-for-byte.
- [ ] All notice/licence files are present in NSIS and ZIP staging.
- [ ] Exact release source and build material are publicly reachable.
- [ ] SudoVDA/nefconc closure above is complete.
- [ ] Artifact and source hashes were independently rechecked.

Release approval: `[fill: reviewer, date, and evidence link]`
