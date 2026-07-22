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
2. Extract the entire release ZIP to a folder.
3. Select the Steam bottle in CrossOver, click **Run Command**, and run `InstallCompleteAudioPatch.exe`.
4. Choose **Yes**, confirm the detected game folder, and finish installation.
5. Fully restart Steam inside the bottle and launch the game.

The guided installer copies the required files, preserves an existing patch INI, and sets the bottle-local `winmm` override to **Native, then Builtin**. The README includes manual installation as a fallback.

Do not overwrite a `winmm.dll` shared by another mod without checking it first.

## Uninstall

Close the game and Steam, run `InstallCompleteAudioPatch.exe` again, and choose **No**. It asks before removing potentially shared files or the `winmm` override. Manual removal instructions are also in the README. The patch does not modify saves, game data, Steam Cloud, or unrelated CrossOver bottles.

## Reporting problems

Include your macOS version, Mac model and chip, CrossOver or Wine version, game version, output device, ASI checksum, and a short reviewed log excerpt.

Do not upload game binaries, saves, credentials, personal paths, or unrelated CrossOver bottle contents.

## Known limitations

- Testing has only covered the MacBook Air and CrossOver Preview configuration listed above.
- The cutout fix handles the confirmed temporary audio-buffer problem; other unrelated audio failures may have different causes.
- Experimental stream reset/restart and client recreation remain disabled.
- Full diagnostic logging is optional and adds some overhead.

## Checksums

- `InstallCompleteAudioPatch.exe`: `29d2079b274b88f154f5c31e00d2460ba0e98d3a76de9945cbe2d8d88d7c9538`
- `MetaphorCompleteAudioPatch.asi`: `78ecd8841eef85788464fedf3e759e144d74ed624fe76665d91915ac88d1b3de`
- `Metaphor-ReFantazio-Complete-Audio-Patch-v0.2.0-rc.1-win64.zip`: `a43d27a98a31ddde6d0ca4725629b814ffcc9240518961e7b99004c48024c1f0`

This unofficial community project is not affiliated with or endorsed by ATLUS, SEGA, CodeWeavers, Valve, Microsoft, or Apple.
