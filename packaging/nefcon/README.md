# nefconc source build

Checkout https://github.com/nefarius/nefcon at
`f5dd078b3d3045be3127d962c62b1c4b2710bb9a` (v1.12.0), including its vcpkg
submodule at `8d88de013751588c7361735aa748490619775058`.
Copy this directory's `vcpkg-configuration.json` and `umbra-overlays/` into that
checkout. The overlay preserves upstream neflib v1.0.1 and removes only the
empty `PLATFORM ${PLATFORM}` argument; vcpkg then selects the x64 generator
architecture instead of silently producing x86 libraries.

Use a Visual Studio 2022 x64 developer command prompt. Set `PKG_CONFIG` to an
installed native pkgconf.exe, and `VCPKG_KEEP_ENV_VARS=PKG_CONFIG` so the pinned
vcpkg does not try an unavailable historic MSYS runtime download. Then run:

```
vcpkg\bootstrap-vcpkg.bat -disableMetrics
msbuild /p:Configuration="Release (Console)" /p:Platform=x64 NefConUtil.vcxproj -verbosity:minimal
```

Copy `bin/x64/nefconc.exe` into `src_assets/windows/drivers/sudovda/` and update
its SHA-256 in `PROVENANCE.json`. Preserve the project's MIT licence and linked
dependency notices. The inspected result imports only Windows system DLLs.
It is intentionally unsigned because it is a locally compiled userspace helper,
not a downloaded kernel driver. Diagnostic compiler paths are not runtime inputs.
