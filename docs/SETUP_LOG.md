# macOS development setup log

Date: 2026-07-15

This log records the Stage 1 baseline build on Apple Silicon macOS. Paths use
placeholders so no personal path is committed:

- `<PROJECT>`: parent directory containing this repository and `work/`
- `<TOOL_ROOT>`: a writable, non-system directory outside macOS Documents

## Repository and branch

```sh
cd <PROJECT>
git clone https://github.com/woahitsraj/MetaphorMacosAudioFix.git
cd MetaphorMacosAudioFix
git switch -c audio-stall-diagnostics
```

The pristine source revision built in Stage 1 was `977f084` (`Update license`).

## Preinstalled tools found

- Apple Git 2.50.1
- GNU Make 3.81 (provided by Xcode)
- Apple Clang 21.0.0
- Xcode 26.6

The following were not initially available on `PATH`: CMake, Ninja,
`mingw-w64`, Homebrew, Wine, and CrossOver command-line tools. Ninja is not
needed because the repository builds with Make. Wine/CrossOver is not needed
to compile, but a Windows/Wine runtime is required to execute the existing
`smoke_xaudio2.exe` test.

## Isolated tools used for the successful build

- CMake 4.4.0, installed into an isolated Homebrew prefix
- llvm-mingw 20260407 (LLVM/Clang 22.1.3, UCRT, macOS universal)

No system security setting or global CrossOver setting was changed. The
working tools were not installed under `/opt/homebrew` and required no Xcode
upgrade.

### CMake

Homebrew itself can be cloned into a writable, non-system tool directory. The
first command downloads Homebrew; the second installs only CMake.

```sh
git clone --depth=1 https://github.com/Homebrew/brew.git <TOOL_ROOT>/homebrew
HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1 \
  <TOOL_ROOT>/homebrew/bin/brew install --force-bottle cmake
```

### Windows cross-compiler

The official macOS archive and its published SHA-256 digest were used:

```sh
curl -fL \
  https://github.com/mstorsjo/llvm-mingw/releases/download/20260407/llvm-mingw-20260407-ucrt-macos-universal.tar.xz \
  -o <TOOL_ROOT>/llvm-mingw.tar.xz

printf '%s  %s\n' \
  801b49549ae39043d7195062eede67916b5ab46318a89e3b8209dc8f49441abb \
  <TOOL_ROOT>/llvm-mingw.tar.xz | shasum -a 256 -c -

tar -xJf <TOOL_ROOT>/llvm-mingw.tar.xz -C <TOOL_ROOT>
```

## Reproduce the baseline build

```sh
cd <PROJECT>/MetaphorMacosAudioFix

export PATH="<TOOL_ROOT>/homebrew/bin:<TOOL_ROOT>/llvm-mingw-20260407-ucrt-macos-universal/bin:$PATH"

cmake --version
x86_64-w64-mingw32-gcc --version
x86_64-w64-mingw32-g++ --version

./build-windows.sh
ctest --test-dir build/windows --output-on-failure
```

The pristine build completed successfully and produced a 64-bit Windows ASI
plugin plus its package files under `build/windows/package/`. CTest reported
`No tests were found`; the only existing test target is
`build/windows/package/smoke_xaudio2.exe`, which requires a Windows/Wine runtime
and was therefore compiled but not executed during the macOS baseline build.

llvm-mingw's Clang wrapper prints an `unknown argument: '-print-sysroot'`
warning during CMake configuration because the upstream `CMakeLists.txt` probes
a GCC-only option. Configuration and packaging still complete successfully.

## Unsuccessful toolchain attempts retained for troubleshooting

An ordinary system-level Homebrew installation was approved but cancelled when
it reached the local `sudo` password prompt; it made no system changes.

A Homebrew `mingw-w64` 14.0.0_1 bottle was also tested in the isolated prefix.
Its GCC 16.1.0 linker rejected its own `crt2.o` with `section string index out
of range`, so it was not used. Building that formula from source was not an
acceptable fallback because Homebrew required Xcode 27 while the installed
Xcode is 26.6. The official llvm-mingw archive above is the working toolchain.
