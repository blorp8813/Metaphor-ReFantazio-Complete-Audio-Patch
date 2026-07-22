# MetaphorAudioFix

An unofficial Windows x86-64 ASI plugin for `Metaphor: ReFantazio` under Wine and CrossOver. It preserves the game's spatial-audio API while mixing its multichannel static-object bed to stereo, addressing faint or hollow dialogue. This fork also adds a bounded adaptive retry for an observed `AUDCLNT_E_BUFFER_TOO_LARGE` render failure that can otherwise be followed by total audio loss.

This project is not affiliated with, endorsed by, or supported by ATLUS, SEGA, Valve, CodeWeavers, Microsoft, or Apple. Back up existing plugin files before installation. Use at your own risk.

## Release status

`v0.2.0-rc.1` is a prerelease candidate. The adaptive retry was validated with `Metaphor: ReFantazio`, CrossOver Preview on Apple Silicon macOS, and Windows Steam. Exact public compatibility details remain intentionally unfilled until confirmed for release:

- macOS version: **[TO BE SUPPLIED]**
- Mac model and chip: **[TO BE SUPPLIED]**
- CrossOver version used for game validation: **[TO BE SUPPLIED]**
- game version/build: **[TO BE SUPPLIED]**

Broader Wine, CrossOver, Proton, Windows, hardware, and game-version compatibility is not yet established.

The production configuration enables only the narrow adaptive retry. It does **not** automatically call `IAudioClient::Stop`, `Reset`, or `Start`, and it does not recreate the audio client. Experimental fallback code remains disabled.

## Install

The release ZIP contains redistributable runtime files and their notices.

1. In Steam, open the game's `Properties` → `Installed Files` → `Browse`.
2. Back up any existing `MetaphorAudioFix.asi`, `MetaphorAudioFix.ini`, `winmm.dll`, and `libwinpthread-1.dll` outside the game directory.
3. New users should copy `MetaphorAudioFix.asi`, `MetaphorAudioFix.ini`, `winmm.dll`, and `libwinpthread-1.dll` beside `METAPHOR.exe`.
4. Existing MetaphorAudioFix users should replace the ASI with the release version and merge the new `[Recovery]` section into their reviewed INI. Replace `winmm.dll` or `libwinpthread-1.dll` only if their installed checksums differ from the release manifest and no other mod owns those files.
5. Configure Wine to load `winmm` as `native,builtin`. For a Steam launch option:

   ```text
   WINEDLLOVERRIDES="winmm=n,b" %command%
   ```

6. Fully restart Steam inside the bottle or prefix, then launch the game normally.

If another mod already supplies an ASI loader, do not blindly overwrite it. Confirm that the existing loader is compatible or use its documented installation method.

## Upgrade

Exit the game and Steam, make a fresh timestamped backup, verify the new ZIP checksum, replace the ASI, and merge new INI keys without discarding unrelated settings. Keep reset/restart and recreation fallbacks disabled. A release upgrade should not require changes to saves, Steam Cloud, or global CrossOver settings.

## Uninstall or roll back

Exit the game and Steam. Restore the files from your backup, or remove only the files you added. Do not remove a shared `winmm.dll` or runtime DLL if another installed mod depends on it.

## Configuration

The shipped [`MetaphorAudioFix.ini`](MetaphorAudioFix.ini) is the production configuration. A full telemetry example is provided at [`config/MetaphorAudioFix.diagnostic.ini`](config/MetaphorAudioFix.diagnostic.ini).

Production recovery defaults:

```ini
[Recovery]
Enabled = true
RecoveryLogging = true
AdaptiveBufferRetry = true
ResetRestartFallback = false
RecreateClientFallback = false
MaximumAttemptsPerFailure = 1
MaximumRecoveriesPerWindow = 3
RecoveryWindowMs = 30000
RecoveryCooldownMs = 1000
FaultInjectBufferTooLargeAfter = 0
```

