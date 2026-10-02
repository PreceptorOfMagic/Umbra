# Third-party notices and release provenance

This index records the inspected Windows AMD64 forms of the components below.
The verbatim upstream licence texts are installed in `licenses/`. The project's
own terms remain in `LICENSE`, and Valve's existing trademark notice remains in
`NOTICE`.

This is a targeted notice bundle, not a complete SBOM or a declaration that the
whole package is ready for public distribution. A completed
`SOURCE-MANIFEST.md` and a full dependency review are required for each release.

## x265 4.1, dynamicHDR10/HDR10+, and json11

- **Shipped form:** `libx265.a` and `libhdr10plus.a` are static Windows AMD64
  build inputs and their code is linked into the application, rather than
  shipped as separate libraries.
- **Source pin:** x265 commit
  [`1d117bed4747758b51bd2c124d738527e30392cb`](https://github.com/LizardByte-infrastructure/x265_git/commit/1d117bed4747758b51bd2c124d738527e30392cb)
  (x265 4.1/build 215). The nested `source/dynamicHDR10` and its
  `json11` directory are part of that same commit.
- **Build provenance:** LizardByte/build-deps source recipe commit
  [`6ad5cf841f592f95be47fb401cde02ae621acd0f`](https://github.com/LizardByte/build-deps/commit/6ad5cf841f592f95be47fb401cde02ae621acd0f),
  represented by the checked-out binary `dist` commit
  `cf5dffaf4c62a5e5e2949c37b642cfc5ad962b98`.
- **Inspected input hashes:** `libx265.a` SHA-256
  `4b81b82a6ed80ced8c506e21a0ce925e7ae5e399768f91401df0a68900a2d028`;
  `libhdr10plus.a` SHA-256
  `f7280580ebc36c397b401b55a23ab8e9f5b0303d7a824b53d0cf0e381768cfd7`.
- **x265 terms:** GPL version 2 or later, or a separate commercial proprietary
  licence. No commercial grant is recorded for this build; the applicable
  upstream text is `licenses/x265-COPYING-GPL-2.0.txt`.
- **dynamicHDR10 terms and credit:** GPL version 2 or later, or a separate
  commercial proprietary licence. Copyright (C) 2013-2020 MulticoreWare, Inc.
  Authors: Bhavna Hariharan `<bhavna@multicorewareinc.com>` and Kavitha Sampath
  `<kavitha@multicorewareinc.com>`. See
  `licenses/dynamicHDR10-LICENSE.txt`.
- **Nested json11 terms:** MIT; Copyright (c) 2013 Dropbox, Inc. See
  `licenses/json11-MIT.txt`.

The completed release manifest must give recipients the exact Corresponding
Source and build material for the statically linked GPL-covered code. The pins
above identify inputs; they are not by themselves a completed source delivery.

## ViGEmClient

- **Shipped form:** sources are compiled directly into the Windows application.
- **Source pin:** the `third-party/ViGEmClient` submodule is LizardByte's
  `Virtual-Gamepad-Emulation-Client` commit
  [`8d71f6740ffff4671cdadbca255ce528e3cd3fef`](https://github.com/LizardByte/Virtual-Gamepad-Emulation-Client/commit/8d71f6740ffff4671cdadbca255ce528e3cd3fef).
- **Terms:** MIT. The repository licence credits Copyright (c) 2018 Benjamin
  Höglinger-Stelzer; the compiled source headers also carry Copyright (c)
  2017-2023 Nefarius Software Solutions e.U. and Contributors. Both verbatim
  notices are preserved in `licenses/ViGEmClient-MIT.txt` and
  `licenses/ViGEmClient-source-header-MIT.txt`.

## ViGEmBus

- **Shipped form:** the upstream
  `ViGEmBus_1.21.442_x64_x86_arm64.exe` installer is bundled as
  `scripts/vigembus_installer.exe`.
- **Source/release pin:** tag
  [`v1.21.442.0`](https://github.com/nefarius/ViGEmBus/releases/tag/v1.21.442.0).
- **Installer SHA-256:**
  `155c50f1eec07bdc28d2f61a3e3c2c6c132fee7328412de224695f89143316bc`.
- **Terms:** BSD 3-Clause; Copyright (c) 2016-2020, Nefarius Software
  Solutions e.U. See `licenses/ViGEmBus-BSD-3-Clause.txt`.

## qrcodejs

- **Shipped form:** minified web asset
  `assets/web/public/assets/js/qrcode.min.js`.
- **Source pin:** davidshimjs/qrcodejs commit
  [`04f46c6a0708418cb7b96fc563eacae0fbf77674`](https://github.com/davidshimjs/qrcodejs/commit/04f46c6a0708418cb7b96fc563eacae0fbf77674).
- **Asset SHA-256:**
  `c541ef06327885a8415bca8df6071e14189b4855336def4f36db54bde8484f36`.
- **Terms:** MIT; Copyright (c) 2012 davidshimjs. See
  `licenses/qrcodejs-MIT.txt`.

## Valve notice

The pre-existing Valve Corporation Steam trademark notice is preserved verbatim
in the installed root `NOTICE` file.

## SudoVDA — unresolved public-release blocker

**SudoVDA is not classified as MIT here and is not cleared for public release.**
The current package input is incomplete and cannot be mapped to a complete,
licensed source set:

- `SudoVDA.inf` declares `DriverVer = 07/14/2025,1.10.9.289` and requires
  `SudoVDA.dll`, but that DLL is absent from the repository/package input.
- `install.bat` invokes `nefconc.exe`, but that executable is also absent, so
  neither its binary hash nor its exact source and licence can be recorded.
- The public SudoMaker/SudoVDA repository currently resolves to commit
  `a4b09fa2aa731a964d0cb5d139cb1e6240e4da12`, has no standalone licence file,
  and contains Microsoft-derived and inherited material whose applicable terms
  have not been mapped to the newer packaged driver artifacts.

Do not publish a package containing or depending on this component until the
missing DLL and `nefconc.exe` are present and hashed, every artifact is tied to
an exact source revision/build recipe, the inherited licence chain is recorded
with its verbatim texts, and a clean install has been verified.
