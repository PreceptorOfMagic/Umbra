# Build Umbra on Windows

This is the source-build route for the private Eclipse/Umbra engineering
preview. It does not turn an unreviewed build into an official release.

## Requirements

- 64-bit Windows 10 or Windows 11; the current project co-op rigs use Windows 11.
- A supported NVIDIA, AMD or Intel GPU and driver for hardware encoding.
- [Git for Windows](https://git-scm.com/download/win).
- [MSYS2](https://www.msys2.org/) using its **UCRT64** shell.
- CMake newer than 3.25, Ninja, the UCRT64 C/C++ toolchain, Node.js and the
  development libraries installed below.

## Install build dependencies

In the **MSYS2 UCRT64** shell, update the environment first:

```bash
pacman -Syu
```

If MSYS2 asks you to close and reopen the shell, do that and run `pacman -Syu`
again. Then install the build dependencies:

```bash
pacman -S --needed \
  git \
  mingw-w64-ucrt-x86_64-boost \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-cppwinrt \
  mingw-w64-ucrt-x86_64-curl-winssl \
  mingw-w64-ucrt-x86_64-MinHook \
  mingw-w64-ucrt-x86_64-miniupnpc \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-nodejs \
  mingw-w64-ucrt-x86_64-nsis \
  mingw-w64-ucrt-x86_64-onevpl \
  mingw-w64-ucrt-x86_64-openssl \
  mingw-w64-ucrt-x86_64-opus \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-nlohmann_json
```

## Clone and build

```bash
git clone --recurse-submodules --branch beta/vega-coop \
  https://github.com/PreceptorOfMagic/umbra.git
cd umbra
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The source still contains inherited `Apollo` and `Sunshine` identifiers where
packaging, protocol or compatibility work has not yet been migrated. The
consumer-facing application name for this fork is Umbra; do not perform a blind
identifier replacement in source or configuration paths.

## Create a local package

After a successful build, create either an installer or portable archive with
the generated CPack configuration:

```bash
cpack -G NSIS --config build/CPackConfig.cmake
```

or:

```bash
cpack -G ZIP --config build/CPackConfig.cmake
```

Treat the output as a local, unsigned engineering build. Do not redistribute it
as an official Umbra release until its exact licence/NOTICE bundle, dependency
inventory, source correspondence, checksums and clean-install behaviour have
been reviewed.

## Configure and test

1. Start the host and open `https://localhost:47990`.
2. Complete the local account setup and pair one Eclipse client.
3. Verify an ordinary single-host stream before configuring co-op.
4. For co-op, create exactly one application named **Co-op Pane**, enable the
   virtual display, and leave resolution and refresh rate client-driven.
5. Follow the shared [Eclipse/Umbra setup guide](https://preceptorofmagic.github.io/setup.html)
   for pairing both hosts and assigning inputs.

Report host build, capture, encoding or virtual-display failures in
[Umbra issues](https://github.com/PreceptorOfMagic/umbra/issues).
