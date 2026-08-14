# Metaphor: ReFantazio Complete Audio Patch v1.0.1

This usability update removes the only manual configuration step for people
upgrading from an earlier release. The audio recovery behavior is unchanged
from v1.0.0.

## Easier upgrades

Run `InstallCompleteAudioPatch.exe` normally. If an existing
`MetaphorCompleteAudioPatch.ini` still has the old
`ResetRestartFallback = false` default, the installer changes that setting to
`true` automatically.

The installer preserves every other setting, including diagnostics, custom log
paths, comments, formatting, and line endings. New installations already
receive the recommended configuration. No manual INI editing is required.

The update is written atomically. If the installer cannot complete the INI
migration, it reports the problem and leaves the original file unchanged.

## Validation

- Migration tests cover the old shipped INI and customized configurations.
- CRLF and LF line endings, UTF-8 BOMs, comments, duplicate keys, and accepted
  false-value spellings are covered.
- Portable and Windows migration tests passed.
- Buffer recovery, stall detection, and logger shutdown tests passed under
  CrossOver.
- Fault injection remains compiled out of the public build.

## Installation

Close the game and Steam inside the bottle, extract the entire ZIP, then run
`InstallCompleteAudioPatch.exe` through CrossOver's **Run Command** option.
Fully restart Steam afterward.

## Reporting problems

Use the repository's GitHub Issues page. Include your macOS version, Mac model
and chip, CrossOver or Wine version, game version, output device, ASI checksum,
and a short reviewed log excerpt. Do not upload game binaries, saves,
credentials, personal paths, or unrelated bottle contents.

The repository now includes a guided audio-problem form that asks for these
details and includes a privacy checklist.

## Checksums

- `InstallCompleteAudioPatch.exe`: `f2726c13d55686bf250aaeacf677cb98e440fadd774e7534a72a094630948f0e`
- `MetaphorCompleteAudioPatch.asi`: `7417b0557596c8e436495e13692d5507badf89fdcfb77ca62fb809900feb4781`
- `Metaphor-ReFantazio-Complete-Audio-Patch-v1.0.1-win64.zip`: `b1e33fc6b35612809d396ab98fef19478a79c566b96ac9ed3f211f18dee12203`

This unofficial community project is not affiliated with or endorsed by ATLUS,
SEGA, CodeWeavers, Valve, Microsoft, or Apple.
