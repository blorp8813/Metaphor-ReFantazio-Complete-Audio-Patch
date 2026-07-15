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
DetailedBufferLogging = false
BufferLogSampleRate = 100
ErrorReminderMs = 30000
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

Raw audio samples are never logged. Submitted object-buffer periods are only
classified as `silent` or `non-silent` using an absolute sample threshold of
`0.000001`. The audio thread normally updates compact atomic counters and
timestamps rather than formatting a record per period. Set
`DetailedBufferLogging=true` to sample one successful buffer period out of each
`BufferLogSampleRate`; failures and HRESULT transitions remain event-driven.

## Observed operations

The diagnostic path records spatial stream activation and the internal stereo
output stream's `IAudioClient` activation, initialization, event handle,
`Start`, `Stop`, and `Reset`. Atomic metrics cover GetBuffer/ReleaseBuffer
counts, last HRESULTs, failure counts, last callback/non-silent timestamps, and
total submitted frames. The watchdog periodically records those aggregates
with buffer padding, audio-clock position/frequency, MMDevice endpoint
notifications, thread identifiers, and timestamps.

`IMMNotificationClient` callbacks report default-device changes, state changes,
addition, removal/disconnection, and endpoint property changes.

Repeated padding/position/frequency failures are rate-limited: the first
failure, an HRESULT change, recovery, and a reminder no more often than
`ErrorReminderMs` are logged.

## Observation-only stall rules

Both conditions require a successfully started stream, a valid audio-clock
measurement, and no clock-position progress for `StallTimeoutMs`.

- `SUSPECTED_CLOCK_STALL_WITH_SUBMISSIONS` means callbacks and non-silent
  submissions continued while the clock remained frozen.
- `SUSPECTED_RENDER_CALLBACK_STARVATION` means active non-silent playback was
  latched when progress stopped, the event-driven stream still expects work,
  and callbacks then ceased beyond `BufferActivityWindowMs`.

The activity latch intentionally survives beyond the recent-activity windows,
so callback starvation can still be reported after the two-second timeout when
callbacks stop immediately after a non-silent period.

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
cases to be exercised under Wine/CrossOver later. Tests cover healthy clock
progress, continued-submission stalls, immediate callback starvation beyond
the timeout, one-shot reporting, recovery, stopped streams, and preservation of
an already-started state after a failed `Start` such as
`AUDCLNT_E_NOT_STOPPED`.

`logger_shutdown_tests.exe` floods the logger queue, requests shutdown, and
requires both confirmed writer termination and closed writer/wake/file handles.

## Controlled teardown and endpoint ownership

The interface returned by `CoCreateInstance` is treated only as the requested
interface. The plugin calls `QueryInterface(IID_IMMDeviceEnumerator)`, hooks and
registers through that result, and retains exactly that query reference while
the callback is registered. Controlled teardown unregisters the callback and
releases the retained enumerator, logging both HRESULTs.

`Log::Shutdown` is called only from `ControlledRuntimeTeardown` on the runtime
coordinator thread—not from `DllMain` or while holding the loader lock. The
writer and wake handles are closed only after `WaitForSingleObject` confirms
writer termination. A timeout retains every handle and waits again outside the
loader lock.

The exported `RequestMetaphorAudioFixDiagnosticShutdown` function signals this
controlled path. A DLL self-reference is retained for the process lifetime so
explicit loader activity cannot unload executable callback/vtable code while a
registration or worker could still reference it. `DLL_PROCESS_DETACH` performs
no COM, hook, logger, wait, or handle teardown.

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
- The second `IAudioClock::GetPosition` value is logged as
  `qpc_position_100ns`; it is the correlated performance-counter timestamp in
  100 ns units, not a device-frame position.