`RecoveryLogging` records only configuration and recovery events in the bounded log while full diagnostics are off. Set both `RecoveryLogging = false` and `[Diagnostics] Enabled = false` to disable file logging. Logs rotate according to `MaxLogSizeMB` and `MaxLogFiles`.

Important recovery events include:

- `BUFFER_TOO_LARGE_CAUGHT`
- `ADAPTIVE_BUFFER_RETRY_SUCCEEDED`
- `ADAPTIVE_BUFFER_RETRY_FAILED`
- `RECOVERY_CIRCUIT_BREAKER_OPEN`

Fault injection is compiled out of public builds. The production parser holds `FaultInjectBufferTooLargeAfter` at zero even if a different value is written to the INI.

In plain language, the game normally requests a 480-frame packet. If Wine reports that the packet is temporarily too large—for example, only 448 frames fit—the plugin validates the stream capacity and padding, retries once with 448 frames, mixes and releases exactly those 448 frames, and returns to 480 frames on the next processing pass. It avoids letting that transient error propagate to the game's stream-stop behavior.

## Troubleshooting and bug reports

- Confirm all plugin files are beside the correct `METAPHOR.exe` and that the `winmm` override is `native,builtin`.
- Confirm the active INI has adaptive recovery enabled and both fallbacks disabled.
- Check `MetaphorAudioFix.log` for the recovery event names above. Enable the diagnostic sample only for a bounded reproduction session.
- If startup or audio behavior regresses, roll back all files from the same backup set before testing individual differences.

Bug reports should include macOS version, Mac model and chip, CrossOver/Wine version, game version, output device, ASI SHA-256, relevant reviewed INI settings, reproduction steps, and a short relevant log excerpt. **Do not upload game binaries, saves, account details, personal paths, or unrelated CrossOver bottle files. Review and redact logs before sharing.**

## Build and test

On a clean macOS development environment, install CMake and an x86-64 MinGW cross-toolchain (for example `brew install cmake mingw-w64`), then run:

```bash
./tests/run-host-tests.sh
./build-windows.sh
./scripts/verify-release-config.sh
./scripts/package-release.sh v0.2.0-rc.1
```

The host script runs the portable stall-detector and buffer-recovery tests. The Windows build also produces `logger_shutdown_tests.exe`, which should be run under Wine or CrossOver for its target-runtime check. Toolchain layouts differ; if Homebrew's package does not expose the expected x86-64 compiler names, set `CC` and `CXX` explicitly. See [`docs/REPRODUCIBLE_BUILDS.md`](docs/REPRODUCIBLE_BUILDS.md).

## Known limitations

- Validation so far is limited to one Apple Silicon/CrossOver Preview environment.
- The plugin hooks Windows audio and COM interfaces; Wine implementations and game updates may change behavior.
- Adaptive retry addresses the confirmed buffer-size failure only. It is not a general audio-device recovery system.
- Reset/restart and client-recreation fallbacks are experimental and disabled.
- Full diagnostic logging adds overhead and can contain local paths and device metadata. Review logs before sharing them.
- No game executable, game data, saves, or copyrighted game assets are included.

## Project documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Reproducible builds](docs/REPRODUCIBLE_BUILDS.md)
- [Diagnostic build](docs/DIAGNOSTIC_BUILD.md)
- [Recovery design](docs/RECOVERY_PROTOTYPE.md)
- [Wine spatial-audio notes](docs/WINE_SPATIAL_AUDIO_NOTES.md)
- [Contributing](CONTRIBUTING.md) and [security policy](SECURITY.md)

## License and attribution

The project remains under the upstream [MIT License](LICENSE), Copyright (c) 2026 Rajan Singh. The fork retains upstream history and attribution. Vendored MinHook/HDE and bundled Ultimate ASI Loader retain their own licenses; see [NOTICE.md](NOTICE.md) and [THIRD_PARTY.md](THIRD_PARTY.md).
