# Source attribution report

## Upstream work

The repository preserves the complete history of `https://github.com/woahitsraj/MetaphorMacosAudioFix`. Rajan Singh's upstream implementation provides the original spatial-audio compatibility layer and stereo downmix that addresses faint, hollow, or incorrectly mixed dialogue under Wine/CrossOver. The upstream MIT License is preserved unchanged in `LICENSE`.

## Fork additions

Commits after upstream base `977f084` add diagnostic telemetry, controlled COM/logger teardown, observation-only stall classification, adaptive retry for transient WASAPI render-buffer failures, focused tests, and public release engineering. The production adaptive path calculates available frames from buffer capacity and padding and makes one bounded retry. It does not call `Stop`, `Reset`, or `Start`, and it does not recreate the client.

## Third-party source

MinHook and HDE remain in `external-minhook/` under their existing notices. Ultimate ASI Loader remains in `third_party/Ultimate-ASI-Loader/` under its MIT License. See `THIRD_PARTY.md` for the complete inventory.
