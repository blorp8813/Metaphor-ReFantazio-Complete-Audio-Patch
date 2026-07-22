# Metaphor: ReFantazio Complete Audio Patch v0.2.0-rc.1

**Prerelease candidate**

This patch combines two fixes for `Metaphor: ReFantazio` under CrossOver and Wine:

- Clearer dialogue and correct stereo mixing, based on Rajan Singh's original [`MetaphorMacosAudioFix`](https://github.com/woahitsraj/MetaphorMacosAudioFix) project.
- A new fix for the random loss of all game audio during gameplay.

This is an independent repository based on and crediting the original open-source work.

## What causes the random cutout?

The game normally requests a full packet of audio from Wine. Occasionally, less buffer space is available than the game expects. Returning that error can lead the game to stop its audio stream, causing complete silence until the output is reselected.

The patch checks how much space is actually available and retries once with a safe smaller packet. Normal full-size audio processing resumes on the next update. It does not automatically stop, reset, restart, or recreate the stream.

## Test results

- One controlled test recovery succeeded without an audible interruption.
- Ten naturally occurring audio-buffer events recovered successfully.
- Audio remained audible and full-size processing resumed afterward.
- More than 16 minutes of continued gameplay showed no delayed instability.
- Portable recovery, stall-detection, warning-build, and logger-shutdown tests passed.

Testing is still limited. These results do not prove that every possible source of audio loss is fixed on every Mac, Wine version, or output device.

## Installation

1. Close the game and Steam inside CrossOver or Wine.
2. Back up existing `MetaphorAudioFix.asi`, `MetaphorAudioFix.ini`, `winmm.dll`, and `libwinpthread-1.dll` files.
3. Extract the release ZIP beside `METAPHOR.exe`.
4. Set the game's Steam launch option to:

   ```text
   WINEDLLOVERRIDES="winmm=n,b" %command%
   ```

5. Restart Steam and launch the game.

Existing MetaphorAudioFix users can replace the ASI and merge the new `[Recovery]` settings into a customized INI. Do not overwrite a `winmm.dll` shared by another mod without checking it first.

## Rollback

Close the game and Steam, then restore the complete original plugin backup. The patch does not modify saves, game data, Steam Cloud, or global CrossOver settings.

## Reporting problems

Include your macOS version, Mac model and chip, CrossOver or Wine version, game version, output device, ASI checksum, and a short reviewed log excerpt.

Do not upload game binaries, saves, credentials, personal paths, or unrelated CrossOver bottle contents.

## Known limitations

- Testing has primarily covered one Apple Silicon Mac and CrossOver Preview environment.
- The cutout fix handles the confirmed temporary audio-buffer problem; other unrelated audio failures may have different causes.
- Experimental stream reset/restart and client recreation remain disabled.
- Full diagnostic logging is optional and adds some overhead.

## Checksums

- `MetaphorAudioFix.asi`: `5befdfca7c78087edd378d2d21ffbd3321ec5a225de565197d6017f3db9c1a74`
- `Metaphor-ReFantazio-Complete-Audio-Patch-v0.2.0-rc.1-win64.zip`: `13c00b8c45eec7e2e070f59e9988cf48948a6e5474c715f59662a882a265ca08`

This unofficial community project is not affiliated with or endorsed by ATLUS, SEGA, CodeWeavers, Valve, Microsoft, or Apple.
