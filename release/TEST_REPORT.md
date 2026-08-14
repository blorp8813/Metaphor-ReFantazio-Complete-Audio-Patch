# Metaphor: ReFantazio Complete Audio Patch v1.0.0 — Test Report

Verification date: 2026-08-13. Branch: `main`.

| Check | Result |
|---|---|
| Portable stall-detector tests | PASS |
| Portable buffer-recovery tests | PASS, including zero-availability fallback |
| Warning build (`-Wall -Wextra -Wpedantic`) | PASS, no warnings |
| Windows x86-64 ASI and installer build | PASS |
| Windows recovery tests under CrossOver | PASS |
| Windows stall-detector tests under CrossOver | PASS |
| Windows logger-shutdown harness under CrossOver | PASS |
| Production configuration and license gate | PASS |
| Public fault-injection CMake option | OFF |
| Release ZIP deterministic rebuild comparison | PASS, byte-identical |
| Release ZIP inspection | PASS |

## Controlled game observations

- A 93-minute natural session recovered 21 buffer-pressure events. The closest
  event left 32 frames available; there were no zero-availability events,
  final errors, stalls, or device changes.
- A test-only trigger supplied the same 1440/1440 full-buffer geometry seen in
  the external report, without touching a real render buffer first.
- The real CrossOver Stop, Reset, Start, padding refresh, and 480-frame
  `GetBuffer` all succeeded. The complete fallback took 2.751 ms.
- The tester heard no interruption. Audio-clock progress and render callbacks
  continued afterward with zero GetBuffer or ReleaseBuffer failures.

## External report

The first external diagnostic log came from a MacBook Pro with an M1 Pro,
macOS Tahoe 26.5.2, CrossOver 26.2, game version 1.02, and built-in speakers.
Across three launches it recorded 1,521 buffer-too-large events, roughly 1,500
successful adaptive recoveries, and one definitive final failure with a
480-frame request, 1440/1440 frames occupied, zero frames available, and
`AUDCLNT_E_BUFFER_TOO_LARGE`. That final geometry is the condition addressed by
the newly enabled fallback.

## Primary test environment

- MacBook Air with Apple M5, 10-core CPU, 10-core GPU, and 24 GB unified memory
- macOS 27.0 build `26A5378n`
- Apple Clang for portable tests
- CMake 4.4.0
- LLVM/Clang MinGW 22.1.3 targeting `x86_64-w64-windows-gnu`
- CrossOver Preview 27.0.0.40646 using Windows Steam
- Built-in MacBook Air speakers and AirPods Max
- Exact game build not recorded

No game files, saves, game executables, personal diagnostic logs, or personal
filesystem paths are included in the release package.
