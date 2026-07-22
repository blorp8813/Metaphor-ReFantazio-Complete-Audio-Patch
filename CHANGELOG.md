# Metaphor: ReFantazio Complete Audio Patch — Changelog

All notable changes to this project are documented here.

## [0.2.0-rc.1] - 2026-07-22

### Added

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
