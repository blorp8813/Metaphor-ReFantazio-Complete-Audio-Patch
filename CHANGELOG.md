# Metaphor: ReFantazio Complete Audio Patch — Changelog

All notable changes to this project are documented here.

## [1.0.1] - 2026-08-13

### Changed

- The guided installer now upgrades an existing
  `ResetRestartFallback = false` setting to the stable `true` default
  automatically.
- All other INI settings, comments, custom log paths, formatting, and line
  endings are preserved.
- The INI update is written to a temporary file and replaced atomically, leaving
  the original untouched if migration cannot complete.

### Validation

- Added portable and Windows tests covering the old shipped INI, customized
  settings, comments, CRLF and LF endings, UTF-8 BOMs, duplicate keys, and all
  accepted false-value spellings.
- Installer migration, buffer recovery, stall detection, and logger shutdown
  tests passed under CrossOver.

## [1.0.0] - 2026-08-13

### Added

- A circuit-breaker-limited Stop/Reset/Start fallback when Wine reports the
  confirmed full-buffer condition and adaptive retry has no space available.
- Test-only process-wide fault injection for exercising the zero-availability
  recovery path without acquiring or corrupting a real render buffer first.

### Changed

- Enabled `ResetRestartFallback` in the production configuration after a live
  CrossOver test completed the entire recovery in 2.751 ms without an audible
  interruption.
- Kept client recreation disabled and fault injection compiled out of public
  builds.

### Validation

- A user log from an M1 Pro reproduced the same full-buffer geometry: a
  480-frame request, 1440/1440 frames occupied, zero available, and
  `AUDCLNT_E_BUFFER_TOO_LARGE`.
- A 93-minute natural session recovered 21 buffer-pressure events with no
  failures, stalls, or device changes.
- A controlled live recovery completed Stop, Reset, Start, refreshed padding,
  and the real 480-frame request successfully; audio and callbacks continued.

## [0.2.0-rc.1] - 2026-07-22

### Added

- Guided Windows installer and uninstaller designed to run inside the game's CrossOver/Wine bottle, with Steam game detection, browse fallback, safe handling of an existing loader, and automatic bottle-local `winmm` override configuration.
- Fix for random loss of all game audio during gameplay. When Wine temporarily cannot accept the game's full audio packet, the patch safely retries with the available space instead of allowing the game to stop its audio stream.
- Observation-only stall detection, audio-client telemetry, endpoint notifications, rate-limited error reporting, and asynchronous bounded log rotation.
- Separate lightweight recovery logging that remains useful with full diagnostics disabled.
- Portable recovery and stall-detector tests plus a Windows logger-shutdown harness.
- Public build, contribution, security, architecture, configuration, and release documentation.

### Safety defaults and technical details

- Recovery and adaptive retry enabled.
- Reset/restart and client-recreation fallbacks disabled.
- One retry per failed processing pass.
- Fault injection set to zero and compiled out of public builds.
- Full diagnostics disabled by default.

### Fixed

- Preserved the known started state after a failed `IAudioClient::Start` call.
- Made logger shutdown deterministic around concurrent producers.
- Corrected endpoint notification interface ownership and controlled unregistration.
- Distinguished frozen-clock submissions from render-callback starvation.
- Reduced render-thread instrumentation overhead and rate-limited repeated polling failures.
- Corrected a prototype declaration in vendored MinHook for clean strict-warning builds; behavior is unchanged.

## [0.1.0]

- Upstream spatial-object stereo mix fix for dialogue on Wine/CrossOver.
