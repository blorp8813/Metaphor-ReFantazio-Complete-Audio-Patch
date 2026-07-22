# MetaphorAudioFix Adaptive Recovery v0.2.0-rc.1

**Prerelease**

This fork preserves and credits Rajan Singh's original `MetaphorMacosAudioFix` spatial-audio dialogue fix. The release-candidate additions are the diagnostic infrastructure and adaptive buffer recovery described below.

This prerelease adds a narrowly scoped adaptive retry for an observed `AUDCLNT_E_BUFFER_TOO_LARGE` condition in the spatial render path. It queries current padding, calculates safe available capacity, and retries the buffer request once with a bounded frame count. The next processing pass returns to the normal full period.

Production safety settings keep `ResetRestartFallback` and `RecreateClientFallback` disabled. Fault injection is zero and is compiled out of the public ASI. Full diagnostics are off by default; bounded recovery-event logging remains enabled and may be disabled separately.

## Validation completed

- Apple Silicon macOS, CrossOver Preview, Windows Steam, and `Metaphor: ReFantazio` in-game testing.
- Controlled injection prototype: recovery events occurred, audio remained audible, the game did not call `IAudioClient::Stop`, and render callbacks, padding, and audio-clock progress continued.
- Ten naturally occurring buffer-too-large events recovered. Audio remained audible, full-sized processing resumed afterward, and more than 16 minutes of subsequent runtime showed no delayed instability.
- Portable stall-detector and buffer-recovery tests pass, including 1440-frame capacity / 480-frame period / 992-frame padding → 448-frame retry and return to 480 frames.
- Logger concurrent-producer shutdown harness passed under the existing CrossOver environment during prototype verification. The final release-candidate harness result is recorded in `TEST_REPORT.md`.

## Known limitations

- Validation is not yet representative of all Wine, CrossOver, Proton, Windows, hardware, or game versions.
- The adaptive retry handles the confirmed buffer-size condition only; it is not a general output-device recovery mechanism.
- Experimental reset/restart code remains present but disabled. Client recreation is not implemented.
- Full diagnostics may add overhead and logs may include local paths or device metadata; review before sharing.

Verify the release ZIP against its `.sha256` file. Back up existing plugin files before installing. This project is unofficial and unaffiliated with the game publisher or runtime vendors.

## Install or upgrade

Back up the existing ASI, INI, loader, and runtime DLL outside the game directory. New users copy the four runtime files beside `METAPHOR.exe` and configure `winmm` as `native,builtin`. Existing users replace the ASI and merge the new `[Recovery]` settings; do not overwrite a shared ASI loader without checking its owner and checksum.

## Roll back

Exit the game and Steam, then restore the complete original backup set and verify its checksums. No save, Steam Cloud, gameplay, or global CrossOver changes are required.

## Bug reports

Include macOS version, Mac model/chip, runtime and game versions, output device, ASI checksum, reviewed configuration, reproduction steps, and a short redacted log excerpt. Never attach game binaries, saves, credentials, personal paths, or unrelated bottle contents.

## Checksums

- `MetaphorAudioFix.asi`: `5befdfca7c78087edd378d2d21ffbd3321ec5a225de565197d6017f3db9c1a74`
- `MetaphorAudioFix-v0.2.0-rc.1-win64.zip`: `99949b0772dc82010664224b8f6bf05186bbfa097fda6fa280494f6bcf081135`
