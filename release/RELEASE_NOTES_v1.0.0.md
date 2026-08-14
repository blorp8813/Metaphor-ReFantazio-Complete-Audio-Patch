# Metaphor: ReFantazio Complete Audio Patch v1.0.0

This release combines the clearer-dialogue stereo mix with a stronger fix for
random loss of all game audio under CrossOver and Wine.

## What changed

The earlier release could recover when at least part of Wine's audio buffer was
available. The first external user log showed the remaining case: the game asked
for 480 frames while all 1440 frames were occupied, leaving no room for even a
smaller retry.

v1.0.0 now allows one bounded Stop, Reset, and Start sequence for that exact
condition, then retries the real audio-buffer request once. A cooldown and
circuit breaker limit repeated attempts. It does not recreate the audio client.

## Validation

- The external M1 Pro log reproduced the full-buffer failure this release targets.
- A 93-minute natural session recovered 21 buffer-pressure events without a failure.
- A controlled full-buffer test completed Stop, Reset, Start, refreshed padding,
  and the real buffer request successfully in 2.751 ms.
- No audible interruption was heard, and audio callbacks continued afterward.
- Portable tests, strict warning builds, and all Windows tests under CrossOver passed.
- Fault injection is compiled out of the public build.

Testing cannot prove every unrelated audio problem is fixed on every system,
but this release directly covers both observed forms of the confirmed Wine
buffer-size failure.

## Installation

Close the game and Steam inside the bottle, extract the entire ZIP, then run
`InstallCompleteAudioPatch.exe` through CrossOver's **Run Command** option.
Fully restart Steam afterward.

### Upgrading from v0.2.0-rc.1

The installer preserves an existing `MetaphorCompleteAudioPatch.ini`. After
upgrading, set `ResetRestartFallback = true` in that file. New installations
already have the correct setting.

## Reporting problems

Include your macOS version, Mac model and chip, CrossOver or Wine version, game
version, output device, ASI checksum, and a short reviewed log excerpt. Do not
upload game binaries, saves, credentials, personal paths, or unrelated bottle
contents.

## Checksums

- `InstallCompleteAudioPatch.exe`: `fe65caeca7892320db88e9250b76057d2a87228a36f402082dbbf0ce7e1b97d0`
- `MetaphorCompleteAudioPatch.asi`: `1f9967eaa5cbf5fec79405253e32f1cf25dffabf5981100aab61e15b36044a8d`
- `Metaphor-ReFantazio-Complete-Audio-Patch-v1.0.0-win64.zip`: `92293bc33b2a713b2894bbb51a6bf59e49570f9581a0bbfb205632ddb1664202`

This unofficial community project is not affiliated with or endorsed by ATLUS,
SEGA, CodeWeavers, Valve, Microsoft, or Apple.
