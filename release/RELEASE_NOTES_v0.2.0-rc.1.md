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

Testing has only been performed on a MacBook Air with an Apple M5, 10-core CPU, 10-core GPU, 24 GB unified memory, macOS 27.0 build `26A5378n`, and CrossOver Preview 27.0.0.40646 preview build `20260702`. The game was run through Windows Steam using the built-in MacBook Air speakers and normal macOS audio output. The exact game build was not recorded.

- One controlled test recovery succeeded without an audible interruption.
- Ten naturally occurring audio-buffer events recovered successfully.
- Audio remained audible and full-size processing resumed afterward.
- More than 16 minutes of continued gameplay showed no delayed instability.
- Portable recovery, stall-detection, warning-build, and logger-shutdown tests passed.

Testing is still limited. These results do not prove that every possible source of audio loss is fixed on every Mac, Wine version, or output device.

## Installation

1. Close the game and Steam inside CrossOver or Wine.
2. Extract the release ZIP beside `METAPHOR.exe`.
3. In CrossOver, select the bottle containing Steam and open **Wine Configuration**.
4. On the **Libraries** tab, add a `winmm` override.
5. Edit `winmm` and set it to **Native, then Builtin**.
6. Apply the change, fully restart Steam inside the bottle, and launch the game.

Do not overwrite a `winmm.dll` shared by another mod without checking it first.

## Uninstall

Close the game and Steam and remove the patch files. Remove the `winmm` Wine Configuration library override only if no other ASI mod uses it. Do not remove `winmm.dll` or `libwinpthread-1.dll` if another mod depends on them. The patch does not modify saves, game data, Steam Cloud, or unrelated CrossOver bottles.

## Reporting problems

Include your macOS version, Mac model and chip, CrossOver or Wine version, game version, output device, ASI checksum, and a short reviewed log excerpt.

Do not upload game binaries, saves, credentials, personal paths, or unrelated CrossOver bottle contents.

## Known limitations

- Testing has only covered the MacBook Air and CrossOver Preview configuration listed above.
- The cutout fix handles the confirmed temporary audio-buffer problem; other unrelated audio failures may have different causes.
- Experimental stream reset/restart and client recreation remain disabled.
- Full diagnostic logging is optional and adds some overhead.

## Checksums

- `MetaphorCompleteAudioPatch.asi`: `525adf81b3fb1484cbc6c32d1184b16a133caa63cc16c37b239a871516dd0f7b`
- `Metaphor-ReFantazio-Complete-Audio-Patch-v0.2.0-rc.1-win64.zip`: `04f85df675e38e343c575c5d92ac08333aea200b4984055055b3164a4ad95e6b`

This unofficial community project is not affiliated with or endorsed by ATLUS, SEGA, CodeWeavers, Valve, Microsoft, or Apple.
