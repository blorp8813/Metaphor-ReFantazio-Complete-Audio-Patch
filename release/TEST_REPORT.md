# Metaphor: ReFantazio Complete Audio Patch v0.2.0-rc.1 — Test Report

Verification date: 2026-07-22. Branch: `audio-buffer-recovery-release`.

| Check | Result |
|---|---|
| Portable stall-detector tests | PASS |
| Portable buffer-recovery tests | PASS, including 1440/480/992 → 448 regression and same-size retry |
| Warning build (`-Wall -Wextra -Wpedantic`) | PASS, no warnings after strict-prototype cleanup in vendored MinHook |
| Windows x86-64 ASI build | PASS |
| Windows stall and recovery tests under CrossOver | PASS |
| Logger shutdown harness under CrossOver | PASS |
| Production configuration and license gate | PASS |
| Public fault-injection CMake option | OFF |
| Release ZIP deterministic rebuild comparison | PASS |
| Release ZIP inspection and privacy scan | PASS |

## Controlled game observations

- One injected buffer-too-large recovery succeeded without an audible interruption.
- Ten natural buffer-too-large events recovered; audio remained audible and full processing resumed.
- More than 16 minutes of subsequent runtime showed no delayed instability.
- These limited observations do not establish that every possible audio-dropout cause is fixed.

## Environment

- Apple Silicon macOS host
- Apple Clang 21.0.0 for portable tests
- CMake 4.4.0
- LLVM/Clang MinGW 22.1.3 targeting `x86_64-w64-windows-gnu`
- CrossOver Preview 27.0.0.40646 (`cxpreview-20260702-rc1`) using the existing Steam bottle

The final commit and artifact checksums are recorded in the external release manifest generated after the source commits. No game files, saves, game executables, personal diagnostic logs, or personal filesystem paths are included.
