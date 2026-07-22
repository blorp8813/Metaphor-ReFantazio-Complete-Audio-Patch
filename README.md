# Metaphor: ReFantazio Complete Audio Patch

An unofficial audio patch for `Metaphor: ReFantazio` running through CrossOver or Wine.

It fixes two separate problems:

- **Quiet, hollow, or badly mixed dialogue.** The game sends multichannel spatial audio to a stereo output incorrectly. The patch mixes those channels down to stereo so voices sound clear and properly balanced.
- **Random loss of all game audio during gameplay.** The game can occasionally request a larger audio packet than Wine can accept at that moment. The patch retries with the amount of space actually available, preventing the game from stopping its audio stream.

The original dialogue and multichannel fix was created by Rajan Singh in the open-source [`MetaphorMacosAudioFix`](https://github.com/woahitsraj/MetaphorMacosAudioFix) project. This independent project is based on that code and adds the random audio-cutout fix, diagnostics, testing, and release packaging.

## Tested configuration

Testing has only been performed on this configuration so far:

- MacBook Air with Apple M5
- 10-core CPU and 10-core GPU
- 24 GB unified memory
- macOS 27.0, build `26A5378n`
- CrossOver Preview 27.0.0.40646, preview build `20260702`
- Windows Steam running `Metaphor: ReFantazio`
- Built-in MacBook Air speakers and normal macOS audio output

The exact game build used during testing was not recorded. Other Mac models, Wine or CrossOver versions, Proton, Windows, and audio devices may work but have not been tested yet. `v0.2.0-rc.1` is a prerelease candidate, not a final universal fix.

## Download

Download the newest ZIP from this repository's **Releases** page. Verify it using the included `.sha256` checksum file before installing.

The internal plugin filenames remain `MetaphorAudioFix.*` because those are the filenames used by the plugin.

## Installation

1. Fully close the game and Steam inside CrossOver or Wine.
2. In Steam, right-click `Metaphor: ReFantazio` and open **Properties → Installed Files → Browse**.
3. Find `METAPHOR.exe` in the game folder.
4. Extract the release ZIP.
5. Copy these four files beside `METAPHOR.exe`:

   - `MetaphorAudioFix.asi`
   - `MetaphorAudioFix.ini`
   - `winmm.dll`
   - `libwinpthread-1.dll`

6. Open CrossOver and select the bottle that contains Steam and the game.
7. Open **Wine Configuration** for that bottle.
8. Select the **Libraries** tab.
9. Under **New override for library**, enter or select `winmm`, then click **Add**.
10. Select the new `winmm` entry and click **Edit**.
11. Choose **Native, then Builtin**, then click **OK**.
12. Click **Apply** and **OK** to close Wine Configuration.
13. Fully restart Steam inside the bottle, then launch the game normally.

If another mod already installed `winmm.dll`, do not overwrite it blindly. It may be a shared ASI loader required by that mod.

## Normal settings

The included INI is ready for normal use. Most users should not need to edit it.

The random-cutout fix is enabled, while more invasive experimental recovery methods remain disabled:

```ini
[Recovery]
Enabled = true
RecoveryLogging = true
AdaptiveBufferRetry = true
ResetRestartFallback = false
RecreateClientFallback = false
FaultInjectBufferTooLargeAfter = 0
```

When the temporary audio-buffer problem occurs, the patch writes a small recovery record to `MetaphorAudioFix.log`. The log is size-limited and rotated automatically. Set `RecoveryLogging = false` to disable it.

## Troubleshooting

### The patch does not load

- Confirm all four files are beside the correct `METAPHOR.exe`.
- Open Wine Configuration for the correct CrossOver bottle and confirm the **Libraries** tab shows `winmm` set to **Native, then Builtin**.
- Fully restart Steam inside the bottle after changing the files or library override.
- Check whether another mod supplies a conflicting `winmm.dll`.

### Dialogue is still quiet or hollow

- Confirm `[Spatial] WrapperEnabled = true` in `MetaphorAudioFix.ini`.
- Confirm the ASI loader is actually loading the plugin.
- Test with a normal stereo macOS output before adding Bluetooth, controller, or virtual audio devices.

### All audio still cuts out

- Check `MetaphorAudioFix.log` for `BUFFER_TOO_LARGE_CAUGHT`, `ADAPTIVE_BUFFER_RETRY_SUCCEEDED`, or `ADAPTIVE_BUFFER_RETRY_FAILED`.
- Enable the diagnostic sample in [`config/MetaphorAudioFix.diagnostic.ini`](config/MetaphorAudioFix.diagnostic.ini) for a short reproduction session.
- Restore the normal production INI after testing because full diagnostics add overhead.

When reporting a problem, include your macOS version, Mac model and chip, CrossOver or Wine version, game version, output device, plugin checksum, and a short relevant log excerpt. Review the log first and remove personal paths or device information you do not want to share.

**Never upload the game executable, game files, saves, account details, personal paths, or unrelated CrossOver bottle contents.**

## Uninstall

1. Close the game and Steam.
2. Remove `MetaphorAudioFix.asi` and `MetaphorAudioFix.ini` from the game folder.
3. Remove `libwinpthread-1.dll` if no other plugin uses it.
4. Remove `winmm.dll` only if no other mod depends on it.
5. In CrossOver's Wine Configuration, remove the `winmm` library override only if no other ASI mod needs it.

The patch does not modify saves, Steam Cloud data, game data, or global CrossOver settings.

## Technical explanation

The dialogue fix keeps the spatial-audio interface expected by the game, then mixes its multichannel static audio objects into a normal stereo render stream.

The cutout fix handles a temporary `AUDCLNT_E_BUFFER_TOO_LARGE` response. For example, if the game asks for 480 audio frames but only 448 currently fit, the patch submits 448 safely instead of returning the error to the game. The next update returns to the normal 480-frame size. This recovery does not automatically stop, reset, restart, or recreate the audio stream.

Developers can read:

- [Architecture](docs/ARCHITECTURE.md)
- [Recovery design](docs/RECOVERY_PROTOTYPE.md)
- [Diagnostic logging](docs/DIAGNOSTIC_BUILD.md)
- [Build instructions](docs/REPRODUCIBLE_BUILDS.md)
- [Test report](release/TEST_REPORT.md)

## Building from source

Install CMake and a Windows x86-64 MinGW cross-compiler, then run:

```bash
./tests/run-host-tests.sh
./build-windows.sh
./scripts/verify-release-config.sh
```

See [Reproducible builds](docs/REPRODUCIBLE_BUILDS.md) for the complete warning-build, Windows-test, and packaging procedure.

## Credits and license

- **Original dialogue and multichannel spatial-audio fix:** Rajan Singh, [`woahitsraj/MetaphorMacosAudioFix`](https://github.com/woahitsraj/MetaphorMacosAudioFix)
- **Additional work in this project:** random audio-cutout recovery, diagnostics, tests, safety hardening, and packaging
- **Third-party components:** MinHook/HDE and Ultimate ASI Loader, under their included licenses

The original MIT `LICENSE` and copyright notice are preserved unchanged. See [NOTICE.md](NOTICE.md) and [THIRD_PARTY.md](THIRD_PARTY.md) for full attribution.

This is an unofficial community project. It is not affiliated with or endorsed by ATLUS, SEGA, CodeWeavers, Valve, Microsoft, or Apple. Use it at your own risk.
