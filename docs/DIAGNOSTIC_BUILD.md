# Audio-stall diagnostic build

This branch adds observation-only diagnostics to the existing spatial-audio
stereo fix. It does not reset, restart, replace, or reselect an audio device.

## Enable and configure

Diagnostics are controlled by the `[Diagnostics]` section of
`MetaphorAudioFix.ini`:

```ini
[Diagnostics]
Enabled = true
LogPath = MetaphorAudioFix-diagnostic.log
MaxLogSizeMB = 16
MaxLogFiles = 3
StallTimeoutMs = 2000
WatchdogPollIntervalMs = 250
NonSilentWindowMs = 750
BufferActivityWindowMs = 750
PeriodicStatusMs = 1000
```

An empty or relative `LogPath` is resolved beside `MetaphorAudioFix.asi`
(normally also beside `METAPHOR.exe`). An absolute path is accepted. The logger
rotates at `MaxLogSizeMB`, retaining at most `MaxLogFiles` rotated files in
addition to the active log.

## Logger behavior

Callers format into a fixed-size stack buffer and attempt to enqueue into a
bounded fixed-size queue. They never wait for the queue lock; contention or a
full queue increments a dropped-message counter. A background writer performs
file I/O and rotation. Error records wake the writer immediately and the writer
flushes the file after writing them.

Each record includes:

- a Windows `FILETIME` wall timestamp in 100 ns units
- raw and nanosecond-converted `QueryPerformanceCounter` timestamps
- the Windows thread identifier
- severity and event details

Raw audio samples are never logged. Each submitted object-buffer period is only
classified as `silent` or `non-silent` using an absolute sample threshold of
`0.000001`.

## Observed operations

The diagnostic path records spatial stream activation and the internal stereo
output stream's `IAudioClient` activation, initialization, event handle,
`Start`, `Stop`, and `Reset`. It records each
`IAudioRenderClient::GetBuffer`/`ReleaseBuffer` result, periodic buffer padding,
audio-clock position/frequency, MMDevice endpoint notifications, numeric
HRESULTs, event-driven update entry, thread identifiers, and timestamps.

`IMMNotificationClient` callbacks report default-device changes, state changes,
addition, removal/disconnection, and endpoint property changes.

## Observation-only stall rule

The watchdog emits `SUSPECTED_AUDIO_STALL` once when all of these are true:

1. `IAudioClient::Start` succeeded and no successful `Stop` has followed.
2. A valid `IAudioClock::GetPosition` measurement exists.
3. The measured position has not changed for `StallTimeoutMs`.
4. Non-silent game audio was submitted within `NonSilentWindowMs`.
5. Buffer operations occurred within `BufferActivityWindowMs`, or the started
   event-driven stream still expects buffer operations.

The detector is deliberately conservative when no audio clock is available: it
logs that limitation and never declares a stall. A later position change, stop,
measurement failure, or expiration of the qualifying activity conditions emits
`SUSPECTED_AUDIO_STALL_CONDITION_CLEARED` when applicable.

## Focused tests

The state machine has no Windows dependency. Run it natively on macOS with:

```sh
./tests/run-host-tests.sh
```

The Windows build also compiles `stall_detector_tests.exe`, allowing the same
cases to be exercised under Wine/CrossOver later. The tests cover healthy clock
progress, the exact timeout boundary, one-shot reporting, recovery, stale
non-silent input, stale buffer activity, expected event-driven buffers, and
stopped streams.

## Known uncertainties

- The watchdog samples `IAudioClient` and `IAudioClock` from an MTA worker
  thread after `CoInitializeEx`. Windows audio interfaces are generally usable
  this way, but Wine/CrossOver COM-threading behavior is an empirical question.
- The event handle is not waited on or consumed by the plugin because doing so
  could steal an auto-reset notification from the game. Event activity is
  inferred when the game enters `BeginUpdatingAudioObjects`.
- `GetCurrentPadding` and clock values are sampled periodically and therefore
  are not atomic with a particular `GetBuffer`/`ReleaseBuffer` pair.
- The wrapper's stream lifetime waits for the watchdog thread to exit before
  releasing the audio interfaces, preventing use-after-free at teardown. A
  Wine call that never returns could consequently delay teardown.
- A numeric HRESULT and endpoint notification identify invalidation evidence,
  but Wine may map Core Audio failures to different HRESULTs than Windows.
- Non-silent classification happens after the game fills the spatial object
  buffers and before the wrapper releases its stereo mix. It proves submitted
  signal energy, not that Core Audio produced audible sound.
