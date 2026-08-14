# Reproducible build and verification guide

The project targets Windows x86-64. CMake 3.20+, a C++20 compiler for portable tests, and an x86_64-w64-mingw32 toolchain are required. The exact compiler build can affect binary checksums, so record tool versions with every release.

## Portable tests

```bash
./tests/run-host-tests.sh
```

This compiles with `-Wall -Wextra -Werror` (and `-Wpedantic` for recovery tests) and runs the stall-detector and buffer-recovery state machines natively.

## Windows build

Ensure `x86_64-w64-mingw32-gcc`, `g++`, and `strip` are on `PATH`, then run:

```bash
rm -rf build/windows
./build-windows.sh
```

For an explicit warning build:

```bash
cmake -S . -B build/warnings \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_C_FLAGS='-Wall -Wextra -Wpedantic' \
  -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic'
cmake --build build/warnings --config Release
```

The default CMake option `METAPHOR_COMPLETE_AUDIO_PATCH_ENABLE_FAULT_INJECTION=OFF` must remain off for public artifacts.

## Target-runtime logger harness

Run `build/windows/logger_shutdown_tests.exe` through an existing Wine or CrossOver command-line environment. A passing result is `logger_shutdown: PASS`. Do not alter a user's global Wine or CrossOver configuration merely to run the harness.

## Release package

```bash
./scripts/verify-release-config.sh
SOURCE_DATE_EPOCH=1786579200 ./scripts/package-release.sh v1.0.0
shasum -a 256 dist/Metaphor-ReFantazio-Complete-Audio-Patch-v1.0.0-win64.zip
```

The package script normalizes staged file timestamps to `SOURCE_DATE_EPOCH` (use `1786579200` for v1.0.0), sorts the ZIP input, and omits filesystem metadata. Rebuilding with the same source, toolchain, bundled DLLs, and epoch is expected to produce the same ZIP. The ASI itself is only byte-reproducible when the compiler and linker versions are also identical.

Record the commit, branch, tool versions, CMake fault-injection cache value, test results, and SHA-256 manifest in the release test report.
